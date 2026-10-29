//! Copies declared persistent state without changing the source environment.

use super::{check_name, firmware, sparse, Loaded, SystemRecord, INPUTS};
use anyhow::{ensure, Context, Result};
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::os::unix::fs::DirBuilderExt;
use std::path::{Path, PathBuf};

pub(super) const ROOT: &str = "state";
pub(super) const VARIABLE: &str = "QCOM_VP_STATE_ROOT";

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub(super) struct Declaration {
    pub name: String,
    pub seed: Option<String>,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
pub(super) struct Identity {
    bytes: u64,
    sha256: String,
}

type Inventory = BTreeMap<String, Option<Identity>>;

#[derive(Debug, Deserialize, Serialize)]
pub(super) struct Record {
    pub mode: String,
    pub source: Option<String>,
    pub before: Inventory,
    pub after: Option<Inventory>,
}

#[derive(Deserialize)]
struct Previous {
    outcome: String,
    environment: PreviousEnvironment,
    system: SystemRecord,
    state: Record,
}

#[derive(Deserialize)]
struct PreviousEnvironment {
    manifest_sha256: String,
}

pub(super) fn validate(loaded: &[Declaration], inputs: &BTreeSet<&str>) -> Result<()> {
    let mut names = BTreeSet::new();
    for declaration in loaded {
        check_name(&declaration.name)?;
        ensure!(
            names.insert(declaration.name.as_str()),
            "A state name occurs more than once"
        );
        if let Some(seed) = &declaration.seed {
            ensure!(
                inputs.contains(seed.as_str()),
                "The state seed {seed} is not a declared input"
            );
        }
    }
    for name in &names {
        for (separator, _) in name.match_indices('/') {
            ensure!(
                !names.contains(&name[..separator]),
                "State names overlap at {name}"
            );
        }
    }
    Ok(())
}

fn directory(path: &Path) -> Result<()> {
    fs::DirBuilder::new()
        .recursive(true)
        .mode(0o700)
        .create(path)
        .with_context(|| format!("Cannot create the state directory {}", path.display()))
}

fn regular(path: &Path) -> Result<bool> {
    match fs::symlink_metadata(path) {
        Ok(metadata) => {
            ensure!(
                metadata.file_type().is_file(),
                "State is not a regular file: {}",
                path.display()
            );
            Ok(true)
        }
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(false),
        Err(error) => Err(error.into()),
    }
}

fn inspect_tree(root: &Path, relative: &Path, allowed: &BTreeSet<PathBuf>) -> Result<()> {
    let path = root.join(relative);
    ensure!(
        fs::symlink_metadata(&path)?.file_type().is_dir(),
        "The state path is not a directory"
    );
    for entry in fs::read_dir(&path)? {
        let entry = entry?;
        let name = relative.join(entry.file_name());
        let kind = entry.file_type()?;
        if kind.is_dir() {
            ensure!(
                allowed.iter().any(|file| file.starts_with(&name)),
                "An undeclared state directory exists: {}",
                name.display()
            );
            inspect_tree(root, &name, allowed)?;
        } else {
            ensure!(
                kind.is_file() && allowed.contains(&name),
                "An undeclared or nonregular state file exists: {}",
                name.display()
            );
        }
    }
    Ok(())
}

fn inventory(root: &Path, declarations: &[Declaration]) -> Result<Inventory> {
    let allowed = declarations
        .iter()
        .map(|item| PathBuf::from(&item.name))
        .collect();
    inspect_tree(root, Path::new(""), &allowed)?;
    let mut result = BTreeMap::new();
    for item in declarations {
        let path = root.join(&item.name);
        let identity = if regular(&path)? {
            Some(Identity {
                bytes: fs::metadata(&path)?.len(),
                sha256: firmware::hash(&path)?,
            })
        } else {
            None
        };
        result.insert(item.name.clone(), identity);
    }
    Ok(result)
}

pub(super) fn prepare(
    loaded: &Loaded,
    out: &Path,
    system: &SystemRecord,
    resume: Option<&Path>,
    clone: Option<&Path>,
) -> Result<Record> {
    ensure!(
        resume.is_none() || clone.is_none(),
        "Select either resume or clone, not both"
    );
    let declarations = &loaded.environment.state;
    ensure!(
        !declarations.is_empty() || (resume.is_none() && clone.is_none()),
        "The environment declares no persistent state"
    );
    let root = out.join(ROOT);
    directory(&root)?;
    let source = resume.or(clone);
    let (mode, source) = if let Some(source) = source {
        let source = source
            .canonicalize()
            .context("Cannot resolve the source environment")?;
        let previous: Previous = serde_json::from_slice(&fs::read(source.join("result.json"))?)
            .context("The source has no completed state record")?;
        ensure!(
            matches!(previous.outcome.as_str(), "exited" | "signaled" | "timeout"),
            "The source process has no completed outcome"
        );
        ensure!(
            previous.environment.manifest_sha256 == loaded.sha256,
            "The source environment manifest differs"
        );
        ensure!(
            previous.system.sha256 == system.sha256,
            "The source system executable differs"
        );
        let expected = previous
            .state
            .after
            .context("The source state has no final identity")?;
        let source_root = source.join(ROOT);
        let before = inventory(&source_root, declarations)?;
        ensure!(
            before == expected,
            "The source state differs from its final record"
        );
        for declaration in declarations {
            if before[&declaration.name].is_some() {
                let target = root.join(&declaration.name);
                directory(target.parent().context("The state file has no parent")?)?;
                sparse::copy(&source_root.join(&declaration.name), &target)?;
            }
        }
        ensure!(
            inventory(&root, declarations)? == expected,
            "The copied state differs from its source"
        );
        ensure!(
            inventory(&source_root, declarations)? == expected,
            "The source state changed during its copy"
        );
        (
            if resume.is_some() { "resume" } else { "clone" },
            Some(source.display().to_string()),
        )
    } else {
        for declaration in declarations {
            let target = root.join(&declaration.name);
            directory(target.parent().context("The state file has no parent")?)?;
            if let Some(seed) = &declaration.seed {
                sparse::copy(&out.join(INPUTS).join(seed), &target)?;
                ensure!(
                    firmware::hash(&target)? == firmware::hash(&out.join(INPUTS).join(seed))?,
                    "The state seed copy differs"
                );
            }
        }
        ("fresh", None)
    };
    Ok(Record {
        mode: mode.to_owned(),
        source,
        before: inventory(&root, declarations)?,
        after: None,
    })
}

pub(super) fn finish(loaded: &Loaded, out: &Path, record: &mut Record) -> Result<()> {
    // An altered profile must never authorize continuation of its dependent state.
    for file in &loaded.environment.files {
        firmware::check_hash(&out.join(INPUTS).join(&file.name), &file.sha256)
            .context("An immutable input changed during the stateful experiment")?;
    }
    record.after = Some(inventory(&out.join(ROOT), &loaded.environment.state)?);
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::super::{load_environment, snapshot_inputs};
    use super::*;

    fn setup(base: &Path) -> Loaded {
        fs::write(base.join("platform.lua"), b"return {}\n").unwrap();
        fs::write(base.join("media.img"), b"initial media").unwrap();
        let platform = firmware::hash(&base.join("platform.lua")).unwrap();
        let media = firmware::hash(&base.join("media.img")).unwrap();
        let manifest = format!(
            "schema=1\nname='state'\ndescription='Persistent state test'\n\
             entry='Cold reset'\nsecurity_policy='Virtual test identity'\n\
             limitations=['No hardware qualification']\nplatform='platform.lua'\n\
             [[files]]\nname='platform.lua'\npath='platform.lua'\nsha256='{platform}'\n\
             [[files]]\nname='media.img'\npath='media.img'\nsha256='{media}'\n\
             [[state]]\nname='media.img'\nseed='media.img'\n\
             [[state]]\nname='rpmb.bin'\n"
        );
        fs::write(base.join("environment.toml"), manifest).unwrap();
        load_environment(&base.join("environment.toml")).unwrap()
    }

    fn system() -> SystemRecord {
        SystemRecord {
            path: "test-system".to_owned(),
            sha256: "12".repeat(32),
        }
    }

    fn output(loaded: &Loaded, path: &Path) {
        directory(path).unwrap();
        snapshot_inputs(&loaded.environment, &loaded.base, path).unwrap();
    }

    fn seal(loaded: &Loaded, out: &Path, record: &mut Record) {
        finish(loaded, out, record).unwrap();
        let result = serde_json::json!({
            "outcome": "exited",
            "environment": { "manifest_sha256": loaded.sha256 },
            "system": { "path": "test-system", "sha256": system().sha256 },
            "state": record,
        });
        fs::write(
            out.join("result.json"),
            serde_json::to_vec(&result).unwrap(),
        )
        .unwrap();
    }

    #[test]
    fn resume_and_clone_preserve_the_complete_source_and_fresh_state_uses_seeds() {
        let dir = tempfile::tempdir().unwrap();
        let loaded = setup(dir.path());
        let first = dir.path().join("first");
        output(&loaded, &first);
        let mut record = prepare(&loaded, &first, &system(), None, None).unwrap();
        assert!(record.before["rpmb.bin"].is_none());
        fs::write(first.join("state/media.img"), b"provisioned media").unwrap();
        fs::write(first.join("state/rpmb.bin"), b"paired key counter and data").unwrap();
        seal(&loaded, &first, &mut record);
        for mode in ["resume", "clone"] {
            let out = dir.path().join(mode);
            output(&loaded, &out);
            let copy = prepare(
                &loaded,
                &out,
                &system(),
                (mode == "resume").then_some(first.as_path()),
                (mode == "clone").then_some(first.as_path()),
            )
            .unwrap();
            assert_eq!(copy.mode, mode);
            assert_eq!(Some(copy.before), record.after);
            fs::write(out.join("state/rpmb.bin"), b"new branch").unwrap();
            assert_eq!(
                fs::read(first.join("state/rpmb.bin")).unwrap(),
                b"paired key counter and data"
            );
        }
        let fresh = dir.path().join("fresh");
        output(&loaded, &fresh);
        prepare(&loaded, &fresh, &system(), None, None).unwrap();
        assert_eq!(
            fs::read(fresh.join("state/media.img")).unwrap(),
            b"initial media"
        );
        assert!(!fresh.join("state/rpmb.bin").exists());
    }

    #[test]
    fn continuation_rejects_incomplete_or_changed_identity_and_state() {
        let dir = tempfile::tempdir().unwrap();
        let mut loaded = setup(dir.path());
        let source = dir.path().join("source");
        output(&loaded, &source);
        let mut record = prepare(&loaded, &source, &system(), None, None).unwrap();
        fs::write(source.join("state/rpmb.bin"), b"key counter and data").unwrap();
        let missing = dir.path().join("missing");
        output(&loaded, &missing);
        assert!(prepare(&loaded, &missing, &system(), Some(&source), None).is_err());
        seal(&loaded, &source, &mut record);

        let wrong_system = dir.path().join("wrong-system");
        output(&loaded, &wrong_system);
        let mut changed = system();
        changed.sha256 = "34".repeat(32);
        assert!(prepare(&loaded, &wrong_system, &changed, Some(&source), None).is_err());

        let original_manifest = loaded.sha256.clone();
        loaded.sha256 = "56".repeat(32);
        let wrong_identity = dir.path().join("wrong-identity");
        output(&loaded, &wrong_identity);
        assert!(prepare(&loaded, &wrong_identity, &system(), Some(&source), None).is_err());
        loaded.sha256 = original_manifest;

        fs::write(source.join("state/rpmb.bin"), b"changed counter").unwrap();
        let changed_state = dir.path().join("changed-state");
        output(&loaded, &changed_state);
        assert!(prepare(&loaded, &changed_state, &system(), Some(&source), None).is_err());
        fs::remove_file(source.join("state/rpmb.bin")).unwrap();
        let missing_state = dir.path().join("missing-state");
        output(&loaded, &missing_state);
        assert!(prepare(&loaded, &missing_state, &system(), Some(&source), None).is_err());
    }

    #[test]
    fn undeclared_state_symlinks_and_changed_profiles_cannot_receive_final_identities() {
        let dir = tempfile::tempdir().unwrap();
        let loaded = setup(dir.path());
        for condition in ["extra", "symlink", "profile"] {
            let out = dir.path().join(condition);
            output(&loaded, &out);
            let mut record = prepare(&loaded, &out, &system(), None, None).unwrap();
            match condition {
                "extra" => fs::write(out.join("state/untracked.bin"), b"hidden authority").unwrap(),
                "symlink" => std::os::unix::fs::symlink(
                    dir.path().join("media.img"),
                    out.join("state/rpmb.bin"),
                )
                .unwrap(),
                _ => fs::write(out.join("inputs/platform.lua"), b"changed identity").unwrap(),
            }
            assert!(finish(&loaded, &out, &mut record).is_err());
            assert!(record.after.is_none());
        }
    }
}
