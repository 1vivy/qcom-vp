//! Runs a declared research experiment in a new output directory.
//!
//! The environment file declares the inputs, their identities, and the entry
//! conditions. The platform Lua file remains the machine description, because
//! this module does not parse or reproduce the QBox configuration. The module
//! copies the declared inputs into the output, verifies their identities,
//! starts the system executable with the snapshot, and records the process
//! outcome.
//!
//! The platform script is trusted host code. It runs without a sandbox, so its
//! external dependencies belong in the environment description. A successful
//! process exit does not establish kernel entry, boot success, or physical
//! hardware behavior. Guest-specific assertions belong to the experiment
//! checker.

use crate::{create_output, firmware, save_json, sparse, PlatformProcess};
use anyhow::{bail, ensure, Context, Result};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::{BTreeMap, BTreeSet};
use std::fs::{self, File};
use std::os::unix::process::ExitStatusExt;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

/// Files of the output directory that an input name must not select.
const RESERVED: [&str; 4] = ["inputs", "result.json", "stdout.log", "stderr.log"];
const RESULT: &str = "result.json";
const STDOUT: &str = "stdout.log";
const STDERR: &str = "stderr.log";
const INPUTS: &str = "inputs";
const ENV_ROOT: &str = "QCOM_VP_ENV_ROOT";
const RUN_ROOT: &str = "QCOM_VP_RUN_ROOT";
/// Host variables that earlier fixtures used for input and helper locations.
/// The snapshot is the only input root, so these inherited values are removed.
const REMOVED: [&str; 2] = ["QCOM_VP_PLATFORM_DIR", "QCOM_VP_PROBE_ROOT"];

/// States the limits of one execution record. Keep this text free of boot or
/// qualification claims.
pub const EVIDENCE_SCOPE: &str = "The platform script is trusted host code and runs \
without a sandbox. The launcher sets QCOM_VP_ENV_ROOT and QCOM_VP_RUN_ROOT and \
removes QCOM_VP_PROBE_ROOT and QCOM_VP_PLATFORM_DIR. Other host variables remain \
inherited, so the environment is not hermetic. This record describes one process \
execution. It does not establish kernel entry, boot success, physical hardware \
behavior, or any security property.";

pub struct Options {
    pub environment: PathBuf,
    pub out: PathBuf,
    pub system: Option<PathBuf>,
    pub timeout_seconds: u64,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Environment {
    schema: u32,
    name: String,
    description: String,
    entry: String,
    limitations: Vec<String>,
    security_policy: String,
    platform: String,
    files: Vec<FileInput>,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct FileInput {
    name: String,
    path: String,
    sha256: String,
}

impl Environment {
    fn validate(&self) -> Result<()> {
        ensure!(
            self.schema == 1,
            "Unsupported environment schema {}",
            self.schema
        );
        require_text(&self.name, "name")?;
        require_text(&self.description, "description")?;
        require_text(&self.entry, "entry")?;
        require_text(&self.security_policy, "security_policy")?;
        ensure!(
            !self.limitations.is_empty(),
            "The environment field limitations is empty"
        );
        for limitation in &self.limitations {
            require_text(limitation, "limitations")?;
        }
        check_files(&self.files, &self.platform)
    }
}

fn require_text(value: &str, field: &str) -> Result<()> {
    ensure!(
        !value.trim().is_empty(),
        "The environment field {field} is empty"
    );
    Ok(())
}

fn check_name(name: &str) -> Result<()> {
    ensure!(!name.is_empty(), "An input name is empty");
    ensure!(
        !name.starts_with('/'),
        "The input name {name} is an absolute path"
    );
    for component in name.split('/') {
        ensure!(
            !component.is_empty() && component != "." && component != "..",
            "The input name {name} is not a safe relative path"
        );
        ensure!(
            !component.contains('\0'),
            "The input name {name} contains a NUL byte"
        );
    }
    let first = name
        .split('/')
        .next()
        .context("The input name is empty")?;
    ensure!(
        !RESERVED.contains(&first),
        "The input name {name} collides with the reserved output name {first}"
    );
    Ok(())
}

fn check_files(files: &[FileInput], platform: &str) -> Result<()> {
    ensure!(
        !files.is_empty(),
        "The environment must declare at least one input file"
    );
    let mut names = BTreeSet::new();
    for file in files {
        check_name(&file.name)?;
        ensure!(
            names.insert(file.name.as_str()),
            "The input name {} occurs more than once",
            file.name
        );
        ensure!(
            !file.path.trim().is_empty(),
            "The input path for {} is empty",
            file.name
        );
        ensure!(
            file.sha256.len() == 64 && file.sha256.bytes().all(|b| b.is_ascii_hexdigit()),
            "Invalid SHA-256 for the input {}",
            file.name
        );
    }
    for name in &names {
        for (separator, _) in name.match_indices('/') {
            let ancestor = &name[..separator];
            ensure!(
                !names.contains(ancestor),
                "The input names {ancestor} and {name} overlap"
            );
        }
    }
    ensure!(
        files.iter().any(|file| file.name == platform),
        "The platform {platform} is not a declared input file"
    );
    Ok(())
}

struct Loaded {
    environment: Environment,
    base: PathBuf,
    source: PathBuf,
    sha256: String,
}

fn load_environment(path: &Path) -> Result<Loaded> {
    let source = path
        .canonicalize()
        .with_context(|| format!("Cannot resolve the environment file {}", path.display()))?;
    let bytes = fs::read(&source)
        .with_context(|| format!("Cannot read the environment file {}", source.display()))?;
    let text = std::str::from_utf8(&bytes).context("The environment file is not UTF-8")?;
    let environment: Environment = toml::from_str(text)
        .with_context(|| format!("Cannot parse the environment file {}", source.display()))?;
    environment.validate()?;
    let sha256 = format!("{:x}", Sha256::digest(&bytes));
    let base = source
        .parent()
        .context("The environment file has no parent directory")?
        .to_owned();
    Ok(Loaded {
        environment,
        base,
        source,
        sha256,
    })
}

fn resolve_source(base: &Path, file: &FileInput) -> Result<PathBuf> {
    let declared = Path::new(&file.path);
    let joined = if declared.is_absolute() {
        declared.to_owned()
    } else {
        base.join(declared)
    };
    let source = joined.canonicalize().with_context(|| {
        format!(
            "Cannot resolve the input file {} declared for {}",
            joined.display(),
            file.name
        )
    })?;
    ensure!(
        fs::metadata(&source)?.is_file(),
        "The input file {} is not a regular file",
        source.display()
    );
    Ok(source)
}

/// Copies every declared input under `out/inputs` and verifies both the source
/// and the copy against the declared identity before any guest starts.
fn snapshot_inputs(
    environment: &Environment,
    base: &Path,
    out: &Path,
) -> Result<Vec<SnapshotRecord>> {
    let root = out.join(INPUTS);
    fs::create_dir(&root).with_context(|| format!("Cannot create {}", root.display()))?;
    let mut records = Vec::new();
    for file in &environment.files {
        let source = resolve_source(base, file)?;
        let bytes = fs::metadata(&source)?.len();
        firmware::check_hash(&source, &file.sha256).with_context(|| {
            format!(
                "The declared identity of the input {} does not match {}",
                file.name,
                source.display()
            )
        })?;
        let target = root.join(&file.name);
        if let Some(parent) = target.parent() {
            fs::create_dir_all(parent)
                .with_context(|| format!("Cannot create {}", parent.display()))?;
        }
        // A reflink or sparse extent copy keeps large storage inputs cheap and
        // never allocates a whole-image buffer.
        sparse::copy(&source, &target)
            .with_context(|| format!("Cannot copy the input {} to {}", file.name, target.display()))?;
        firmware::check_hash(&target, &file.sha256).with_context(|| {
            format!("The snapshotted input {} does not match its identity", file.name)
        })?;
        records.push(SnapshotRecord {
            name: file.name.clone(),
            path: file.path.clone(),
            source: source.display().to_string(),
            sha256: file.sha256.clone(),
            bytes,
        });
    }
    Ok(records)
}

fn resolve_system(system: Option<&Path>) -> Result<PathBuf> {
    let path = match system {
        Some(path) => path.to_owned(),
        None => {
            let executable = std::env::current_exe()?.canonicalize()?;
            executable
                .parent()
                .context("The executable has no parent directory")?
                .join("qcom-vp-system")
        }
    };
    let source = path
        .canonicalize()
        .with_context(|| format!("Cannot locate the system executable {}", path.display()))?;
    ensure!(
        fs::metadata(&source)?.is_file(),
        "The system executable is not a regular file: {}",
        source.display()
    );
    Ok(source)
}

pub fn run(options: &Options) -> Result<()> {
    ensure!(
        options.timeout_seconds > 0,
        "The experiment timeout must be positive"
    );
    let loaded = load_environment(&options.environment)?;
    let out = create_output(&options.out)?;
    let started = Instant::now();
    let mut record = ResultRecord::new(&loaded, options.timeout_seconds);
    let mut failure = None;
    if let Err(error) = execute(&loaded, &out, options, &mut record) {
        record.error = Some(format!("{error:#}"));
        failure = Some(error);
    }
    if record.outcome.is_empty() {
        record.outcome = "setup_error";
    }
    record.elapsed_seconds = started.elapsed().as_secs_f64();
    let recorded = save_json(&out.join(RESULT), &record);
    match (failure, recorded) {
        (None, Ok(())) => {
            if record.exit_code == Some(0) && record.signal.is_none() {
                println!(
                    "Experiment {} exited with code 0. Outputs: {}",
                    record.environment.name,
                    out.display()
                );
                Ok(())
            } else if let Some(signal) = record.signal {
                bail!(
                    "The experiment process stopped on signal {signal}. Outputs: {}",
                    out.display()
                )
            } else {
                bail!(
                    "The experiment process exited with code {}. Outputs: {}",
                    record.exit_code.unwrap_or(-1),
                    out.display()
                )
            }
        }
        (Some(error), Ok(())) => Err(error),
        (None, Err(error)) => Err(error.context("Cannot record the experiment result")),
        (Some(error), Err(save)) => {
            Err(error.context(format!("Cannot record the experiment result: {save:#}")))
        }
    }
}

fn execute(
    loaded: &Loaded,
    out: &Path,
    options: &Options,
    record: &mut ResultRecord,
) -> Result<()> {
    let system = resolve_system(options.system.as_deref())?;
    record.system = Some(SystemRecord {
        path: system.display().to_string(),
        sha256: firmware::hash(&system)?,
    });
    record.files = snapshot_inputs(&loaded.environment, &loaded.base, out)?;
    let env_root = out.join(INPUTS);
    let platform = env_root.join(&loaded.environment.platform);
    record.invocation = Some(InvocationRecord {
        program: system.display().to_string(),
        arguments: vec!["-l".to_owned(), platform.display().to_string()],
        working_directory: out.display().to_string(),
        environment_root: env_root.display().to_string(),
        run_root: out.display().to_string(),
        removed_environment: REMOVED.to_vec(),
        inherited_environment: inherited_environment(),
    });
    let stdout = File::create(out.join(STDOUT)).context("Cannot create the stdout record")?;
    let stderr = File::create(out.join(STDERR)).context("Cannot create the stderr record")?;
    let deadline = Instant::now()
        .checked_add(Duration::from_secs(options.timeout_seconds))
        .context("The experiment timeout overflows")?;
    let mut command = Command::new(&system);
    command
        .arg("-l")
        .arg(&platform)
        .current_dir(out)
        .env(ENV_ROOT, &env_root)
        .env(RUN_ROOT, out)
        .stdin(Stdio::null())
        .stdout(stdout)
        .stderr(stderr);
    for name in REMOVED {
        command.env_remove(name);
    }
    let child = command.spawn();
    let mut child = match child {
        Ok(child) => PlatformProcess {
            child,
            status: None,
        },
        Err(error) => {
            record.outcome = "spawn_error";
            return Err(error).with_context(|| format!("Cannot execute {}", system.display()));
        }
    };
    loop {
        match child.poll() {
            Ok(Some(status)) => {
                record.exit_code = status.code();
                record.signal = status.signal();
                record.outcome = if record.signal.is_some() {
                    "signaled"
                } else {
                    "exited"
                };
                // A nonzero exit is a process result, not a launcher error. It
                // still makes the command status nonzero.
                return Ok(());
            }
            Ok(None) => {}
            Err(error) => {
                record.outcome = "wait_error";
                let _ = child.stop();
                return Err(error);
            }
        }
        if Instant::now() >= deadline {
            record.timed_out = true;
            record.outcome = "timeout";
            let stopped = child.stop().context("Cannot stop the experiment process");
            record.exit_code = child.status.and_then(|status| status.code());
            record.signal = child.status.and_then(|status| status.signal());
            stopped?;
            bail!(
                "The experiment exceeded the {} second timeout",
                options.timeout_seconds
            );
        }
        std::thread::sleep(Duration::from_millis(20));
    }
}

#[derive(Serialize)]
struct ResultRecord {
    schema: u32,
    /// Process outcome: exited, signaled, timeout, spawn_error, wait_error, or
    /// setup_error. No outcome states boot or qualification.
    outcome: &'static str,
    environment: EnvironmentSummary,
    files: Vec<SnapshotRecord>,
    system: Option<SystemRecord>,
    invocation: Option<InvocationRecord>,
    timeout_seconds: u64,
    elapsed_seconds: f64,
    timed_out: bool,
    exit_code: Option<i32>,
    signal: Option<i32>,
    stdout: &'static str,
    stderr: &'static str,
    /// Launcher failure: setup, spawn, wait, or timeout. A nonzero child exit
    /// appears in `exit_code` and never here.
    error: Option<String>,
    evidence_scope: &'static str,
}

#[derive(Serialize)]
struct EnvironmentSummary {
    schema: u32,
    source: String,
    manifest_sha256: String,
    name: String,
    description: String,
    entry: String,
    limitations: Vec<String>,
    security_policy: String,
    platform: String,
}

#[derive(Debug, Serialize)]
struct SnapshotRecord {
    name: String,
    /// Declared path as written in the environment file.
    path: String,
    /// Resolved source path that the snapshot copied.
    source: String,
    sha256: String,
    bytes: u64,
}

#[derive(Serialize)]
struct SystemRecord {
    path: String,
    sha256: String,
}

#[derive(Serialize)]
struct InvocationRecord {
    program: String,
    arguments: Vec<String>,
    working_directory: String,
    environment_root: String,
    run_root: String,
    /// Host variables that the launcher removed from the child.
    removed_environment: Vec<&'static str>,
    /// Inherited project variables that can still select host locations. The
    /// launcher records them and does not claim a hermetic environment.
    inherited_environment: BTreeMap<String, String>,
}

fn inherited_environment() -> BTreeMap<String, String> {
    std::env::vars_os()
        .filter_map(|(name, value)| {
            let name = name.into_string().ok()?;
            if REMOVED.contains(&name.as_str()) || name == ENV_ROOT || name == RUN_ROOT {
                return None;
            }
            if !name.starts_with("QCOM_VP_") && name != "PLATFORM_DIR" {
                return None;
            }
            Some((name, value.into_string().ok()?))
        })
        .collect()
}

impl ResultRecord {
    fn new(loaded: &Loaded, timeout_seconds: u64) -> Self {
        Self {
            schema: 1,
            outcome: "",
            environment: EnvironmentSummary {
                schema: loaded.environment.schema,
                source: loaded.source.display().to_string(),
                manifest_sha256: loaded.sha256.clone(),
                name: loaded.environment.name.clone(),
                description: loaded.environment.description.clone(),
                entry: loaded.environment.entry.clone(),
                limitations: loaded.environment.limitations.clone(),
                security_policy: loaded.environment.security_policy.clone(),
                platform: loaded.environment.platform.clone(),
            },
            files: Vec::new(),
            system: None,
            invocation: None,
            timeout_seconds,
            elapsed_seconds: 0.0,
            timed_out: false,
            exit_code: None,
            signal: None,
            stdout: STDOUT,
            stderr: STDERR,
            error: None,
            evidence_scope: EVIDENCE_SCOPE,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Seek, SeekFrom, Write};

    fn file(name: &str) -> FileInput {
        FileInput {
            name: name.to_owned(),
            path: name.to_owned(),
            sha256: "0".repeat(64),
        }
    }

    fn environment(files: Vec<FileInput>) -> Environment {
        Environment {
            schema: 1,
            name: "ipcc".to_owned(),
            description: "Does the APSS producer reach the TME consumer?".to_owned(),
            entry: "Constructed reset with an empty IPCC shared queue.".to_owned(),
            limitations: vec!["No physical comparison is included.".to_owned()],
            security_policy: "No authentication result is claimed.".to_owned(),
            platform: "platform.lua".to_owned(),
            files,
        }
    }

    fn platform_environment() -> Environment {
        environment(vec![file("platform.lua"), file("guests/producer.elf")])
    }

    fn write_file(directory: &Path, name: &str, bytes: &[u8]) -> String {
        let path = directory.join(name);
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent).unwrap();
        }
        fs::write(&path, bytes).unwrap();
        firmware::hash(&path).unwrap()
    }

    #[test]
    fn unsafe_input_names_are_rejected() {
        for name in [
            "",
            "/platform.lua",
            "../platform.lua",
            "guests/../../platform.lua",
            "guests//producer.elf",
            "./platform.lua",
            "guests/./producer.elf",
            "guests/",
            "inputs",
            "inputs/platform.lua",
            "result.json",
            "stdout.log",
            "stderr.log",
        ] {
            assert!(
                check_name(name).is_err(),
                "The input name {name} must not be accepted"
            );
        }
        assert!(check_name("guests/producer.elf").is_ok());
    }

    #[test]
    fn duplicate_and_overlapping_input_names_are_rejected() {
        let mut duplicate = platform_environment();
        duplicate.files.push(file("platform.lua"));
        assert!(duplicate.validate().is_err());

        let mut nested = platform_environment();
        nested.files.push(file("guests"));
        assert!(nested.validate().is_err());

        // A sibling between the two names must not hide the overlap.
        let mut sibling = platform_environment();
        sibling.files.push(file("guests!"));
        sibling.files.push(file("guests/producer.elf/state.bin"));
        assert!(sibling.validate().is_err());

        let mut reversed = environment(vec![file("guests/producer.elf"), file("guests")]);
        reversed.platform = "guests".to_owned();
        assert!(reversed.validate().is_err());
    }

    #[test]
    fn the_platform_must_be_a_declared_input() {
        let mut environment = platform_environment();
        environment.platform = "missing.lua".to_owned();
        let error = environment.validate().unwrap_err().to_string();
        assert!(error.contains("platform"), "{error}");

        let mut unsafe_platform = platform_environment();
        unsafe_platform.platform = "../platform.lua".to_owned();
        assert!(unsafe_platform.validate().is_err());
    }

    #[test]
    fn required_environment_fields_are_not_empty() {
        let mutations: Vec<fn(&mut Environment)> = vec![
            |environment| environment.name.clear(),
            |environment| environment.description.clear(),
            |environment| environment.entry.clear(),
            |environment| environment.security_policy.clear(),
            |environment| environment.limitations.clear(),
            |environment| environment.limitations = vec![" ".to_owned()],
            |environment| environment.files.clear(),
        ];
        for mutate in mutations {
            let mut environment = platform_environment();
            mutate(&mut environment);
            assert!(environment.validate().is_err());
        }
        assert!(platform_environment().validate().is_ok());
    }

    #[test]
    fn unknown_fields_and_schemas_are_rejected() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("environment.toml");
        let text = "schema = 1\nname = \"ipcc\"\ndescription = \"question\"\n\
                    entry = \"constructed reset\"\nlimitations = [\"virtual only\"]\n\
                    security_policy = \"none claimed\"\nplatform = \"platform.lua\"\n\
                    mmio = [{ address = 0 }]\n\
                    files = [{ name = \"platform.lua\", path = \"platform.lua\", \
                    sha256 = \"0000000000000000000000000000000000000000000000000000000000000000\" }]\n";
        fs::write(&path, text).unwrap();
        assert!(load_environment(&path).is_err());

        let mut environment = platform_environment();
        environment.schema = 2;
        assert!(environment.validate().is_err());
    }

    #[test]
    fn wrong_and_truncated_inputs_are_rejected_before_launch() {
        let dir = tempfile::tempdir().unwrap();
        let base = dir.path().join("environment");
        fs::create_dir(&base).unwrap();
        let platform_hash = write_file(&base, "platform.lua", b"return {}\n");
        let full = write_file(&base, "guests/producer.elf", b"guest payload");
        fs::write(base.join("guests/producer.elf"), b"guest").unwrap();

        let truncated_out = dir.path().join("truncated");
        fs::create_dir(&truncated_out).unwrap();
        let mut environment = platform_environment();
        environment.files[0].sha256.clone_from(&platform_hash);
        environment.files[1].sha256 = full;
        assert!(snapshot_inputs(&environment, &base, &truncated_out).is_err());
        assert!(!truncated_out
            .join(INPUTS)
            .join("guests/producer.elf")
            .exists());

        let wrong_out = dir.path().join("wrong");
        fs::create_dir(&wrong_out).unwrap();
        let mut wrong = platform_environment();
        wrong.files[0].sha256 = platform_hash;
        wrong.files[1].sha256 = "ab".repeat(32);
        assert!(snapshot_inputs(&wrong, &base, &wrong_out).is_err());
        assert!(!wrong_out.join(INPUTS).join("guests/producer.elf").exists());
    }

    #[test]
    fn absolute_input_paths_are_resolved_and_recorded() {
        let dir = tempfile::tempdir().unwrap();
        let base = dir.path().join("environment");
        fs::create_dir(&base).unwrap();
        let out = dir.path().join("out");
        fs::create_dir(&out).unwrap();
        let platform = write_file(&base, "platform.lua", b"return {}\n");
        let guest = dir.path().join("store/guest.bin");
        fs::create_dir_all(guest.parent().unwrap()).unwrap();
        fs::write(&guest, b"guest").unwrap();
        let environment = Environment {
            files: vec![
                FileInput {
                    name: "platform.lua".to_owned(),
                    path: "platform.lua".to_owned(),
                    sha256: platform,
                },
                FileInput {
                    name: "guest.bin".to_owned(),
                    path: guest.display().to_string(),
                    sha256: firmware::hash(&guest).unwrap(),
                },
            ],
            ..environment(Vec::new())
        };
        let records = snapshot_inputs(&environment, &base, &out).unwrap();
        assert_eq!(
            records[1].source,
            guest.canonicalize().unwrap().display().to_string()
        );
        assert_eq!(
            fs::read(out.join(INPUTS).join("guest.bin")).unwrap(),
            b"guest"
        );
    }

    #[test]
    fn symlinked_inputs_are_resolved_and_directories_are_rejected() {
        let dir = tempfile::tempdir().unwrap();
        let base = dir.path().join("environment");
        fs::create_dir(&base).unwrap();
        let out = dir.path().join("out");
        fs::create_dir(&out).unwrap();
        let platform = write_file(&base, "platform.lua", b"return {}\n");
        let outside = dir.path().join("store/guest.bin");
        fs::create_dir_all(outside.parent().unwrap()).unwrap();
        fs::write(&outside, b"guest").unwrap();
        std::os::unix::fs::symlink(&outside, base.join("guest.bin")).unwrap();
        let environment = Environment {
            files: vec![
                FileInput {
                    name: "platform.lua".to_owned(),
                    path: "platform.lua".to_owned(),
                    sha256: platform,
                },
                FileInput {
                    name: "guest.bin".to_owned(),
                    path: "guest.bin".to_owned(),
                    sha256: firmware::hash(&outside).unwrap(),
                },
            ],
            ..environment(Vec::new())
        };
        let records = snapshot_inputs(&environment, &base, &out).unwrap();
        // The record shows the resolved target, not the declared symlink.
        assert_eq!(
            records[1].source,
            outside.canonicalize().unwrap().display().to_string()
        );
        assert_eq!(
            fs::read(out.join(INPUTS).join("guest.bin")).unwrap(),
            b"guest"
        );

        fs::create_dir(base.join("guests")).unwrap();
        let directory_out = dir.path().join("directory");
        fs::create_dir(&directory_out).unwrap();
        let mut directory = platform_environment();
        directory.files[0].sha256.clone_from(&environment.files[0].sha256);
        directory.files[1].path = "guests".to_owned();
        let error = snapshot_inputs(&directory, &base, &directory_out)
            .unwrap_err()
            .to_string();
        assert!(error.contains("regular file") || error.contains("resolve"), "{error}");
    }

    #[test]
    fn run_storage_writes_preserve_the_input_image() {
        let dir = tempfile::tempdir().unwrap();
        let base = dir.path().join("environment");
        fs::create_dir(&base).unwrap();
        let out = dir.path().join("out");
        fs::create_dir(&out).unwrap();
        let platform = write_file(&base, "platform.lua", b"return {}\n");

        // A raw storage input with a hole keeps its complete content.
        let disk = base.join("storage/disk.raw");
        fs::create_dir_all(disk.parent().unwrap()).unwrap();
        let mut file = File::create(&disk).unwrap();
        file.set_len(8192).unwrap();
        file.seek(SeekFrom::Start(4096)).unwrap();
        file.write_all(b"partition").unwrap();
        drop(file);
        let disk_sha = firmware::hash(&disk).unwrap();

        let environment = Environment {
            files: vec![
                FileInput {
                    name: "platform.lua".to_owned(),
                    path: "platform.lua".to_owned(),
                    sha256: platform,
                },
                FileInput {
                    name: "storage/disk.raw".to_owned(),
                    path: "storage/disk.raw".to_owned(),
                    sha256: disk_sha.clone(),
                },
            ],
            ..environment(Vec::new())
        };
        environment.validate().unwrap();
        snapshot_inputs(&environment, &base, &out).unwrap();
        let run_disk = out.join(INPUTS).join("storage/disk.raw");
        let mut modified = fs::OpenOptions::new().write(true).open(&run_disk).unwrap();
        modified.seek(SeekFrom::Start(4096)).unwrap();
        modified.write_all(b"changed!!").unwrap();
        modified.sync_all().unwrap();
        assert_eq!(firmware::hash(&disk).unwrap(), disk_sha);
        assert_ne!(firmware::hash(&run_disk).unwrap(), disk_sha);
    }

    #[test]
    fn a_nonpositive_timeout_is_rejected_before_creating_the_output() {
        let dir = tempfile::tempdir().unwrap();
        let out = dir.path().join("out");
        let error = run(&Options {
            environment: dir.path().join("environment.toml"),
            out: out.clone(),
            system: None,
            timeout_seconds: 0,
        })
        .unwrap_err()
        .to_string();
        assert!(error.contains("timeout"), "{error}");
        assert!(!out.exists());
    }

}
