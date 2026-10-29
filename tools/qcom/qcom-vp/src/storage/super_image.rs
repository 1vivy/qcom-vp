use super::{add, image, mul, range, Package, Sink};
use anyhow::{ensure, Context, Result};
use serde::Deserialize;
use sha2::{Digest, Sha256};
use std::collections::{BTreeMap, BTreeSet};
use std::io::{Cursor, Read};

#[derive(Deserialize)]
struct Definition {
    super_meta: Metadata,
    super_device: Device,
    partitions: Vec<Partition>,
}

#[derive(Deserialize)]
struct Metadata {
    path: String,
    size: String,
}
#[derive(Deserialize)]
struct Device {
    total_size: String,
}
#[derive(Deserialize)]
struct Partition {
    name: String,
    path: Option<String>,
    size: Option<String>,
}

fn table<'a>(
    header: &'a [u8],
    tables: &'a [u8],
    offset: usize,
    expected_width: u32,
) -> Result<Vec<&'a [u8]>> {
    let start = u64::from(image::u32_at(header, offset));
    let count = image::u32_at(header, offset + 4);
    let width = image::u32_at(header, offset + 8);
    ensure!(
        width == expected_width,
        "Unsupported logical partition entry width"
    );
    let size = mul(u64::from(count), u64::from(width))?;
    range(start, size, tables.len() as u64)?;
    Ok(tables[start as usize..(start + size) as usize]
        .chunks_exact(width as usize)
        .collect())
}

pub(super) fn assemble(
    package: &mut Package,
    start: u64,
    capacity: u64,
    sink: &mut Sink<'_>,
) -> Result<u64> {
    let definition: Definition =
        serde_json::from_slice(&package.bytes("META/super_def.00000000.json")?)?;
    ensure!(
        definition.super_device.total_size.parse::<u64>()? == capacity,
        "Composite super size differs from rawprogram"
    );
    let bytes = package.bytes(&definition.super_meta.path)?;
    ensure!(
        bytes.len() >= 4096 + 128,
        "Logical partition metadata is truncated"
    );
    let geometry = &bytes[..4096];
    ensure!(
        image::u32_at(geometry, 0) == 0x616c4467 && image::u32_at(geometry, 4) == 52,
        "Invalid logical partition geometry"
    );
    let mut checked_geometry = geometry[..52].to_vec();
    checked_geometry[8..40].fill(0);
    ensure!(
        Sha256::digest(&checked_geometry)[..] == geometry[8..40],
        "Logical partition geometry checksum differs"
    );
    let metadata_size = u64::from(image::u32_at(geometry, 40));
    let slots = u64::from(image::u32_at(geometry, 44));
    ensure!(
        metadata_size == definition.super_meta.size.parse::<u64>()?
            && metadata_size >= 128
            && metadata_size % 512 == 0
            && (1..=8).contains(&slots),
        "Invalid logical partition metadata geometry"
    );
    ensure!(
        image::u32_at(geometry, 48) == 4096,
        "Unsupported logical partition block size"
    );
    let header = &bytes[4096..];
    ensure!(
        image::u32_at(header, 0) == 0x414c5030
            && image::u16_at(header, 4) == 10
            && image::u16_at(header, 6) <= 2,
        "Unsupported logical partition metadata version"
    );
    let header_size = image::u32_at(header, 8) as usize;
    ensure!(
        (128..=header.len()).contains(&header_size),
        "Invalid logical partition header size"
    );
    let mut checked_header = header[..header_size].to_vec();
    checked_header[12..44].fill(0);
    ensure!(
        Sha256::digest(&checked_header)[..] == header[12..44],
        "Logical partition header checksum differs"
    );
    let tables_size = image::u32_at(header, 44) as usize;
    range(header_size as u64, tables_size as u64, header.len() as u64)?;
    let tables = &header[header_size..header_size + tables_size];
    ensure!(
        Sha256::digest(tables)[..] == header[48..80],
        "Logical partition table checksum differs"
    );
    ensure!(
        header.len() as u64 <= metadata_size,
        "Logical partition metadata exceeds its slot"
    );
    ensure!(
        header.len() == header_size + tables_size,
        "Logical partition metadata has trailing bytes"
    );
    let partitions = table(header, tables, 80, 52)?;
    let extents = table(header, tables, 92, 24)?;
    let groups = table(header, tables, 104, 48)?;
    let devices = table(header, tables, 116, 64)?;
    ensure!(
        devices.len() == 1 && image::u64_at(devices[0], 16) == capacity,
        "Logical partition device differs from super geometry"
    );
    let reserved = add(12288, mul(mul(metadata_size, slots)?, 2)?)?;
    ensure!(
        mul(image::u64_at(devices[0], 0), 512)? >= reserved,
        "Logical partition data overlaps metadata"
    );
    range(0, reserved, capacity)?;
    let mut definitions = BTreeMap::new();
    for partition in &definition.partitions {
        ensure!(
            definitions
                .insert(partition.name.as_str(), partition)
                .is_none(),
            "Duplicate logical partition definition"
        );
    }
    let mut matched = BTreeSet::new();
    let mut occupied = Vec::new();
    let mut operations = Vec::new();
    for partition in partitions {
        let end = partition[..36].iter().position(|b| *b == 0).unwrap_or(36);
        let name = std::str::from_utf8(&partition[..end])?;
        ensure!(
            matched.insert(name.to_owned()),
            "Duplicate logical partition name"
        );
        let definition = definitions
            .get(name)
            .context("Logical partition has no JSON definition")?;
        ensure!(
            (image::u32_at(partition, 48) as usize) < groups.len(),
            "Logical partition group is invalid"
        );
        let first = image::u32_at(partition, 40) as usize;
        let count = image::u32_at(partition, 44) as usize;
        ensure!(
            first <= extents.len() && count <= extents.len() - first,
            "Logical partition extent index is invalid"
        );
        if count == 0 {
            ensure!(
                definition.path.is_none(),
                "Populated logical partition has no extents"
            );
            continue;
        }
        let path = definition
            .path
            .as_ref()
            .context("Logical partition data path is absent")?;
        let mut logical_size = 0;
        let mut partition_extents = Vec::new();
        for extent in &extents[first..first + count] {
            ensure!(
                image::u32_at(extent, 8) == 0 && image::u32_at(extent, 20) == 0,
                "Logical partition extent is not linear on super"
            );
            let offset = mul(image::u64_at(extent, 12), 512)?;
            let size = mul(image::u64_at(extent, 0), 512)?;
            ensure!(
                size > 0 && offset >= reserved,
                "Logical partition extent overlaps metadata"
            );
            range(offset, size, capacity)?;
            for &(other, length) in &occupied {
                ensure!(
                    add(offset, size)? <= other || add(other, length)? <= offset,
                    "Logical partition extents overlap"
                );
            }
            occupied.push((offset, size));
            logical_size = add(logical_size, size)?;
            partition_extents.push((offset, size));
        }
        ensure!(
            Some(logical_size)
                == definition
                    .size
                    .as_ref()
                    .map(|s| s.parse::<u64>())
                    .transpose()?,
            "Logical partition size differs from JSON"
        );
        operations.push((path.to_owned(), partition_extents, logical_size));
    }
    ensure!(
        matched.len() == definitions.len(),
        "JSON contains a logical partition that metadata does not define"
    );
    sink.emit(add(start, 4096)?, geometry)?;
    sink.emit(add(start, 8192)?, geometry)?;
    for slot in 0..slots * 2 {
        sink.emit(add(start, add(12288, mul(slot, metadata_size)?)?)?, header)?;
    }
    for (path, extents, length) in operations {
        let mut mapped = Sink::new(sink.file, sink.limit, sink.verify, sink.patches);
        let mut logical = 0;
        for (offset, size) in extents {
            mapped.mapping.push((logical, add(start, offset)?, size));
            logical = add(logical, size)?;
        }
        package.stream(&path, |reader| {
            let source_length = reader.remaining;
            let mut prefix = [0; 4];
            reader.read_exact(&mut prefix)?;
            let sparse = u32::from_le_bytes(prefix) == 0xed26ff3a;
            let mut source = Cursor::new(prefix).chain(reader);
            if sparse {
                ensure!(
                    image::expand(&mut source, 0, length, &mut mapped)? == length,
                    "Sparse logical partition size differs from its extents"
                );
            } else {
                ensure!(
                    source_length == length,
                    "Logical partition source size differs from its extents"
                );
                image::raw(&mut source, length, 0, &mut mapped)?;
            }
            Ok(())
        })?;
    }
    Ok(capacity)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs::File;
    use std::io::{Seek, SeekFrom, Write};

    fn fixture(path: &std::path::Path, sparse: bool) {
        let capacity = 2 * 1024 * 1024u64;
        let mut geometry = vec![0u8; 4096];
        for (offset, value) in [(0, 0x616c4467u32), (4, 52), (40, 4096), (44, 1), (48, 4096)] {
            geometry[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        }
        let digest = Sha256::digest(&geometry[..52]);
        geometry[8..40].copy_from_slice(&digest);
        let mut tables = vec![0u8; 188];
        tables[..8].copy_from_slice(b"system_a");
        tables[44..48].copy_from_slice(&1u32.to_le_bytes());
        tables[52..60].copy_from_slice(&8u64.to_le_bytes());
        tables[64..72].copy_from_slice(&2048u64.to_le_bytes());
        tables[124..132].copy_from_slice(&2048u64.to_le_bytes());
        tables[140..148].copy_from_slice(&capacity.to_le_bytes());
        let mut header = vec![0u8; 128];
        header[..4].copy_from_slice(&0x414c5030u32.to_le_bytes());
        header[4..6].copy_from_slice(&10u16.to_le_bytes());
        header[8..12].copy_from_slice(&128u32.to_le_bytes());
        header[44..48].copy_from_slice(&188u32.to_le_bytes());
        header[48..80].copy_from_slice(&Sha256::digest(&tables));
        for (at, offset, width) in [
            (80, 0u32, 52u32),
            (92, 52, 24),
            (104, 76, 48),
            (116, 124, 64),
        ] {
            header[at..at + 4].copy_from_slice(&offset.to_le_bytes());
            header[at + 4..at + 8].copy_from_slice(&1u32.to_le_bytes());
            header[at + 8..at + 12].copy_from_slice(&width.to_le_bytes());
        }
        let digest = Sha256::digest(&header);
        header[12..44].copy_from_slice(&digest);
        geometry.extend(header);
        geometry.extend(tables);
        let definition = serde_json::json!({
            "super_meta":{"path":"IMAGES/meta.raw","size":"4096"},
            "super_device":{"total_size":capacity.to_string()},
            "partitions":[{"name":"system_a","path":"IMAGES/system.img","size":"4096"}]
        });
        let mut data = vec![0x5a; 4096];
        if sparse {
            let mut encoded = Vec::new();
            encoded.extend(0xed26ff3au32.to_le_bytes());
            for n in [1u16, 0, 28, 12] {
                encoded.extend(n.to_le_bytes());
            }
            for n in [4096u32, 1, 1, 0] {
                encoded.extend(n.to_le_bytes());
            }
            encoded.extend(0xcac1u16.to_le_bytes());
            encoded.extend(0u16.to_le_bytes());
            encoded.extend(1u32.to_le_bytes());
            encoded.extend(4108u32.to_le_bytes());
            encoded.extend(data);
            data = encoded;
        }
        let mut zip = zip::ZipWriter::new(File::create(path).unwrap());
        for (name, data) in [
            (
                "META/super_def.00000000.json",
                serde_json::to_vec(&definition).unwrap(),
            ),
            ("IMAGES/meta.raw", geometry),
            ("IMAGES/system.img", data),
        ] {
            zip.start_file(name, zip::write::SimpleFileOptions::default())
                .unwrap();
            zip.write_all(&data).unwrap();
        }
        zip.finish().unwrap();
    }

    #[test]
    fn assembles_raw_and_sparse_logical_partitions() {
        for sparse in [false, true] {
            let directory = tempfile::tempdir().unwrap();
            let path = directory.path().join("package.zip");
            fixture(&path, sparse);
            let mut package = Package::open(&path).unwrap();
            let mut file = tempfile::tempfile().unwrap();
            file.set_len(2 * 1024 * 1024).unwrap();
            assemble(
                &mut package,
                0,
                2 * 1024 * 1024,
                &mut Sink::new(&mut file, 2 * 1024 * 1024, false, &[]),
            )
            .unwrap();
            assemble(
                &mut package,
                0,
                2 * 1024 * 1024,
                &mut Sink::new(&mut file, 2 * 1024 * 1024, true, &[]),
            )
            .unwrap();
            assert_eq!(package.consumed.len(), 3);
            file.seek(SeekFrom::Start(1024 * 1024)).unwrap();
            let mut bytes = vec![0; 4096];
            file.read_exact(&mut bytes).unwrap();
            assert_eq!(bytes, vec![0x5a; 4096]);
        }
    }
}
