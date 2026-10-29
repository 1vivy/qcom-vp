use anyhow::{ensure, Context, Result};
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet};
use std::fs::{self, File, OpenOptions};
use std::io::{self, Write};
use std::os::fd::AsRawFd;
use std::os::unix::fs::{MetadataExt, OpenOptionsExt};
use std::path::{Path, PathBuf};
use std::time::SystemTime;

const POLICY: &str = ".qcom-vp-retention.json";
const ACTIVE: &str = ".qcom-vp-active.lock";
const ROOT_LOCK: &str = ".qcom-vp-retention.lock";
const PRUNED: &str = "storage-pruned.json";
const LOGS: [&str; 5] = [
    "system.log",
    "qemu-apss.log",
    "qemu-tme.log",
    "qemu-cpucp.log",
    "qemu-dcp.log",
];

/// Hold this guard while an output is in use, even without a retention policy.
pub struct Guard {
    _file: File,
}

impl Guard {
    pub fn acquire(directory: &Path) -> Result<Self> {
        let directory = open_directory(directory)?;
        let file = lock_file(&directory, ACTIVE)?;
        flock(&file, libc::LOCK_SH)?;
        Ok(Self { _file: file })
    }
}

fn flock(file: &File, operation: i32) -> io::Result<()> {
    loop {
        // The file owns the descriptor throughout this synchronous call.
        if unsafe { libc::flock(file.as_raw_fd(), operation) } == 0 {
            return Ok(());
        }
        let error = io::Error::last_os_error();
        if error.kind() != io::ErrorKind::Interrupted {
            return Err(error);
        }
    }
}

fn open_directory(path: &Path) -> Result<File> {
    OpenOptions::new()
        .read(true)
        .custom_flags(libc::O_DIRECTORY | libc::O_NOFOLLOW)
        .open(path)
        .with_context(|| format!("Cannot open directory {}", path.display()))
}

// Descriptor paths keep operations inside the directory that we opened.
fn directory_path(directory: &File) -> PathBuf {
    PathBuf::from(format!("/proc/self/fd/{}", directory.as_raw_fd()))
}

fn open_regular(path: &Path) -> Result<File> {
    let file = OpenOptions::new()
        .read(true)
        .custom_flags(libc::O_NOFOLLOW | libc::O_NONBLOCK)
        .open(path)?;
    ensure!(
        file.metadata()?.is_file(),
        "Not a regular file: {}",
        path.display()
    );
    Ok(file)
}

fn lock_file(directory: &File, name: &str) -> Result<File> {
    let file = OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .truncate(false)
        .custom_flags(libc::O_NOFOLLOW | libc::O_NONBLOCK)
        .open(directory_path(directory).join(name))?;
    ensure!(file.metadata()?.is_file(), "The lock is not a regular file");
    ensure!(file.metadata()?.nlink() == 1, "The lock has multiple links");
    Ok(file)
}

#[derive(Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct Policy {
    limit_bytes: u64,
}

fn replace_json(path: &Path, value: &impl Serialize) -> Result<()> {
    let temporary = path.with_extension(format!("json.tmp-{}", std::process::id()));
    let mut file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&temporary)?;
    let result = (|| {
        serde_json::to_writer_pretty(&mut file, value)?;
        file.write_all(b"\n")?;
        file.sync_all()?;
        fs::rename(&temporary, path)?;
        Ok(())
    })();
    if result.is_err() {
        let _ = fs::remove_file(&temporary);
    }
    result
}

pub fn configure(root: &Path, limit_bytes: u64) -> Result<()> {
    ensure!(
        limit_bytes > 0,
        "The retention limit must exceed zero bytes"
    );
    let directory = open_directory(root)?;
    let lock = lock_file(&directory, ROOT_LOCK)?;
    flock(&lock, libc::LOCK_EX)?;
    let path = directory_path(&directory).join(POLICY);
    match fs::symlink_metadata(&path) {
        Ok(metadata) => ensure!(
            metadata.is_file(),
            "The retention policy is not a regular file"
        ),
        Err(error) if error.kind() == io::ErrorKind::NotFound => (),
        Err(error) => return Err(error.into()),
    }
    replace_json(&path, &Policy { limit_bytes })?;
    directory.sync_all()?;
    Ok(())
}

#[derive(Deserialize)]
struct Manifest {
    inputs: Inputs,
    disks: Vec<Disk>,
}

#[derive(Deserialize)]
struct Inputs {
    platform: String,
}

#[derive(Deserialize)]
struct Disk {
    filename: String,
}

struct Candidate {
    name: PathBuf,
    directory: File,
    _lock: Option<File>,
    platform: String,
    disks: Vec<String>,
    prepared: Option<SystemTime>,
    completed: Option<SystemTime>,
    runnable: bool,
}

fn disk_name(name: &str) -> bool {
    name.strip_prefix("lun")
        .and_then(|name| name.strip_suffix(".img"))
        .is_some_and(|number| {
            !number.is_empty() && number.bytes().all(|byte| byte.is_ascii_digit())
        })
}

fn json_time(path: &Path) -> Option<SystemTime> {
    let file = open_regular(path).ok()?;
    let modified = file.metadata().ok()?.modified().ok()?;
    serde_json::from_reader::<_, serde_json::Value>(file).ok()?;
    Some(modified)
}

fn inspect(directory: &File) -> Option<(Manifest, Option<SystemTime>, Option<SystemTime>, bool)> {
    let path = directory_path(directory);
    let file = open_regular(&path.join("prepared.json")).ok()?;
    let manifest_time = file.metadata().ok()?.modified().ok()?;
    let manifest: Manifest = serde_json::from_reader(file).ok()?;
    if manifest.inputs.platform.trim().is_empty()
        || manifest.disks.is_empty()
        || manifest.disks.iter().any(|disk| !disk_name(&disk.filename))
        || manifest
            .disks
            .iter()
            .map(|disk| &disk.filename)
            .collect::<BTreeSet<_>>()
            .len()
            != manifest.disks.len()
    {
        return None;
    }
    let mut runnable = match fs::symlink_metadata(path.join(PRUNED)) {
        Ok(_) => false,
        Err(error) if error.kind() == io::ErrorKind::NotFound => true,
        Err(_) => return None,
    };
    for disk in &manifest.disks {
        match fs::symlink_metadata(path.join(&disk.filename)) {
            Ok(metadata) if metadata.is_file() => (),
            Err(error) if error.kind() == io::ErrorKind::NotFound => runnable = false,
            _ => return None,
        }
    }
    let prepared = json_time(&path.join("inputs.json")).map(|_| manifest_time);
    let completed = json_time(&path.join("result.json"));
    if prepared.is_none() && completed.is_none() {
        return None;
    }
    Some((manifest, prepared, completed, runnable))
}

fn discover(root: &File) -> Result<Vec<Candidate>> {
    let mut candidates = Vec::new();
    for entry in fs::read_dir(directory_path(root))? {
        let entry = entry?;
        if !entry.file_type()?.is_dir() {
            continue;
        }
        let directory = open_directory(&entry.path())?;
        if inspect(&directory).is_none() {
            continue;
        }
        let lock = lock_file(&directory, ACTIVE)?;
        let lock = match flock(&lock, libc::LOCK_EX | libc::LOCK_NB) {
            Ok(()) => Some(lock),
            Err(error) if error.kind() == io::ErrorKind::WouldBlock => None,
            Err(error) => return Err(error.into()),
        };
        // Read again after locking because a writer can finish during discovery.
        let Some((manifest, prepared, completed, runnable)) = inspect(&directory) else {
            continue;
        };
        candidates.push(Candidate {
            name: PathBuf::from(entry.file_name()),
            directory,
            _lock: lock,
            platform: manifest.inputs.platform,
            disks: manifest
                .disks
                .into_iter()
                .map(|disk| disk.filename)
                .collect(),
            prepared,
            completed,
            runnable,
        });
    }
    Ok(candidates)
}

fn allocated(directory: &File) -> Result<u64> {
    let mut total = directory
        .metadata()?
        .blocks()
        .checked_mul(512)
        .context("Storage accounting overflow")?;
    for entry in fs::read_dir(directory_path(directory))? {
        let entry = entry?;
        let metadata = fs::symlink_metadata(entry.path())?;
        let bytes = if metadata.is_dir() {
            allocated(&open_directory(&entry.path())?)?
        } else {
            metadata
                .blocks()
                .checked_mul(512)
                .context("Storage accounting overflow")?
        };
        total = total
            .checked_add(bytes)
            .context("Storage accounting overflow")?;
    }
    Ok(total)
}

#[derive(Serialize)]
struct Pruned<'a> {
    runnable: bool,
    reason: &'static str,
    removed_filenames: Vec<&'a str>,
    pending_filenames: Vec<&'a str>,
}

fn retire(candidate: &Candidate) -> Result<()> {
    let path = directory_path(&candidate.directory);
    // Create the marker before deleting data, so an interrupted retirement is not runnable.
    let marker_path = path.join(PRUNED);
    let mut marker = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&marker_path)?;
    let mut record = Pruned {
        runnable: false,
        reason: "Retention removed storage images. This output is no longer runnable.",
        removed_filenames: Vec::new(),
        pending_filenames: candidate.disks.iter().map(String::as_str).collect(),
    };
    serde_json::to_writer_pretty(&mut marker, &record)?;
    marker.write_all(b"\n")?;
    marker.sync_all()?;
    candidate.directory.sync_all()?;
    for name in &candidate.disks {
        open_regular(&path.join(name))?;
        fs::remove_file(path.join(name))?;
        record.removed_filenames.push(name);
        record.pending_filenames.remove(0);
        replace_json(&marker_path, &record)?;
    }
    candidate.directory.sync_all()?;
    Ok(())
}

pub fn enforce(root: &Path) -> Result<()> {
    let directory = open_directory(root)?;
    let policy_path = directory_path(&directory).join(POLICY);
    match fs::symlink_metadata(&policy_path) {
        Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(()),
        Err(error) => return Err(error.into()),
        Ok(_) => (),
    }
    let lock = lock_file(&directory, ROOT_LOCK)?;
    flock(&lock, libc::LOCK_EX)?;
    let policy: Policy = serde_json::from_reader(open_regular(&policy_path)?)?;
    ensure!(
        policy.limit_bytes > 0,
        "The retention limit must exceed zero bytes"
    );
    let before = allocated(&directory)?;
    let mut retired = 0;
    let cleanup = (|| -> Result<()> {
        let mut candidates = discover(&directory)?;
        let mut newest = BTreeMap::new();
        for (index, candidate) in candidates.iter().enumerate() {
            if !candidate.runnable {
                continue;
            }
            for (kind, time) in [(0, candidate.prepared), (1, candidate.completed)] {
                if let Some(time) = time {
                    let key = (candidate.platform.as_str(), kind);
                    let value = (time, &candidate.name, index);
                    if newest.get(&key).is_none_or(|current| value > *current) {
                        newest.insert(key, value);
                    }
                }
            }
        }
        let protected: BTreeSet<_> = newest
            .values()
            .map(|value| candidates[value.2].name.clone())
            .collect();
        drop(newest);
        candidates.sort_by_key(|candidate| {
            (
                candidate.completed.or(candidate.prepared),
                candidate.name.clone(),
            )
        });
        for candidate in &candidates {
            if candidate._lock.is_none() {
                continue;
            }
            let path = directory_path(&candidate.directory);
            for entry in fs::read_dir(&path)? {
                let entry = entry?;
                let filename = entry.file_name();
                let Some(name) = filename.to_str() else {
                    continue;
                };
                let cpu_thread = name
                    .strip_prefix("qemu-cpucp-")
                    .and_then(|suffix| suffix.strip_suffix(".log"))
                    .is_some_and(|id| {
                        !id.is_empty() && id.bytes().all(|byte| byte.is_ascii_digit())
                    });
                if !LOGS.contains(&name) && !cpu_thread {
                    continue;
                }
                let log = path.join(&filename);
                if let Ok(file) = open_regular(&log) {
                    if file.metadata()?.len() > 1024 * 1024 {
                        crate::log_archive::compress(
                            &candidate.directory,
                            std::ffi::OsStr::new(name),
                        )?;
                    }
                }
            }
            if candidate.runnable && !protected.contains(&candidate.name) {
                retire(candidate).with_context(|| {
                    format!("Cannot retire {}", root.join(&candidate.name).display())
                })?;
                retired += 1;
            }
        }
        Ok(())
    })();
    let after = allocated(&directory)?;
    println!(
        "Retention: {} -> {} allocated bytes, limit {}, retired {} outputs",
        before, after, policy.limit_bytes, retired
    );
    cleanup?;
    ensure!(
        after <= policy.limit_bytes,
        "Retention cannot meet the {} byte limit: {} allocated bytes remain in protected, active, incomplete, or unknown data under {}",
        policy.limit_bytes,
        after,
        root.display()
    );
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::os::unix::fs::symlink;
    use std::time::Duration;
    use tempfile::TempDir;

    fn output(root: &Path, name: &str, platform: &str, run: bool, time: u64) -> PathBuf {
        let path = root.join(name);
        fs::create_dir(&path).unwrap();
        fs::write(path.join("lun0.img"), vec![0x5a; 256 * 1024]).unwrap();
        fs::write(
            path.join("prepared.json"),
            serde_json::to_vec(&serde_json::json!({
                "inputs": {"platform": platform}, "disks": [{"filename": "lun0.img"}]
            }))
            .unwrap(),
        )
        .unwrap();
        let stamp = if run { "result.json" } else { "inputs.json" };
        fs::write(path.join(stamp), b"{}").unwrap();
        let modified = SystemTime::UNIX_EPOCH + Duration::from_secs(time);
        File::open(path.join("prepared.json"))
            .unwrap()
            .set_modified(modified)
            .unwrap();
        File::open(path.join(stamp))
            .unwrap()
            .set_modified(modified)
            .unwrap();
        fs::write(path.join("uart.log"), b"UART evidence\n").unwrap();
        path
    }

    fn total(root: &Path) -> u64 {
        allocated(&open_directory(root).unwrap()).unwrap()
    }

    #[test]
    fn preserves_latest_complete_sets_for_each_platform() {
        let root = TempDir::new().unwrap();
        let mut old = Vec::new();
        let mut current = Vec::new();
        for platform in ["sm8850", "sm8845"] {
            for run in [false, true] {
                let prefix = format!("{platform}-{run}");
                old.push(output(
                    root.path(),
                    &format!("{prefix}-old"),
                    platform,
                    run,
                    10,
                ));
                current.push(output(
                    root.path(),
                    &format!("{prefix}-new"),
                    platform,
                    run,
                    20,
                ));
            }
        }
        configure(root.path(), 1).unwrap();
        assert!(enforce(root.path()).is_err());
        for path in current {
            assert!(path.join("lun0.img").is_file());
            assert!(!path.join(PRUNED).exists());
        }
        for path in old {
            assert!(!path.join("lun0.img").exists());
            let marker: serde_json::Value =
                serde_json::from_reader(File::open(path.join(PRUNED)).unwrap()).unwrap();
            assert_eq!(marker["runnable"], false);
            assert_eq!(marker["removed_filenames"], serde_json::json!(["lun0.img"]));
            assert_eq!(fs::read(path.join("uart.log")).unwrap(), b"UART evidence\n");
            assert!(path.join("prepared.json").is_file());
        }
    }

    #[test]
    fn does_not_mutate_unknown_incomplete_or_symlinked_outputs() {
        let root = TempDir::new().unwrap();
        let external = TempDir::new().unwrap();
        let target = output(external.path(), "external", "sm8850", false, 1);
        symlink(&target, root.path().join("linked-output")).unwrap();
        let unsafe_output = output(root.path(), "unsafe", "sm8850", false, 1);
        fs::write(
            unsafe_output.join("prepared.json"),
            br#"{"inputs":{"platform":"sm8850"},"disks":[{"filename":"../lun0.img"}]}"#,
        )
        .unwrap();
        let incomplete = output(root.path(), "incomplete", "sm8850", false, 1);
        fs::remove_file(incomplete.join("inputs.json")).unwrap();
        let symlinked = output(root.path(), "symlinked-disk", "sm8850", false, 1);
        fs::remove_file(symlinked.join("lun0.img")).unwrap();
        symlink(target.join("lun0.img"), symlinked.join("lun0.img")).unwrap();
        let unknown = root.path().join("unknown.bin");
        fs::write(&unknown, b"private input").unwrap();
        configure(root.path(), 1).unwrap();
        assert!(enforce(root.path()).is_err());
        for path in [&target, &unsafe_output, &incomplete, &symlinked] {
            assert!(path.join("lun0.img").exists());
            assert!(!path.join(PRUNED).exists());
        }
        assert_eq!(fs::read(unknown).unwrap(), b"private input");
        assert!(total(root.path()) < total(external.path()) * 4);
    }

    #[test]
    fn active_guards_prevent_retirement_until_released() {
        let root = TempDir::new().unwrap();
        let old = output(root.path(), "old", "sm8850", false, 1);
        output(root.path(), "new", "sm8850", false, 2);
        let guard = Guard::acquire(&old).unwrap();
        configure(root.path(), 1).unwrap();
        assert!(enforce(root.path()).is_err());
        assert!(old.join("lun0.img").exists());
        drop(guard);
        assert!(enforce(root.path()).is_err());
        assert!(!old.join("lun0.img").exists());
    }

    #[test]
    fn absent_policy_does_nothing_and_protected_excess_returns_error() {
        let root = TempDir::new().unwrap();
        let old = output(root.path(), "old", "sm8850", false, 1);
        let current = output(root.path(), "current", "sm8850", false, 2);
        enforce(root.path()).unwrap();
        assert!(old.join("lun0.img").exists());
        assert!(!root.path().join(ROOT_LOCK).exists());
        configure(root.path(), 1).unwrap();
        assert!(enforce(root.path()).is_err());
        assert!(current.join("lun0.img").exists());
        assert!(total(root.path()) > 1);
    }

    #[test]
    fn missing_disks_cannot_replace_the_latest_runnable_output() {
        let root = TempDir::new().unwrap();
        let complete = output(root.path(), "complete", "sm8850", false, 1);
        let missing = output(root.path(), "missing", "sm8850", false, 2);
        fs::remove_file(missing.join("lun0.img")).unwrap();
        configure(root.path(), 1).unwrap();
        assert!(enforce(root.path()).is_err());
        assert!(complete.join("lun0.img").exists());
        assert!(!complete.join(PRUNED).exists());
        assert!(!missing.join(PRUNED).exists());
    }

    #[test]
    fn retires_obsolete_images_even_below_the_limit() {
        let root = TempDir::new().unwrap();
        let old = output(root.path(), "old", "sm8850", false, 1);
        output(root.path(), "new", "sm8850", false, 2);
        configure(root.path(), total(root.path()) + 1024 * 1024).unwrap();
        enforce(root.path()).unwrap();
        assert!(!old.join("lun0.img").exists());
    }

    #[test]
    fn pruned_logs_remain_eligible_but_uart_and_console_remain_readable() {
        match std::process::Command::new("zstd").arg("--version").output() {
            Ok(result) => assert!(result.status.success()),
            Err(error) if error.kind() == io::ErrorKind::NotFound => return,
            Err(error) => panic!("Cannot execute zstd: {error}"),
        }
        let root = TempDir::new().unwrap();
        let old = output(root.path(), "old", "sm8850", false, 1);
        let current = output(root.path(), "current", "sm8850", false, 2);
        fs::remove_file(old.join("lun0.img")).unwrap();
        fs::write(old.join(PRUNED), b"{\"runnable\":false}").unwrap();
        let contents = vec![b'Z'; 2 * 1024 * 1024];
        fs::write(old.join("system.log"), &contents).unwrap();
        fs::write(old.join("qemu-cpucp-123.log"), &contents).unwrap();
        fs::write(old.join("qemu-cpucp-other.log"), &contents).unwrap();
        fs::write(old.join("uart.log"), &contents).unwrap();
        fs::write(old.join("console.log"), &contents).unwrap();
        fs::write(old.join("other.log"), &contents).unwrap();
        configure(root.path(), 1).unwrap();
        assert!(enforce(root.path()).is_err());
        for name in ["system.log", "qemu-cpucp-123.log"] {
            assert!(!old.join(name).exists());
            let archive = old.join(format!("{name}.zst"));
            let restored = std::process::Command::new("zstd")
                .args(["-q", "-d", "-c", "--"])
                .arg(archive)
                .output()
                .unwrap();
            assert!(restored.status.success());
            assert_eq!(restored.stdout, contents);
        }
        for name in [
            "uart.log",
            "console.log",
            "other.log",
            "qemu-cpucp-other.log",
        ] {
            assert_eq!(fs::read(old.join(name)).unwrap(), contents);
        }
        assert!(current.join("lun0.img").exists());
    }

    #[test]
    fn policy_symlinks_are_not_followed_or_replaced() {
        let root = TempDir::new().unwrap();
        let external = TempDir::new().unwrap();
        let target = external.path().join("policy.json");
        fs::write(&target, b"{\"limit_bytes\":1}").unwrap();
        symlink(&target, root.path().join(POLICY)).unwrap();
        assert!(configure(root.path(), 1024).is_err());
        assert!(enforce(root.path()).is_err());
        assert_eq!(fs::read(target).unwrap(), b"{\"limit_bytes\":1}");
    }
}
