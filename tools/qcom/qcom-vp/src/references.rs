//! Check evidence references in root Markdown, `docs/`, and `models/`.
//!
//! A reference names its source by identity, not by location. A git source
//! uses a GitHub permalink or `<store>@<commit>:<path>`. An untracked file
//! uses `<store>:<path> sha256=<digest>`. The manifest `docs/references.toml`
//! records store identities and environment variables. A local root is
//! optional configuration: the store `env` variable selects it, or a private
//! `--manifest` sets `root` relative to the manifest directory. A store
//! without a local source fails with instructions, so this check never
//! reports absent evidence as verified.
//!
//! Scope is root-level Markdown, `docs/`, and `models/`. The check does not
//! audit the rest of the tree and does not certify publication.

use crate::firmware;
use anyhow::{ensure, Context, Result};
use serde::Deserialize;
use std::collections::BTreeMap;
use std::ffi::OsString;
use std::fs;
use std::path::{Component, Path, PathBuf};
use std::process::{Command, Stdio};

/// The command line options of the `references` action.
pub struct Options {
    pub repo: PathBuf,
    pub manifest: Option<PathBuf>,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct Manifest {
    schema: u32,
    stores: BTreeMap<String, Store>,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct Store {
    /// Optional local root, relative to the manifest directory.
    root: Option<String>,
    /// Environment variable that replaces the default root.
    env: String,
    /// GitHub `owner/repository` for a git store.
    #[serde(default)]
    remote: Option<String>,
    /// How to obtain the store.
    #[allow(dead_code)]
    obtain: String,
}

#[derive(Debug, PartialEq, Eq)]
enum Citation {
    /// A git object at an exact commit. `store` is a store name or a remote.
    Pinned {
        store: String,
        commit: String,
        path: String,
    },
    /// An untracked file and its SHA-256.
    Hashed {
        store: String,
        path: String,
        sha256: String,
    },
    /// A location that depends on one machine or one session.
    Local { text: String, reason: &'static str },
}

const MACHINE_MARKERS: &[(&str, &str)] = &[
    ("/home/", "absolute home path"),
    ("~/Projects", "home-relative project path"),
    ("~/Downloads", "home-relative download path"),
    ("local://", "session-local artifact"),
    ("/tmp/", "temporary file"),
];

fn is_hex(text: &str, length: usize) -> bool {
    text.len() == length && text.bytes().all(|b| b.is_ascii_hexdigit())
}

fn path_end(text: &str) -> usize {
    text.find(|c: char| c.is_whitespace() || "`)|>\"'#,;".contains(c))
        .unwrap_or(text.len())
}

/// Find the citations on one line. `store_names` selects which prefixes are
/// stores, so that ordinary `key=value` text and unrelated colons stay out.
fn scan_line(line: &str, file_dir: &Path, store_names: &[&str]) -> Vec<Citation> {
    let mut found = Vec::new();

    let permalink = "https://github.com/";
    let mut rest = line;
    while let Some(start) = rest.find(permalink) {
        let tail = &rest[start + permalink.len()..];
        let mut parts = tail.splitn(5, '/');
        if let (Some(owner), Some(repository), Some("blob"), Some(commit), Some(path)) = (
            parts.next(),
            parts.next(),
            parts.next(),
            parts.next(),
            parts.next(),
        ) {
            if is_hex(commit, 40) {
                found.push(Citation::Pinned {
                    store: format!("{owner}/{repository}"),
                    commit: commit.to_owned(),
                    path: path[..path_end(path)].to_owned(),
                });
            }
        }
        rest = tail;
    }

    for (at, _) in line.match_indices('@') {
        let after = &line[at + 1..];
        if after.len() < 41 || !is_hex(&after[..40], 40) || after.as_bytes()[40] != b':' {
            continue;
        }
        let before = &line[..at];
        let start = before
            .rfind(|c: char| !(c.is_ascii_alphanumeric() || "._/-".contains(c)))
            .map_or(0, |index| index + 1);
        let path = &after[41..];
        found.push(Citation::Pinned {
            store: before[start..].to_owned(),
            commit: after[..40].to_owned(),
            path: path[..path_end(path)].to_owned(),
        });
    }

    for (at, _) in line.match_indices(" sha256=") {
        let digest = &line[at + 8..];
        let digest = &digest[..path_end(digest)];
        let before = &line[..at];
        let start = before
            .rfind(|c: char| c.is_whitespace() || "`|(".contains(c))
            .map_or(0, |index| index + 1);
        let Some((store, path)) = before[start..].split_once(':') else {
            continue;
        };
        if store_names.contains(&store) && is_hex(digest, 64) {
            found.push(Citation::Hashed {
                store: store.to_owned(),
                path: path.to_owned(),
                sha256: digest.to_owned(),
            });
        }
    }

    for (marker, reason) in MACHINE_MARKERS {
        for (at, _) in line.match_indices(marker) {
            let text = &line[at..];
            found.push(Citation::Local {
                text: text[..path_end(text)].to_owned(),
                reason,
            });
        }
    }
    for name in store_names {
        for prefix in [format!("`{name}/"), format!("../{name}/")] {
            for (at, _) in line.match_indices(&prefix) {
                let text = &line[at..];
                let text = text.trim_start_matches('`');
                found.push(Citation::Local {
                    text: text[..path_end(text)].to_owned(),
                    reason: "unpinned store path",
                });
            }
        }
    }

    for (at, _) in line.match_indices("](") {
        let target = &line[at + 2..];
        let target = &target[..target.find(')').unwrap_or(target.len())];
        let target = target.split('#').next().unwrap_or_default();
        if target.is_empty() || target.contains("://") || target.starts_with("mailto:") {
            continue;
        }
        if escapes(file_dir, Path::new(target)) {
            found.push(Citation::Local {
                text: target.to_owned(),
                reason: "link outside the repository",
            });
        }
    }
    found
}

/// Report whether `target`, relative to `file_dir`, leaves the repository.
/// `file_dir` is relative to the repository root.
fn escapes(file_dir: &Path, target: &Path) -> bool {
    if target.is_absolute() {
        return true;
    }
    let mut depth: isize = 0;
    for component in file_dir.components().chain(target.components()) {
        match component {
            Component::Normal(_) => depth += 1,
            Component::ParentDir => {
                depth -= 1;
                if depth < 0 {
                    return true;
                }
            }
            _ => {}
        }
    }
    false
}

fn store_root(
    manifest_dir: &Path,
    store: &Store,
    environment: &impl Fn(&str) -> Option<OsString>,
) -> Result<PathBuf> {
    if let Some(value) = environment(&store.env).filter(|value| !value.is_empty()) {
        return Ok(PathBuf::from(value));
    }
    let root = store
        .root
        .as_deref()
        .filter(|root| !root.is_empty())
        .with_context(|| {
            format!(
                "Local source is unconfigured. Set {}, or supply a root with --manifest",
                store.env
            )
        })?;
    if let Some(rest) = root.strip_prefix("~/") {
        let home = environment("HOME").filter(|home| !home.is_empty())
            .context("Cannot expand store root: HOME is unset or empty")?;
        return Ok(PathBuf::from(home).join(rest));
    }
    Ok(manifest_dir.join(root))
}

/// The result of one citation. A pinned citation of a remote without a
/// manifest store keeps its identity, but no local clone can confirm it.
enum Outcome {
    Resolved,
    RemoteOnly,
}

fn resolve(
    manifest_dir: &Path,
    manifest: &Manifest,
    citation: &Citation,
    environment: &impl Fn(&str) -> Option<OsString>,
) -> Result<Outcome> {
    match citation {
        Citation::Local { reason, .. } => anyhow::bail!("{reason}"),
        Citation::Pinned {
            store,
            commit,
            path,
        } => {
            let Some(entry) = manifest.stores.get(store).or_else(|| {
                manifest.stores.values().find(|entry| {
                    entry
                        .remote
                        .as_deref()
                        .is_some_and(|remote| remote.eq_ignore_ascii_case(store))
                })
            }) else {
                ensure!(store.contains('/'), "no store named {store}");
                return Ok(Outcome::RemoteOnly);
            };
            let root = store_root(manifest_dir, entry, environment)?;
            ensure!(
                root.is_dir(),
                "Local source {} is absent. Set {}, or supply a root with --manifest",
                root.display(),
                entry.env
            );
            let object = format!("{commit}:{}", path.trim_end_matches('/'));
            let status = Command::new("git")
                .arg("-C")
                .arg(&root)
                .args(["cat-file", "-e", &object])
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .status()
                .context("Cannot run git")?;
            ensure!(
                status.success(),
                "{object} is absent from {}",
                root.display()
            );
            Ok(Outcome::Resolved)
        }
        Citation::Hashed {
            store,
            path,
            sha256,
        } => {
            let entry = manifest
                .stores
                .get(store)
                .with_context(|| format!("no store named {store}"))?;
            let root = store_root(manifest_dir, entry, environment)?;
            ensure!(
                root.is_dir(),
                "Local source {} is absent. Set {}, or supply a root with --manifest",
                root.display(),
                entry.env
            );
            firmware::check_hash(&root.join(path), sha256)?;
            Ok(Outcome::Resolved)
        }
    }
}

fn markdown_files(directory: &Path, recursive: bool, files: &mut Vec<PathBuf>) -> Result<()> {
    let mut entries = fs::read_dir(directory)
        .with_context(|| format!("Cannot read {}", directory.display()))?
        .collect::<std::io::Result<Vec<_>>>()?;
    entries.sort_by_key(|entry| entry.path());
    for entry in entries {
        let path = entry.path();
        if entry.file_type()?.is_dir() {
            if recursive {
                markdown_files(&path, true, files)?;
            }
        } else if path.extension().is_some_and(|extension| extension == "md") {
            files.push(path);
        }
    }
    Ok(())
}

fn reference_files(repo: &Path) -> Result<Vec<PathBuf>> {
    let mut files = Vec::new();
    markdown_files(repo, false, &mut files)?;
    for directory in ["docs", "models"] {
        let path = repo.join(directory);
        if path.is_dir() {
            markdown_files(&path, true, &mut files)?;
        }
    }
    Ok(files)
}

fn describe(citation: &Citation) -> String {
    match citation {
        Citation::Pinned {
            store,
            commit,
            path,
        } => format!("{store}@{}:{path}", &commit[..12]),
        Citation::Hashed { store, path, .. } => format!("{store}:{path}"),
        Citation::Local { text, .. } => text.clone(),
    }
}

pub fn run(options: &Options) -> Result<()> {
    let repo = options
        .repo
        .canonicalize()
        .with_context(|| format!("Cannot resolve the repository {}", options.repo.display()))?;
    let manifest_path = options
        .manifest
        .clone()
        .unwrap_or_else(|| repo.join("docs/references.toml"));
    let manifest: Manifest = toml::from_str(
        &fs::read_to_string(&manifest_path)
            .with_context(|| format!("Cannot read {}", manifest_path.display()))?,
    )
    .with_context(|| format!("Invalid manifest {}", manifest_path.display()))?;
    ensure!(
        manifest.schema == 1,
        "Unsupported reference manifest schema {}",
        manifest.schema
    );
    let manifest_dir = manifest_path
        .canonicalize()?
        .parent()
        .context("The manifest must have a parent directory")?
        .to_path_buf();
    let names: Vec<&str> = manifest.stores.keys().map(String::as_str).collect();
    let files = reference_files(&repo)?;

    let (mut resolved, mut remote_only, mut failed) = (0_usize, 0_usize, 0_usize);
    for file in &files {
        let relative = file.strip_prefix(&repo).unwrap_or(file);
        let file_dir = relative.parent().unwrap_or(Path::new(""));
        let text =
            fs::read_to_string(file).with_context(|| format!("Cannot read {}", file.display()))?;
        for (index, line) in text.lines().enumerate() {
            for citation in scan_line(line, file_dir, &names) {
                match resolve(&manifest_dir, &manifest, &citation, &|name| std::env::var_os(name)) {
                    Ok(Outcome::Resolved) => resolved += 1,
                    Ok(Outcome::RemoteOnly) => remote_only += 1,
                    Err(error) => {
                        failed += 1;
                        println!(
                            "REFERENCE_UNRESOLVED {}:{} {}: {error:#}",
                            relative.display(),
                            index + 1,
                            describe(&citation)
                        );
                    }
                }
            }
        }
    }
    println!(
        "REFERENCES files={} resolved={resolved} remote_only={remote_only} unresolved={failed}",
        files.len()
    );
    ensure!(failed == 0, "{failed} references do not resolve");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    const STORES: &[&str] = &["edl-re", "gobbl-lab"];
    const COMMIT: &str = "00b81d02c9bdaba2a4870eb00719dd7fbc6547d2";
    const DIGEST: &str = "91bd8f81efa7a5940d4b6d4c220388b559ceef39b6b47dd7b4b0727a1beb1d1c";

    #[test]
    fn citations_inside_links_tables_and_code_spans() {
        let line = format!(
            "| [Lab](https://github.com/1vivy/gobbl-lab/blob/{COMMIT}/README.md#boundary) | \
             `qcom-rehost@{COMMIT}:docs/a.md#L4` and `edl-re:pbl8850.bin sha256={DIGEST}` |"
        );
        assert_eq!(
            scan_line(&line, Path::new("docs"), STORES),
            vec![
                Citation::Pinned {
                    store: "1vivy/gobbl-lab".into(),
                    commit: COMMIT.into(),
                    path: "README.md".into()
                },
                Citation::Pinned {
                    store: "qcom-rehost".into(),
                    commit: COMMIT.into(),
                    path: "docs/a.md".into()
                },
                Citation::Hashed {
                    store: "edl-re".into(),
                    path: "pbl8850.bin".into(),
                    sha256: DIGEST.into()
                },
            ]
        );
    }

    #[test]
    fn unrelated_digest_fields_are_not_citations() {
        let line = format!("`FUSE_CALIBRATION_SOURCE source=private-page sha256={DIGEST}`");
        assert!(scan_line(&line, Path::new("models/x"), STORES).is_empty());
    }

    #[test]
    fn machine_local_and_unpinned_locations_are_reported() {
        let line = "[a](../../gobbl-lab/README.md) [b](../models/x.md) `gobbl-lab/records/r/` local://x.json";
        let reasons: Vec<_> = scan_line(line, Path::new("docs"), STORES)
            .into_iter()
            .map(|citation| match citation {
                Citation::Local { reason, .. } => reason,
                other => panic!("{other:?}"),
            })
            .collect();
        assert_eq!(
            reasons,
            [
                "session-local artifact",
                "unpinned store path",
                "unpinned store path",
                "link outside the repository"
            ]
        );
    }

    #[test]
    fn local_root_prefers_the_environment_and_starts_at_the_manifest() {
        let entry = Store {
            root: Some("stores/lab".into()),
            env: "QCOM_VP_REF_TEST_LAB".into(),
            remote: None,
            obtain: String::new(),
        };
        let manifest_dir = Path::new("/srv/private-manifest");
        let environment = |key: &str| {
            (key == "QCOM_VP_REF_TEST_LAB").then(|| OsString::from("/srv/clone"))
        };
        assert_eq!(
            store_root(manifest_dir, &entry, &environment).unwrap(),
            Path::new("/srv/clone")
        );
        let environment = |_: &str| None;
        assert_eq!(
            store_root(manifest_dir, &entry, &environment).unwrap(),
            Path::new("/srv/private-manifest/stores/lab")
        );
    }

    #[test]
    fn unconfigured_source_names_its_variable() {
        let manifest = Manifest {
            schema: 1,
            stores: BTreeMap::from([(
                "edl-re".to_owned(),
                Store {
                    root: None,
                    env: "QCOM_VP_REF_TEST_UNSET".into(),
                    remote: None,
                    obtain: String::new(),
                },
            )]),
        };
        let cite = Citation::Hashed {
            store: "edl-re".into(),
            path: "blob".into(),
            sha256: DIGEST.into(),
        };
        let Err(error) = resolve(Path::new("/srv/manifest"), &manifest, &cite, &|_: &str| None)
        else {
            panic!("an unconfigured store must not resolve");
        };
        let error = format!("{error:#}");
        assert!(error.contains("unconfigured"), "{error}");
        assert!(error.contains("QCOM_VP_REF_TEST_UNSET"), "{error}");
    }

    #[test]
    fn absent_configured_source_names_its_variable() {
        let directory = tempfile::tempdir().unwrap();
        let manifest = Manifest {
            schema: 1,
            stores: BTreeMap::from([(
                "edl-re".to_owned(),
                Store {
                    root: Some(directory.path().join("missing").display().to_string()),
                    env: "QCOM_VP_REF_TEST_ABSENT".into(),
                    remote: None,
                    obtain: String::new(),
                },
            )]),
        };
        let cite = Citation::Hashed {
            store: "edl-re".into(),
            path: "blob".into(),
            sha256: DIGEST.into(),
        };
        let Err(error) = resolve(Path::new("/"), &manifest, &cite, &|_: &str| None) else {
            panic!("an absent source directory must not resolve");
        };
        let error = format!("{error:#}");
        assert!(error.contains("absent"), "{error}");
        assert!(error.contains("QCOM_VP_REF_TEST_ABSENT"), "{error}");
    }

    #[test]
    fn hashed_citations_compare_the_file_digest() {
        let store = tempfile::tempdir().unwrap();
        fs::write(store.path().join("blob"), b"evidence").unwrap();
        let manifest = Manifest {
            schema: 1,
            stores: BTreeMap::from([(
                "edl-re".to_owned(),
                Store {
                    root: Some(store.path().display().to_string()),
                    env: "QCOM_VP_REF_TEST_UNSET".into(),
                    remote: None,
                    obtain: String::new(),
                },
            )]),
        };
        let cite = |sha256: &str| Citation::Hashed {
            store: "edl-re".into(),
            path: "blob".into(),
            sha256: sha256.into(),
        };
        let actual = firmware::hash(&store.path().join("blob")).unwrap();
        assert!(resolve(Path::new("/"), &manifest, &cite(&actual), &|_: &str| None).is_ok());
        let Err(error) = resolve(Path::new("/"), &manifest, &cite(DIGEST), &|_: &str| None) else {
            panic!("a digest mismatch must not resolve");
        };
        assert!(format!("{error:#}").contains("mismatch"), "{error:#}");
    }

    #[test]
    fn root_markdown_and_scanned_directories_are_collected() {
        let repo = tempfile::tempdir().unwrap();
        let write = |relative: &str| {
            let path = repo.path().join(relative);
            fs::create_dir_all(path.parent().unwrap()).unwrap();
            fs::write(path, b"text\n").unwrap();
        };
        write("README.md");
        write("notes.txt");
        write("docs/a.md");
        write("docs/deep/b.md");
        write("models/x/y.md");
        write("src/z.md");

        let files: Vec<_> = reference_files(repo.path())
            .unwrap()
            .iter()
            .map(|file| {
                file.strip_prefix(repo.path())
                    .unwrap()
                    .display()
                    .to_string()
            })
            .collect();
        assert_eq!(files, ["README.md", "docs/a.md", "docs/deep/b.md", "models/x/y.md"]);
    }
}
