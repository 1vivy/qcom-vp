use super::{
    add,
    image::{u32_at, u64_at},
    mul, range, PartitionIdentity,
};
use anyhow::{ensure, Context, Result};
use std::collections::BTreeSet;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};

struct Header {
    bytes: Vec<u8>,
    entries: Vec<u8>,
    first: u64,
    last: u64,
    count: u32,
    width: u32,
}

fn header(file: &mut File, sector: u64, sectors: u64, at: u64, other: u64) -> Result<Header> {
    file.seek(SeekFrom::Start(mul(at, sector)?))?;
    let mut bytes = vec![0; usize::try_from(sector)?];
    file.read_exact(&mut bytes)?;
    ensure!(
        &bytes[..8] == b"EFI PART",
        "GPT signature differs at LBA {at}"
    );
    ensure!(u32_at(&bytes, 8) == 0x10000, "Unsupported GPT revision");
    let size = usize::try_from(u32_at(&bytes, 12))?;
    ensure!(
        (92..=bytes.len()).contains(&size),
        "Invalid GPT header length"
    );
    ensure!(u32_at(&bytes, 20) == 0, "GPT reserved field is nonzero");
    let expected = u32_at(&bytes, 16);
    bytes[16..20].fill(0);
    ensure!(
        crc32fast::hash(&bytes[..size]) == expected,
        "GPT header CRC32 differs at LBA {at}"
    );
    ensure!(
        u64_at(&bytes, 24) == at && u64_at(&bytes, 32) == other,
        "GPT copy locations differ"
    );
    let first = u64_at(&bytes, 40);
    let last = u64_at(&bytes, 48);
    ensure!(
        first >= 2 && first <= last && last < sectors - 1,
        "GPT usable bounds are invalid"
    );
    ensure!(
        bytes[56..72].iter().any(|b| *b != 0),
        "GPT disk GUID is zero"
    );
    let count = u32_at(&bytes, 80);
    let width = u32_at(&bytes, 84);
    ensure!(
        count > 0 && width >= 128 && width % 128 == 0,
        "Invalid GPT partition entry geometry"
    );
    let length = mul(u64::from(count), u64::from(width))?;
    ensure!(
        length <= 16 * 1024 * 1024,
        "GPT partition array exceeds 16 MiB"
    );
    let location = u64_at(&bytes, 72);
    let array_sectors = add(length, sector - 1)? / sector;
    range(location, array_sectors, sectors)?;
    if at == 1 {
        ensure!(
            location >= 2 && add(location, array_sectors)? <= first,
            "Primary GPT array overlaps usable storage"
        );
    } else {
        ensure!(
            location > last && add(location, array_sectors)? <= at,
            "Backup GPT array overlaps usable storage"
        );
    }
    file.seek(SeekFrom::Start(mul(location, sector)?))?;
    let mut entries = vec![0; usize::try_from(length)?];
    file.read_exact(&mut entries)?;
    ensure!(
        crc32fast::hash(&entries) == u32_at(&bytes, 88),
        "GPT partition array CRC32 differs at LBA {at}"
    );
    Ok(Header {
        bytes,
        entries,
        first,
        last,
        count,
        width,
    })
}

fn guid(bytes: &[u8]) -> String {
    format!(
        "{:08x}-{:04x}-{:04x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
        u32_at(bytes, 0),
        super::image::u16_at(bytes, 4),
        super::image::u16_at(bytes, 6),
        bytes[8],
        bytes[9],
        bytes[10],
        bytes[11],
        bytes[12],
        bytes[13],
        bytes[14],
        bytes[15]
    )
}

pub(super) fn validate(
    file: &mut File,
    sector: u64,
    sectors: u64,
) -> Result<Vec<PartitionIdentity>> {
    ensure!(
        sector >= 512 && sectors >= 4,
        "GPT storage geometry is too small"
    );
    ensure!(
        file.metadata()?.len() == mul(sector, sectors)?,
        "GPT file size differs from LUN geometry"
    );
    let primary = header(file, sector, sectors, 1, sectors - 1)?;
    let backup = header(file, sector, sectors, sectors - 1, 1)?;
    ensure!(
        primary.first == backup.first
            && primary.last == backup.last
            && primary.count == backup.count
            && primary.width == backup.width
            && primary.bytes[56..72] == backup.bytes[56..72],
        "GPT copies disagree on disk geometry"
    );
    ensure!(
        primary.entries == backup.entries,
        "GPT partition copies differ"
    );
    let mut partitions = Vec::new();
    let mut identities = BTreeSet::new();
    for entry in primary.entries.chunks_exact(primary.width as usize) {
        if entry[..16].iter().all(|b| *b == 0) {
            continue;
        }
        let first = u64_at(entry, 32);
        let last = u64_at(entry, 40);
        ensure!(
            first >= primary.first && first <= last && last <= primary.last,
            "GPT partition exceeds usable storage"
        );
        ensure!(
            entry[16..32].iter().any(|b| *b != 0),
            "GPT partition GUID is zero"
        );
        let unique_guid = guid(&entry[16..32]);
        ensure!(
            identities.insert(unique_guid.clone()),
            "GPT partition GUID is duplicated"
        );
        let words: Vec<_> = entry[56..128]
            .chunks_exact(2)
            .map(|w| u16::from_le_bytes([w[0], w[1]]))
            .take_while(|w| *w != 0)
            .collect();
        let name =
            String::from_utf16(&words).context("GPT partition name contains invalid UTF-16")?;
        partitions.push(PartitionIdentity {
            name,
            type_guid: guid(&entry[..16]),
            unique_guid,
            first_lba: first,
            last_lba: last,
            attributes: u64_at(entry, 48),
        });
    }
    let mut bounds: Vec<_> = partitions
        .iter()
        .map(|p| (p.first_lba, p.last_lba))
        .collect();
    bounds.sort_unstable();
    ensure!(
        bounds.windows(2).all(|pair| pair[0].1 < pair[1].0),
        "GPT partitions overlap"
    );
    Ok(partitions)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn guid_uses_gpt_byte_order() {
        assert_eq!(
            guid(&[0x78, 0x56, 0x34, 0x12, 0xbc, 0x9a, 0xf0, 0xde, 1, 2, 3, 4, 5, 6, 7, 8]),
            "12345678-9abc-def0-0102-030405060708"
        );
    }

    #[test]
    fn rejects_short_medium() {
        let mut file = tempfile::tempfile().unwrap();
        assert!(validate(&mut file, 4096, 3).is_err());
        file.set_len(4096 * 32).unwrap();
        assert!(validate(&mut file, 4096, 32).is_err());
    }
}
