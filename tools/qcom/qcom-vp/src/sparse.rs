use anyhow::{ensure, Context, Result};
use sha2::{Digest, Sha256};
use std::fs::{File, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::os::fd::AsRawFd;
use std::path::Path;

fn extent(file: &File, offset: u64, operation: i32) -> Result<Option<u64>> {
    let offset = i64::try_from(offset).context("The file offset exceeds the host range")?;
    // The descriptor remains valid during this synchronous call.
    let result = unsafe { libc::lseek(file.as_raw_fd(), offset, operation) };
    if result >= 0 {
        return Ok(Some(result as u64));
    }
    let error = std::io::Error::last_os_error();
    if error.raw_os_error() == Some(libc::ENXIO) {
        return Ok(None);
    }
    Err(error).context("Cannot locate a sparse file extent")
}

pub fn copy(source: &Path, target: &Path) -> Result<()> {
    let mut source = File::open(source)?;
    let mut target = OpenOptions::new()
        .read(true)
        .write(true)
        .create_new(true)
        .open(target)?;
    let length = source.metadata()?.len();
    // FICLONE copies file data without sharing subsequent writes.
    let cloned = unsafe {
        libc::ioctl(
            target.as_raw_fd(),
            0x40049409 as libc::c_ulong,
            source.as_raw_fd(),
        )
    };
    if cloned == 0 {
        target.sync_all()?;
        return Ok(());
    }
    target.set_len(length)?;
    let mut offset = 0;
    let mut buffer = [0_u8; 131072];
    while offset < length {
        let Some(start) = extent(&source, offset, libc::SEEK_DATA)? else {
            break;
        };
        let end = extent(&source, start, libc::SEEK_HOLE)?
            .context("The data extent has no end")?
            .min(length);
        ensure!(start < end, "Invalid sparse extent");
        source.seek(SeekFrom::Start(start))?;
        target.seek(SeekFrom::Start(start))?;
        let mut remaining = end - start;
        while remaining > 0 {
            let n = remaining.min(buffer.len() as u64) as usize;
            source.read_exact(&mut buffer[..n])?;
            target.write_all(&buffer[..n])?;
            remaining -= n as u64;
        }
        offset = end;
    }
    target.sync_all()?;
    Ok(())
}

pub fn hash(path: &Path) -> Result<String> {
    const BLOCK: u64 = 65536;
    let mut file = File::open(path)?;
    let length = file.metadata()?.len();
    let mut digest = Sha256::new();
    digest.update(b"qcom-vp-sparse-sha256-v1\0");
    digest.update(length.to_le_bytes());
    let mut buffer = [0_u8; BLOCK as usize];
    let mut next = 0;
    while next < length {
        let Some(data) = extent(&file, next, libc::SEEK_DATA)? else {
            break;
        };
        let end = extent(&file, data, libc::SEEK_HOLE)?
            .context("The data extent has no end")?
            .min(length);
        ensure!(data < end, "Invalid sparse extent");
        let mut block = (data / BLOCK * BLOCK).max(next);
        while block < end {
            let count = (length - block).min(BLOCK) as usize;
            file.seek(SeekFrom::Start(block))?;
            file.read_exact(&mut buffer[..count])?;
            if buffer[..count].iter().any(|&byte| byte != 0) {
                digest.update(block.to_le_bytes());
                digest.update((count as u64).to_le_bytes());
                digest.update(&buffer[..count]);
            }
            block += count as u64;
        }
        next = block;
    }
    Ok(format!("{:x}", digest.finalize()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sparse_copy_preserves_bytes_and_isolates_writes() {
        let dir = tempfile::tempdir().unwrap();
        let source = dir.path().join("source");
        let target = dir.path().join("target");
        let mut file = File::create(&source).unwrap();
        file.set_len(1024 * 1024).unwrap();
        file.seek(SeekFrom::Start(65535)).unwrap();
        file.write_all(b"boundary").unwrap();
        drop(file);
        copy(&source, &target).unwrap();
        assert_eq!(
            std::fs::read(&source).unwrap(),
            std::fs::read(&target).unwrap()
        );
        assert_eq!(hash(&source).unwrap(), hash(&target).unwrap());
        let mut target_file = OpenOptions::new().write(true).open(&target).unwrap();
        target_file.seek(SeekFrom::Start(65535)).unwrap();
        target_file.write_all(b"changed!").unwrap();
        assert_ne!(hash(&source).unwrap(), hash(&target).unwrap());
        let original = std::fs::read(source).unwrap();
        assert_eq!(&original[65535..65543], b"boundary");
    }

    #[test]
    fn allocated_zeroes_and_holes_have_the_same_identity() {
        let dir = tempfile::tempdir().unwrap();
        let hole = dir.path().join("hole");
        let zeros = dir.path().join("zeros");
        File::create(&hole).unwrap().set_len(100000).unwrap();
        std::fs::write(&zeros, vec![0; 100000]).unwrap();
        assert_eq!(hash(&hole).unwrap(), hash(&zeros).unwrap());
    }
}
