//! Assemble private UFS images from OEM package records.

mod expression;
mod gpt;
mod image;
mod super_image;

#[cfg(test)]
mod behavior_tests;

use anyhow::{ensure, Context, Result};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::{BTreeMap, BTreeSet};
use std::fs::{File, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::Path;
use zip::ZipArchive;

/// The storage identities that preparation records.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StorageReport {
    pub boot_enable: bool,
    pub luns: Vec<LunReport>,
    pub consumed_members: Vec<MemberHash>,
    pub omitted_programs: Vec<BTreeMap<String, String>>,
}

/// One prepared user LUN. Boot selection is independent of Android slots.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LunReport {
    pub lun: u8,
    pub filename: String,
    pub sector_size: u64,
    pub sectors: u64,
    pub boot_lun_id: u8,
    pub partitions: Vec<PartitionIdentity>,
}

/// An archive member whose bytes affect preparation.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MemberHash {
    pub name: String,
    pub sha256: String,
    pub bytes: u64,
}

/// A validated GPT partition identity.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct PartitionIdentity {
    pub name: String,
    pub type_guid: String,
    pub unique_guid: String,
    pub first_lba: u64,
    pub last_lba: u64,
    pub attributes: u64,
}

/// The partition that contains the stock Oplus UART record.
pub const OPLUS_UART_PARTITION: &str = "oplusreserve1";

/// The exact mutable record that a run configured before firmware execution.
#[derive(Serialize)]
pub struct OemUartRecord {
    pub lun: u8,
    pub record_lba: u64,
    pub previous_prefix: [u8; 16],
}

/// Enable the stock UART record in a private mutable LUN.
/// Both GPT copies and the prepared partition identities must agree.
pub fn enable_oplus_uart(file: &mut File, lun: &LunReport) -> Result<OemUartRecord> {
    let partitions = gpt::validate(file, lun.sector_size, lun.sectors)?;
    ensure!(
        partitions == lun.partitions,
        "The mutable GPT differs from the prepared partition identities"
    );
    let mut candidates = partitions
        .iter()
        .filter(|partition| partition.name == OPLUS_UART_PARTITION);
    let partition = candidates
        .next()
        .context("The OEM UART partition is absent")?;
    ensure!(
        candidates.next().is_none(),
        "The OEM UART partition name is duplicated"
    );
    let blocks = partition.last_lba - partition.first_lba + 1;
    ensure!(blocks >= 0x800, "The OEM UART partition is too small");
    let record_lba = add(partition.first_lba, blocks - 0x800 + 0x429)?;
    let offset = mul(record_lba, lun.sector_size)?;
    file.seek(SeekFrom::Start(offset))?;
    let mut previous_prefix = [0; 16];
    file.read_exact(&mut previous_prefix)?;
    if &previous_prefix != b"ENABLE_UART:TRUE" {
        file.seek(SeekFrom::Start(offset))?;
        file.write_all(b"ENABLE_UART:TRUE")?;
        file.sync_data()?;
    }
    Ok(OemUartRecord {
        lun: lun.lun,
        record_lba,
        previous_prefix,
    })
}

type Record = BTreeMap<String, String>;

struct Package {
    zip: ZipArchive<File>,
    names: BTreeSet<String>,
    consumed: BTreeMap<String, MemberHash>,
}

impl Package {
    fn open(path: &Path) -> Result<Self> {
        let mut zip = ZipArchive::new(File::open(path)?)?;
        let mut names = BTreeSet::new();
        for i in 0..zip.len() {
            let member = zip.by_index(i)?;
            safe_name(member.name())?;
            ensure!(
                names.insert(member.name().to_owned()),
                "Duplicate archive member: {}",
                member.name()
            );
        }
        Ok(Self {
            zip,
            names,
            consumed: BTreeMap::new(),
        })
    }

    fn bytes(&mut self, name: &str) -> Result<Vec<u8>> {
        let mut data = Vec::new();
        self.stream(name, |reader| {
            ensure!(
                reader.remaining <= 16 * 1024 * 1024,
                "Metadata member exceeds 16 MiB: {name}"
            );
            reader.read_to_end(&mut data)?;
            Ok(())
        })?;
        Ok(data)
    }

    fn stream<T>(
        &mut self,
        name: &str,
        f: impl FnOnce(&mut HashReader<'_>) -> Result<T>,
    ) -> Result<T> {
        safe_name(name)?;
        let member = self
            .zip
            .by_name(name)
            .with_context(|| format!("Required package member is absent: {name}"))?;
        ensure!(
            !member.is_dir(),
            "The selected archive member is a directory: {name}"
        );
        if let Some(mode) = member.unix_mode() {
            let kind = mode & 0o170000;
            ensure!(
                kind == 0 || kind == 0o100000,
                "The selected archive member is not a regular file: {name}"
            );
        }
        let size = member.size();
        let mut reader = HashReader {
            inner: Box::new(member),
            digest: Sha256::new(),
            remaining: size,
        };
        let result = f(&mut reader).with_context(|| format!("Reading package member {name}"))?;
        std::io::copy(&mut reader, &mut std::io::sink())?;
        ensure!(reader.remaining == 0, "Truncated package member: {name}");
        let hash = format!("{:x}", reader.digest.finalize());
        if let Some(previous) = self.consumed.get(name) {
            ensure!(
                previous.sha256 == hash && previous.bytes == size,
                "Package member changed: {name}"
            );
        }
        self.consumed.insert(
            name.to_owned(),
            MemberHash {
                name: name.to_owned(),
                sha256: hash,
                bytes: size,
            },
        );
        Ok(result)
    }

    fn records(&mut self, name: &str, tag: &str) -> Result<Vec<Record>> {
        let data = self.bytes(name)?;
        let text = std::str::from_utf8(&data)?.trim_start_matches('\u{feff}');
        let xml = roxmltree::Document::parse(text)?;
        Ok(xml
            .descendants()
            .filter(|n| n.has_tag_name(tag))
            .map(|node| {
                node.attributes()
                    .map(|a| (a.name().to_owned(), a.value().to_owned()))
                    .collect()
            })
            .collect())
    }
}

struct HashReader<'a> {
    inner: Box<dyn Read + 'a>,
    digest: Sha256,
    remaining: u64,
}

impl Read for HashReader<'_> {
    fn read(&mut self, buffer: &mut [u8]) -> std::io::Result<usize> {
        let count = self.inner.read(buffer)?;
        self.digest.update(&buffer[..count]);
        self.remaining = self
            .remaining
            .checked_sub(count as u64)
            .ok_or_else(|| std::io::Error::other("Archive size mismatch"))?;
        Ok(count)
    }
}

fn safe_name(name: &str) -> Result<()> {
    ensure!(
        !name.is_empty() && !name.starts_with('/') && !name.contains(['\\', ':', '\0']),
        "Unsafe archive path: {name}"
    );
    ensure!(
        name.trim_end_matches('/')
            .split('/')
            .all(|c| !c.is_empty() && c != "." && c != ".."),
        "Unsafe archive path: {name}"
    );
    Ok(())
}

fn attr<'a>(record: &'a Record, key: &str) -> Result<&'a str> {
    record
        .get(key)
        .map(String::as_str)
        .with_context(|| format!("Missing XML attribute: {key}"))
}

fn number(record: &Record, key: &str) -> Result<u64> {
    expression::eval(attr(record, key)?, 0)
}

fn add(a: u64, b: u64) -> Result<u64> {
    a.checked_add(b).context("Storage addition overflows")
}

fn mul(a: u64, b: u64) -> Result<u64> {
    a.checked_mul(b).context("Storage multiplication overflows")
}

fn range(offset: u64, bytes: u64, limit: u64) -> Result<()> {
    ensure!(
        add(offset, bytes)? <= limit,
        "Storage range exceeds its boundary: offset={offset}, bytes={bytes}, limit={limit}"
    );
    Ok(())
}

#[derive(Clone)]
struct Program {
    record: Record,
    source: Source,
}

#[derive(Clone)]
enum Source {
    Empty,
    Member(String),
    Super,
}

#[derive(Clone)]
struct PatchBytes {
    offset: u64,
    bytes: Vec<u8>,
}

struct Sink<'a> {
    file: &'a mut File,
    limit: u64,
    verify: bool,
    patches: &'a [PatchBytes],
    buffer: Vec<u8>,
    mapping: Vec<(u64, u64, u64)>,
}

impl<'a> Sink<'a> {
    fn new(file: &'a mut File, limit: u64, verify: bool, patches: &'a [PatchBytes]) -> Self {
        Self {
            file,
            limit,
            verify,
            patches,
            buffer: Vec::new(),
            mapping: Vec::new(),
        }
    }

    fn emit(&mut self, offset: u64, bytes: &[u8]) -> Result<()> {
        if self.mapping.is_empty() {
            return self.emit_physical(offset, bytes);
        }
        let mut position = offset;
        let mut remaining = bytes;
        while !remaining.is_empty() {
            let (logical, physical, length) = self
                .mapping
                .iter()
                .copied()
                .find(|(logical, _, length)| position >= *logical && position - *logical < *length)
                .context("Source extent exceeds logical partition mapping")?;
            let within = position - logical;
            let count = (length - within).min(remaining.len() as u64) as usize;
            self.emit_physical(add(physical, within)?, &remaining[..count])?;
            position = add(position, count as u64)?;
            remaining = &remaining[count..];
        }
        Ok(())
    }

    fn emit_physical(&mut self, offset: u64, bytes: &[u8]) -> Result<()> {
        range(offset, bytes.len() as u64, self.limit)?;
        self.file.seek(SeekFrom::Start(offset))?;
        if self.verify {
            let mut expected = std::borrow::Cow::Borrowed(bytes);
            for patch in self.patches {
                let begin = offset.max(patch.offset);
                let end = add(offset, bytes.len() as u64)?
                    .min(add(patch.offset, patch.bytes.len() as u64)?);
                if begin < end {
                    expected.to_mut()[(begin - offset) as usize..(end - offset) as usize]
                        .copy_from_slice(
                            &patch.bytes
                                [(begin - patch.offset) as usize..(end - patch.offset) as usize],
                        );
                }
            }
            self.buffer.resize(expected.len(), 0);
            self.file.read_exact(&mut self.buffer)?;
            ensure!(
                self.buffer.as_slice() == expected.as_ref(),
                "Prepared source extent differs at byte {offset}"
            );
        } else if bytes.iter().any(|b| *b != 0) {
            self.file.write_all(bytes)?;
        }
        Ok(())
    }
}

/// Assemble six sparse LUN files in an existing, empty, private directory.
///
/// # Errors
/// Returns an error for unsafe paths, missing images, invalid storage records, or invalid GPT data.
pub fn assemble(package: &Path, out: &Path, omit_secdata: bool) -> Result<StorageReport> {
    let metadata = std::fs::symlink_metadata(out)?;
    ensure!(
        metadata.is_dir() && !metadata.file_type().is_symlink(),
        "Prepared output must be a real directory"
    );
    ensure!(
        std::fs::read_dir(out)?.next().is_none(),
        "Prepared output directory is not empty"
    );
    let mut package = Package::open(package)?;
    let provisioning = package.records("IMAGES/provision_samsung.xml", "ufs")?;
    let growth: Vec<_> = provisioning
        .iter()
        .filter_map(|r| r.get("LUNtoGrow"))
        .collect();
    ensure!(
        growth.len() == 1 && growth[0] == "0",
        "Expected one growing user LUN: LUN0"
    );
    let mut boot_headers = provisioning
        .iter()
        .filter(|record| record.contains_key("bBootEnable"));
    let boot_header = boot_headers.next().context("Missing UFS boot capability")?;
    ensure!(
        boot_headers.next().is_none(),
        "Multiple UFS boot capability records"
    );
    let boot_enable = number(boot_header, "bBootEnable")?;
    ensure!(boot_enable <= 1, "Invalid UFS boot capability");
    let settings = package.bytes("IMAGES/Setting.xml")?;
    let settings = roxmltree::Document::parse(std::str::from_utf8(&settings)?)?;
    let projects = package.records("IMAGES/Projectconfig.xml", "program")?;
    ensure!(
        projects.len() == 1,
        "Package must select exactly one project"
    );
    let project = attr(&projects[0], "project")?;
    ensure!(
        project.bytes().all(|b| b.is_ascii_digit()),
        "Project identity must be numeric"
    );
    let mut report = StorageReport {
        boot_enable: boot_enable != 0,
        luns: Vec::new(),
        consumed_members: Vec::new(),
        omitted_programs: Vec::new(),
    };
    let mut all_programs = Vec::new();
    let mut all_patches = Vec::new();
    for lun in 0..6u8 {
        let geometry = provisioning
            .iter()
            .find(|r| r.get("LUNum").is_some_and(|v| v == &lun.to_string()))
            .context("Missing LUN provisioning")?;
        ensure!(
            number(geometry, "bLUEnable")? == 1,
            "Required LUN is disabled"
        );
        let shift = u32::try_from(number(geometry, "bLogicalBlockSize")?)?;
        let sector_size = 1u64
            .checked_shl(shift)
            .context("Invalid sector-size exponent")?;
        ensure!(sector_size == 4096, "Package must use 4096-byte sectors");
        let bytes = mul(number(geometry, "size_in_kb")?, 1024)?;
        ensure!(
            bytes % sector_size == 0 && bytes > 0,
            "Invalid numeric LUN geometry"
        );
        let program_name = format!("rawprogram{lun}.xml");
        let patch_name = format!("patch{lun}.xml");
        for (tag, expected) in [
            (format!("Program{lun}"), &program_name),
            (format!("Patch{lun}"), &patch_name),
        ] {
            let node = settings
                .descendants()
                .find(|n| n.has_tag_name(tag.as_str()))
                .context("Missing selected storage XML")?;
            let files: Vec<_> = node.children().filter(|n| n.has_tag_name("File")).collect();
            ensure!(
                files.len() == 1 && files[0].attribute("Path") == Some(expected.as_str()),
                "Package selects an unsupported storage XML"
            );
        }
        let records = package.records(&format!("IMAGES/{program_name}"), "program")?;
        let mut programs = Vec::new();
        let mut sectors = bytes / sector_size;
        for record in records {
            ensure!(
                number(&record, "physical_partition_number")? == u64::from(lun),
                "Program LUN differs from its selected XML"
            );
            ensure!(
                number(&record, "SECTOR_SIZE_IN_BYTES")? == sector_size,
                "Program sector size differs from provisioning"
            );
            let filename = attr(&record, "filename")?;
            let source = if omit_secdata
                && filename == "sec.dat"
                && lun == 4
                && record.get("label").is_some_and(|label| label == "secdata")
            {
                report.omitted_programs.push(record.clone());
                Source::Empty
            } else if filename.is_empty() {
                Source::Empty
            } else if filename == "super.img" && !package.names.contains("IMAGES/super.img") {
                Source::Super
            } else {
                let name = if filename == "oplus" {
                    format!("IMAGES/oplus{project}.bin")
                } else {
                    format!("IMAGES/{filename}")
                };
                safe_name(&name)?;
                ensure!(package.names.contains(&name), "Required package member is absent: {name}. Do not substitute a different security image.");
                Source::Member(name)
            };
            if lun == 0 && !attr(&record, "start_sector")?.contains("NUM_DISK_SECTORS") {
                let start = expression::eval(attr(&record, "start_sector")?, 0)?;
                let mut count = number(&record, "num_partition_sectors")?;
                if let Source::Member(name) = &source {
                    if attr(&record, "sparse")? == "true" {
                        let expanded = package.stream(name, |r| image::expanded_size(r))?;
                        ensure!(
                            expanded % sector_size == 0,
                            "Sparse image has a partial sector"
                        );
                        if count == 0 {
                            count = expanded / sector_size;
                        }
                        ensure!(
                            expanded <= mul(count, sector_size)?,
                            "Sparse image exceeds its partition"
                        );
                    }
                }
                sectors = sectors.max(add(add(start, count)?, 5)?);
            }
            programs.push(Program { record, source });
        }
        ensure!(
            programs
                .iter()
                .any(|p| matches!(p.source, Source::Member(_))),
            "LUN has no populated images"
        );
        all_programs.push(programs);
        all_patches.push(package.records(&format!("IMAGES/{patch_name}"), "patch")?);
        report.luns.push(LunReport {
            lun,
            filename: format!("lun{lun}.img"),
            sector_size,
            sectors,
            boot_lun_id: u8::try_from(number(geometry, "bBootLunID")?)?,
            partitions: Vec::new(),
        });
    }
    ensure!(
        provisioning
            .iter()
            .filter(|r| r.contains_key("LUNum") && r.get("bLUEnable").is_some_and(|v| v == "1"))
            .count()
            == 6,
        "Package must enable exactly six user LUNs"
    );
    for (index, lun) in report.luns.iter_mut().enumerate() {
        let mut file = OpenOptions::new()
            .read(true)
            .write(true)
            .create_new(true)
            .open(out.join(&lun.filename))?;
        let length = mul(lun.sector_size, lun.sectors)?;
        file.set_len(length)?;
        let mut occupied = Vec::new();
        for program in &all_programs[index] {
            let written = apply_program(
                &mut package,
                program,
                lun,
                &mut Sink::new(&mut file, length, false, &[]),
            )?;
            if let Some((start, bytes)) = written {
                for &(other, size) in &occupied {
                    ensure!(
                        add(start, bytes)? <= other || add(other, size)? <= start,
                        "Program source extents overlap"
                    );
                }
                occupied.push((start, bytes));
            }
        }
        let patches = apply_patches(&mut file, lun, &all_patches[index], &all_programs[index])?;
        lun.partitions = gpt::validate(&mut file, lun.sector_size, lun.sectors)?;
        for program in &all_programs[index] {
            apply_program(
                &mut package,
                program,
                lun,
                &mut Sink::new(&mut file, length, true, &patches),
            )?;
        }
        for patch in &patches {
            let mut expected = patch.bytes.clone();
            for later in patches
                .iter()
                .skip_while(|p| !std::ptr::eq(*p, patch))
                .skip(1)
            {
                let begin = patch.offset.max(later.offset);
                let end = add(patch.offset, expected.len() as u64)?
                    .min(add(later.offset, later.bytes.len() as u64)?);
                if begin < end {
                    expected[(begin - patch.offset) as usize..(end - patch.offset) as usize]
                        .copy_from_slice(
                            &later.bytes
                                [(begin - later.offset) as usize..(end - later.offset) as usize],
                        );
                }
            }
            file.seek(SeekFrom::Start(patch.offset))?;
            let mut actual = vec![0; expected.len()];
            file.read_exact(&mut actual)?;
            ensure!(actual == expected, "Final patch bytes differ");
        }
        file.sync_all()?;
    }
    report.consumed_members = package.consumed.into_values().collect();
    Ok(report)
}

fn apply_program(
    package: &mut Package,
    program: &Program,
    lun: &LunReport,
    sink: &mut Sink<'_>,
) -> Result<Option<(u64, u64)>> {
    let record = &program.record;
    let start = mul(
        expression::eval(attr(record, "start_sector")?, lun.sectors)?,
        lun.sector_size,
    )?;
    if let Some(bytes) = record.get("start_byte_hex") {
        ensure!(
            expression::eval(bytes, lun.sectors)? == start,
            "Program byte and sector coordinates differ"
        );
    }
    ensure!(
        matches!(attr(record, "sparse")?, "true" | "false"),
        "Invalid program sparse flag"
    );
    let count = number(record, "num_partition_sectors")?;
    let capacity = if count == 0 {
        sink.limit
            .checked_sub(add(start, mul(5, lun.sector_size)?)?)
            .context("Growing partition exceeds LUN")?
    } else {
        mul(count, lun.sector_size)?
    };
    range(start, capacity, sink.limit)?;
    let offset = mul(number(record, "file_sector_offset")?, lun.sector_size)?;
    let size = match &program.source {
        Source::Empty => return Ok(None),
        Source::Super => {
            ensure!(offset == 0, "Composite super image has a source offset");
            super_image::assemble(package, start, capacity, sink)?
        }
        Source::Member(name) => package.stream(name, |reader| {
            if attr(record, "sparse")? == "true" {
                ensure!(
                    offset == 0,
                    "Sparse source offset is not supported by this package format"
                );
                image::expand(reader, start, capacity, sink)
            } else {
                ensure!(
                    offset <= reader.remaining,
                    "Source offset exceeds member size"
                );
                let skipped =
                    std::io::copy(&mut (&mut *reader).take(offset), &mut std::io::sink())?;
                ensure!(skipped == offset, "Source member is truncated");
                let bytes = reader.remaining;
                let padded = mul(
                    add(bytes, lun.sector_size - 1)? / lun.sector_size,
                    lun.sector_size,
                )?;
                ensure!(padded <= capacity, "Source member exceeds its partition");
                image::raw(reader, bytes, start, sink)?;
                if padded > bytes {
                    sink.emit(add(start, bytes)?, &vec![0; (padded - bytes) as usize])?;
                }
                Ok(padded)
            }
        })?,
    };
    ensure!(
        size % lun.sector_size == 0,
        "Program image has a partial sector"
    );
    Ok(Some((start, size)))
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum PatchValue {
    Integer(u64),
    Crc { offset: u64, bytes: u64 },
}

#[derive(Debug, PartialEq, Eq)]
struct PatchSpec {
    offset: u64,
    width: u64,
    value: PatchValue,
}

fn patch_spec(record: &Record, lun: &LunReport, programs: &[Program]) -> Result<PatchSpec> {
    ensure!(
        number(record, "physical_partition_number")? == u64::from(lun.lun),
        "Patch LUN differs from selected XML"
    );
    ensure!(
        number(record, "SECTOR_SIZE_IN_BYTES")? == lun.sector_size,
        "Patch sector size differs from provisioning"
    );
    let filename = attr(record, "filename")?;
    let (base, local_limit) = if filename == "DISK" {
        (0, mul(lun.sectors, lun.sector_size)?)
    } else {
        let matches: Vec<_> = programs
            .iter()
            .filter(|p| attr(&p.record, "filename").is_ok_and(|n| n == filename))
            .collect();
        ensure!(
            matches.len() == 1,
            "Local patch does not map to one program member"
        );
        let program = &matches[0].record;
        ensure!(
            number(program, "file_sector_offset")? == 0 && attr(program, "sparse")? == "false",
            "Local patch requires a complete raw program source"
        );
        (
            mul(
                expression::eval(attr(program, "start_sector")?, lun.sectors)?,
                lun.sector_size,
            )?,
            mul(number(program, "num_partition_sectors")?, lun.sector_size)?,
        )
    };
    let local_offset = add(
        mul(
            expression::eval(attr(record, "start_sector")?, lun.sectors)?,
            lun.sector_size,
        )?,
        number(record, "byte_offset")?,
    )?;
    let width = number(record, "size_in_bytes")?;
    ensure!((1..=8).contains(&width), "Invalid patch width");
    range(local_offset, width, local_limit)?;
    let offset = add(base, local_offset)?;
    range(offset, width, mul(lun.sectors, lun.sector_size)?)?;
    let expression = attr(record, "value")?;
    let value = if let Some(inner) = expression
        .strip_prefix("CRC32(")
        .and_then(|s| s.strip_suffix(')'))
    {
        let (start, bytes) = inner.split_once(',').context("Invalid CRC32 expression")?;
        let local_start = mul(expression::eval(start, lun.sectors)?, lun.sector_size)?;
        let bytes = expression::eval(bytes, lun.sectors)?;
        range(local_start, bytes, local_limit)?;
        let offset = add(base, local_start)?;
        range(offset, bytes, mul(lun.sectors, lun.sector_size)?)?;
        PatchValue::Crc { offset, bytes }
    } else {
        PatchValue::Integer(expression::eval(expression, lun.sectors)?)
    };
    Ok(PatchSpec {
        offset,
        width,
        value,
    })
}

fn apply_patches(
    file: &mut File,
    lun: &LunReport,
    records: &[Record],
    programs: &[Program],
) -> Result<Vec<PatchBytes>> {
    let specs: Vec<_> = records
        .iter()
        .map(|record| patch_spec(record, lun, programs))
        .collect::<Result<_>>()?;
    let mut patches = Vec::new();
    for (record, spec) in records.iter().zip(&specs) {
        if attr(record, "filename")? != "DISK"
            && records
                .iter()
                .zip(&specs)
                .any(|(r, disk)| r.get("filename").is_some_and(|v| v == "DISK") && disk == spec)
        {
            continue;
        }
        let offset = spec.offset;
        let width = spec.width;
        let value = match spec.value {
            PatchValue::Integer(value) => value,
            PatchValue::Crc {
                offset: start,
                bytes,
            } => {
                file.seek(SeekFrom::Start(start))?;
                let mut crc = crc32fast::Hasher::new();
                let mut remaining = bytes;
                let mut buffer = [0; 65536];
                while remaining > 0 {
                    let n = remaining.min(buffer.len() as u64) as usize;
                    file.read_exact(&mut buffer[..n])?;
                    crc.update(&buffer[..n]);
                    remaining -= n as u64;
                }
                u64::from(crc.finalize())
            }
        };
        ensure!(
            width == 8 || value < (1u64 << (width * 8)),
            "Patch value exceeds its width"
        );
        let bytes = value.to_le_bytes()[..width as usize].to_vec();
        file.seek(SeekFrom::Start(offset))?;
        file.write_all(&bytes)?;
        patches.push(PatchBytes { offset, bytes });
    }
    Ok(patches)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reject_unsafe_paths() {
        for name in ["/a", "../a", "a/../b", "a\\b", "C:a", "a//b", "./a"] {
            assert!(safe_name(name).is_err(), "{name}");
        }
        assert!(safe_name("IMAGES/boot.img").is_ok());
    }

    #[test]
    fn checked_ranges() {
        assert!(range(10, 2, 12).is_ok());
        assert!(range(10, 3, 12).is_err());
        assert!(range(u64::MAX, 1, u64::MAX).is_err());
        assert!(mul(u64::MAX, 2).is_err());
    }
}
