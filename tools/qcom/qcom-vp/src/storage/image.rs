use super::{add, mul, range, Sink};
use anyhow::{bail, ensure, Result};
use std::io::Read;

const MAGIC: u32 = 0xed26ff3a;
const BUFFER: usize = 1024 * 1024;

pub(super) fn u16_at(bytes: &[u8], offset: usize) -> u16 {
    u16::from_le_bytes(
        bytes[offset..offset + 2]
            .try_into()
            .expect("fixed-size field"),
    )
}

pub(super) fn u32_at(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(
        bytes[offset..offset + 4]
            .try_into()
            .expect("fixed-size field"),
    )
}

pub(super) fn u64_at(bytes: &[u8], offset: usize) -> u64 {
    u64::from_le_bytes(
        bytes[offset..offset + 8]
            .try_into()
            .expect("fixed-size field"),
    )
}

struct Header {
    file_size: u16,
    chunk_size: u16,
    block_size: u64,
    blocks: u64,
    chunks: u32,
    checksum: u32,
}

fn header(reader: &mut dyn Read) -> Result<Header> {
    let mut bytes = [0; 28];
    reader.read_exact(&mut bytes)?;
    ensure!(u32_at(&bytes, 0) == MAGIC, "Invalid Android sparse magic");
    ensure!(
        u16_at(&bytes, 4) == 1,
        "Unsupported Android sparse major version"
    );
    let result = Header {
        file_size: u16_at(&bytes, 8),
        chunk_size: u16_at(&bytes, 10),
        block_size: u64::from(u32_at(&bytes, 12)),
        blocks: u64::from(u32_at(&bytes, 16)),
        chunks: u32_at(&bytes, 20),
        checksum: u32_at(&bytes, 24),
    };
    ensure!(
        result.file_size >= 28 && result.chunk_size >= 12,
        "Sparse header is too short"
    );
    ensure!(
        result.block_size > 0 && result.block_size % 4 == 0,
        "Invalid sparse block size"
    );
    Ok(result)
}

fn discard(reader: &mut dyn Read, size: u64) -> Result<()> {
    ensure!(
        std::io::copy(&mut reader.take(size), &mut std::io::sink())? == size,
        "Truncated sparse extension"
    );
    Ok(())
}

pub(super) fn expanded_size(reader: &mut dyn Read) -> Result<u64> {
    let header = header(reader)?;
    mul(header.blocks, header.block_size)
}

pub(super) fn raw(
    reader: &mut dyn Read,
    bytes: u64,
    start: u64,
    sink: &mut Sink<'_>,
) -> Result<()> {
    let mut buffer = vec![0; BUFFER];
    let mut position = 0;
    while position < bytes {
        let count = (bytes - position).min(BUFFER as u64) as usize;
        reader.read_exact(&mut buffer[..count])?;
        sink.emit(add(start, position)?, &buffer[..count])?;
        position += count as u64;
    }
    Ok(())
}

pub(super) fn expand(
    reader: &mut dyn Read,
    start: u64,
    capacity: u64,
    sink: &mut Sink<'_>,
) -> Result<u64> {
    let header = header(reader)?;
    let length = mul(header.blocks, header.block_size)?;
    range(0, length, capacity)?;
    range(start, length, sink.limit)?;
    discard(reader, u64::from(header.file_size - 28))?;
    let mut blocks = 0;
    let mut checksum = crc32fast::Hasher::new();
    let mut buffer = vec![0; BUFFER];
    for _ in 0..header.chunks {
        let mut chunk = [0; 12];
        reader.read_exact(&mut chunk)?;
        discard(reader, u64::from(header.chunk_size - 12))?;
        let kind = u16_at(&chunk, 0);
        let count = u64::from(u32_at(&chunk, 4));
        let total = u64::from(u32_at(&chunk, 8));
        ensure!(
            total >= u64::from(header.chunk_size),
            "Sparse chunk size underflows"
        );
        let data_size = total - u64::from(header.chunk_size);
        let bytes = mul(count, header.block_size)?;
        ensure!(
            add(blocks, count)? <= header.blocks,
            "Sparse chunks exceed declared block count"
        );
        match kind {
            0xcac1 => ensure!(data_size == bytes, "Sparse RAW chunk size differs"),
            0xcac2 => {
                ensure!(data_size == 4, "Sparse FILL chunk size differs");
                let mut pattern = [0; 4];
                reader.read_exact(&mut pattern)?;
                for word in buffer.chunks_exact_mut(4) {
                    word.copy_from_slice(&pattern);
                }
            }
            0xcac3 => {
                ensure!(data_size == 0, "Sparse DONT_CARE chunk contains bytes");
                checksum_zeros(&mut checksum, bytes);
                blocks += count;
                continue;
            }
            0xcac4 => {
                ensure!(count == 0 && data_size == 4, "Invalid sparse CRC32 chunk");
                let mut expected = [0; 4];
                reader.read_exact(&mut expected)?;
                ensure!(
                    checksum.clone().finalize() == u32::from_le_bytes(expected),
                    "Sparse chunk CRC32 differs"
                );
                continue;
            }
            _ => bail!("Unsupported Android sparse chunk: {kind:#x}"),
        }
        let mut position = 0;
        while position < bytes {
            let n = (bytes - position).min(BUFFER as u64) as usize;
            if kind == 0xcac1 {
                reader.read_exact(&mut buffer[..n])?;
            }
            checksum.update(&buffer[..n]);
            sink.emit(
                add(add(start, mul(blocks, header.block_size)?)?, position)?,
                &buffer[..n],
            )?;
            position += n as u64;
        }
        blocks += count;
    }
    ensure!(blocks == header.blocks, "Sparse image block count differs");
    ensure!(
        header.checksum == 0 || header.checksum == checksum.finalize(),
        "Sparse image CRC32 differs"
    );
    let mut extra = [0];
    ensure!(
        reader.read(&mut extra)? == 0,
        "Sparse image has trailing bytes"
    );
    Ok(length)
}

fn checksum_zeros(checksum: &mut crc32fast::Hasher, mut bytes: u64) {
    let mut power = crc32fast::Hasher::new();
    power.update(&[0]);
    while bytes > 0 {
        if bytes & 1 != 0 {
            checksum.combine(&power);
        }
        bytes >>= 1;
        if bytes > 0 {
            let copy = power.clone();
            power.combine(&copy);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Cursor, Seek, SeekFrom};

    fn fixture() -> Vec<u8> {
        let mut bytes = Vec::new();
        bytes.extend(MAGIC.to_le_bytes());
        for v in [1u16, 0, 28, 12] {
            bytes.extend(v.to_le_bytes());
        }
        for v in [4u32, 3, 3, 0] {
            bytes.extend(v.to_le_bytes());
        }
        for (kind, value) in [
            (0xcac1u16, Some([1, 2, 3, 4])),
            (0xcac2, Some([5, 6, 7, 8])),
            (0xcac3, None),
        ] {
            bytes.extend(kind.to_le_bytes());
            bytes.extend(0u16.to_le_bytes());
            bytes.extend(1u32.to_le_bytes());
            bytes.extend((if value.is_some() { 16u32 } else { 12 }).to_le_bytes());
            if let Some(value) = value {
                bytes.extend(value);
            }
        }
        bytes
    }

    #[test]
    fn expands_all_data_chunks_and_keeps_holes() {
        let mut file = tempfile::tempfile().unwrap();
        file.set_len(12).unwrap();
        let mut sink = Sink::new(&mut file, 12, false, &[]);
        assert_eq!(
            expand(&mut Cursor::new(fixture()), 0, 12, &mut sink).unwrap(),
            12
        );
        file.seek(SeekFrom::Start(0)).unwrap();
        let mut bytes = Vec::new();
        file.read_to_end(&mut bytes).unwrap();
        assert_eq!(bytes, [1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0]);
    }

    #[test]
    fn rejects_truncation_and_capacity_overrun() {
        let mut file = tempfile::tempfile().unwrap();
        let mut sink = Sink::new(&mut file, 12, false, &[]);
        assert!(expand(&mut Cursor::new(fixture()), 0, 11, &mut sink).is_err());
        let mut bytes = fixture();
        bytes.pop();
        assert!(expand(&mut Cursor::new(bytes), 0, 12, &mut sink).is_err());
    }

    #[test]
    fn zero_checksum_matches_materialized_bytes() {
        for size in [0, 1, 3, 1000, 1024 * 1024 + 3] {
            let mut combined = crc32fast::Hasher::new();
            combined.update(&[1, 2, 3]);
            checksum_zeros(&mut combined, size);
            let mut direct = crc32fast::Hasher::new();
            direct.update(&[1, 2, 3]);
            direct.update(&vec![0; size as usize]);
            assert_eq!(combined.finalize(), direct.finalize());
        }
    }
}
