use anyhow::{ensure, Context, Result};
use sha2::{Digest, Sha256};
use std::ffi::{CString, OsStr};
use std::fs::{File, Metadata};
use std::io::{Read, Seek, SeekFrom};
use std::os::fd::{AsRawFd, FromRawFd};
use std::os::unix::ffi::OsStrExt;
use std::os::unix::fs::MetadataExt;
use std::path::{Component, Path};
use std::process::{Command, Stdio};

fn name(value: &OsStr) -> Result<CString> {
    CString::new(value.as_bytes()).context("The log path contains a null byte")
}

fn open_at(directory: &File, name: &CString, flags: i32) -> Result<File> {
    // The directory and name remain valid during this call.
    let fd = unsafe {
        libc::openat(
            directory.as_raw_fd(),
            name.as_ptr(),
            flags | libc::O_CLOEXEC | libc::O_NOFOLLOW | libc::O_NONBLOCK,
            0o600 as libc::mode_t,
        )
    };
    if fd < 0 {
        return Err(std::io::Error::last_os_error()).context("Cannot open the log path");
    }
    // openat returned a new descriptor that this File owns.
    Ok(unsafe { File::from_raw_fd(fd) })
}

fn regular(file: &File) -> Result<Metadata> {
    let metadata = file.metadata()?;
    ensure!(metadata.is_file(), "The log path is not a regular file");
    Ok(metadata)
}

fn same_file(first: &Metadata, second: &Metadata) -> bool {
    first.dev() == second.dev() && first.ino() == second.ino()
}

fn unchanged(first: &Metadata, second: &Metadata) -> bool {
    same_file(first, second)
        && first.len() == second.len()
        && first.mtime() == second.mtime()
        && first.mtime_nsec() == second.mtime_nsec()
        && first.ctime() == second.ctime()
        && first.ctime_nsec() == second.ctime_nsec()
}

fn fingerprint(reader: &mut impl Read) -> Result<([u8; 32], u64)> {
    let mut digest = Sha256::new();
    let mut length = 0_u64;
    let mut buffer = [0_u8; 65536];
    loop {
        let count = reader.read(&mut buffer)?;
        if count == 0 {
            break;
        }
        digest.update(&buffer[..count]);
        length = length
            .checked_add(count as u64)
            .context("The log length exceeds the host range")?;
    }
    Ok((digest.finalize().into(), length))
}

fn compress_stream(source: &File, archive: &File) -> Result<()> {
    let status = Command::new("zstd")
        .args(["-q", "-c", "--"])
        .stdin(Stdio::from(source.try_clone()?))
        .stdout(Stdio::from(archive.try_clone()?))
        .stderr(Stdio::null())
        .status()
        .context("Cannot execute zstd to compress the log")?;
    ensure!(
        status.success(),
        "zstd failed to compress the log: {status}"
    );
    Ok(())
}

fn restored_fingerprint(archive: &mut File) -> Result<([u8; 32], u64)> {
    archive.seek(SeekFrom::Start(0))?;
    let mut child = Command::new("zstd")
        .args(["-q", "-d", "-c", "--"])
        .stdin(Stdio::from(archive.try_clone()?))
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .spawn()
        .context("Cannot execute zstd to restore the log")?;
    let result = fingerprint(child.stdout.as_mut().expect("The restore stream is piped"));
    if result.is_err() {
        // Stop the producer before waiting if the consumer cannot read its stream.
        let _ = child.kill();
    }
    let status = child
        .wait()
        .context("Cannot wait for zstd to restore the log")?;
    ensure!(status.success(), "zstd failed to restore the log: {status}");
    result
}

fn remove_at(directory: &File, name: &CString) -> Result<()> {
    // The directory and name remain valid during this call.
    let result = unsafe { libc::unlinkat(directory.as_raw_fd(), name.as_ptr(), 0) };
    if result != 0 {
        return Err(std::io::Error::last_os_error()).context("Cannot remove the log file");
    }
    Ok(())
}

/// The caller must hold the output directory guard and close all log writers.
/// The original remains until zstd restores the same SHA-256 and byte length.
pub fn compress(directory: &File, filename: &OsStr) -> Result<u64> {
    let path = Path::new(filename);
    ensure!(
        path.components().count() == 1
            && matches!(path.components().next(), Some(Component::Normal(_))),
        "The log filename is not local"
    );
    let source_name = name(filename)?;
    let mut archive_name = filename.to_os_string();
    archive_name.push(".zst");
    let archive_name = name(&archive_name)?;
    let mut source = open_at(directory, &source_name, libc::O_RDONLY)?;
    let source_before = regular(&source)?;
    let expected = fingerprint(&mut source)?;
    ensure!(
        expected.1 == source_before.len(),
        "The log changed during hashing"
    );
    ensure!(
        unchanged(&source_before, &regular(&source)?),
        "The log changed during hashing"
    );

    let (mut archive, created) = match open_at(
        directory,
        &archive_name,
        libc::O_RDWR | libc::O_CREAT | libc::O_EXCL,
    ) {
        Ok(file) => (file, true),
        Err(error)
            if error
                .downcast_ref::<std::io::Error>()
                .is_some_and(|error| error.kind() == std::io::ErrorKind::AlreadyExists) =>
        {
            (open_at(directory, &archive_name, libc::O_RDONLY)?, false)
        }
        Err(error) => return Err(error),
    };
    let archive_identity = match archive.metadata() {
        Ok(metadata) => metadata,
        Err(error) => {
            if created {
                remove_at(directory, &archive_name).context("Cannot remove the partial archive")?;
            }
            return Err(error).context("Cannot inspect the archive");
        }
    };
    let result = (|| {
        regular(&archive)?;
        if created {
            source.seek(SeekFrom::Start(0))?;
            compress_stream(&source, &archive)?;
            archive.sync_all()?;
        }
        let archive_before = regular(&archive)?;
        let restored = restored_fingerprint(&mut archive)?;
        ensure!(
            restored == expected,
            "The restored archive does not match the log"
        );
        source.seek(SeekFrom::Start(0))?;
        ensure!(
            fingerprint(&mut source)? == expected,
            "The log changed during compression"
        );
        let source_after = regular(&source)?;
        ensure!(
            unchanged(&source_before, &source_after),
            "The log changed during compression"
        );
        let archive_after = regular(&archive)?;
        ensure!(
            unchanged(&archive_before, &archive_after),
            "The archive changed during verification"
        );
        let source_path = open_at(directory, &source_name, libc::O_RDONLY)?;
        ensure!(
            unchanged(&source_before, &regular(&source_path)?),
            "The log path changed during compression"
        );
        let archive_path = open_at(directory, &archive_name, libc::O_RDONLY)?;
        ensure!(
            unchanged(&archive_before, &regular(&archive_path)?),
            "The archive path changed during verification"
        );
        let source_bytes = source_after.blocks().saturating_mul(512);
        let archive_bytes = if created {
            archive_after.blocks().saturating_mul(512)
        } else {
            0
        };
        directory.sync_all()?;
        remove_at(directory, &source_name)?;
        Ok(source_bytes.saturating_sub(archive_bytes))
    })();
    if result.is_err() && created {
        let current = open_at(directory, &archive_name, libc::O_RDONLY)?;
        ensure!(
            same_file(&archive_identity, &current.metadata()?),
            "The partial archive path changed"
        );
        remove_at(directory, &archive_name).context("Cannot remove the partial archive")?;
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use std::os::unix::fs::symlink;

    fn compress(path: &Path) -> Result<u64> {
        let directory = File::open(path.parent().context("The log parent is absent")?)?;
        super::compress(
            &directory,
            path.file_name().context("The log filename is absent")?,
        )
    }

    fn zstd_available() -> bool {
        match Command::new("zstd")
            .arg("--version")
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
        {
            Ok(status) => {
                assert!(status.success(), "zstd --version failed: {status}");
                true
            }
            Err(error) => {
                eprintln!("Skipping archive test because zstd cannot execute: {error}");
                false
            }
        }
    }

    fn log(path: &Path) -> Vec<u8> {
        let bytes = b"closed UART-independent system log\n".repeat(8192);
        std::fs::write(path, &bytes).unwrap();
        bytes
    }

    #[test]
    fn roundtrip_preserves_bytes_and_reports_allocated_reclamation() {
        if !zstd_available() {
            return;
        }
        let dir = tempfile::tempdir().unwrap();
        let source = dir.path().join("system.log");
        let expected = log(&source);
        let before = std::fs::metadata(&source).unwrap().blocks() * 512;
        let reclaimed = compress(&source).unwrap();
        assert!(!source.exists());
        let archive = dir.path().join("system.log.zst");
        assert_eq!(
            reclaimed,
            before.saturating_sub(std::fs::metadata(&archive).unwrap().blocks() * 512)
        );
        let mut file = File::open(&archive).unwrap();
        assert_eq!(
            restored_fingerprint(&mut file).unwrap(),
            fingerprint(&mut expected.as_slice()).unwrap()
        );
        std::fs::write(&source, &expected).unwrap();
        let archive_before = std::fs::read(&archive).unwrap();
        let source_blocks = std::fs::metadata(&source).unwrap().blocks();
        assert_eq!(compress(&source).unwrap(), source_blocks * 512);
        assert!(!source.exists());
        assert_eq!(std::fs::read(&archive).unwrap(), archive_before);
    }

    #[test]
    fn conflicting_archive_preserves_both_files() {
        if !zstd_available() {
            return;
        }
        let dir = tempfile::tempdir().unwrap();
        let source = dir.path().join("system.log");
        let expected = log(&source);
        let archive = dir.path().join("system.log.zst");
        let mut other = tempfile::tempfile().unwrap();
        other.write_all(b"a different valid log\n").unwrap();
        other.seek(SeekFrom::Start(0)).unwrap();
        let output = File::create(&archive).unwrap();
        compress_stream(&other, &output).unwrap();
        let archive_before = std::fs::read(&archive).unwrap();
        assert!(compress(&source).is_err());
        assert_eq!(std::fs::read(&source).unwrap(), expected);
        assert_eq!(std::fs::read(&archive).unwrap(), archive_before);
    }

    #[test]
    fn damaged_archive_preserves_original_and_existing_output() {
        if !zstd_available() {
            return;
        }
        let dir = tempfile::tempdir().unwrap();
        let source = dir.path().join("system.log");
        let expected = log(&source);
        let archive = dir.path().join("system.log.zst");
        let damaged = b"not a zstd frame";
        std::fs::write(&archive, damaged).unwrap();
        assert!(compress(&source).is_err());
        assert_eq!(std::fs::read(&source).unwrap(), expected);
        assert_eq!(std::fs::read(&archive).unwrap(), damaged);
    }

    #[test]
    fn symlinks_and_nonregular_files_are_not_targets() {
        let dir = tempfile::tempdir().unwrap();
        let source = dir.path().join("source");
        let expected = log(&source);
        let link = dir.path().join("system.log");
        symlink(&source, &link).unwrap();
        assert!(compress(&link).is_err());
        assert!(compress(dir.path()).is_err());
        std::fs::remove_file(&link).unwrap();
        std::fs::write(&link, &expected).unwrap();
        let archive = dir.path().join("system.log.zst");
        symlink(&source, &archive).unwrap();
        assert!(compress(&link).is_err());
        assert_eq!(std::fs::read(&link).unwrap(), expected);
        assert_eq!(std::fs::read(&source).unwrap(), expected);
        assert!(std::fs::symlink_metadata(&archive)
            .unwrap()
            .file_type()
            .is_symlink());
    }
}
