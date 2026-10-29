//! Map UEFI firmware volumes and analyse the static dispatch order.
//!
//! The tool reads one UEFI image. The image can be a raw volume, a raw
//! partition, or a ZIP member. It walks every firmware volume, including
//! volumes inside guided and compressed sections.
//!
//! The dispatch analysis is static. A driver that contains a protocol GUID is
//! a *candidate* producer. The image bytes do not prove that the driver
//! installs the protocol.

use crate::observer;
use crate::uefi_depex::{self, Expr};
use anyhow::{ensure, Context, Result};
use serde::Serialize;
use sha2::{Digest, Sha256};
use std::collections::{BTreeMap, BTreeSet, HashSet};
use std::fmt::Write as _;
use std::fs::{self, File};
use std::io::{BufRead, BufReader, Read};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

const MAX_IMAGE_BYTES: u64 = 256 << 20;
const MAX_DECODED_BYTES: usize = 512 << 20;
const MAX_DEPTH: usize = 8;
const MAX_LOG_BYTES: u64 = 256 << 20;
const MAX_LOADS_PER_LOG: usize = 4096;

const LZMA_GUID: &str = "ee4e5898-3914-4259-9d6e-dc7bd79403cf";
const CRC32_GUID: &str = "fc1bcdb0-7d31-49aa-936a-a4600d9dd083";
const APRIORI_GUID: &str = "fc510ee7-ffdc-11d4-bd41-0080c73c8881";

const REFERENCE_NOTE: &str =
    "Static reference candidate. The image contains the GUID. This is not a proven install.";

/// The built-in GUID table. The table holds EDK2 infrastructure GUIDs.
/// The tool resolves additional names from the selected GUID definition files.
const BUILTIN_NAMES: &[(&str, &str)] = &[
    (LZMA_GUID, "LZMA guided section"),
    (CRC32_GUID, "CRC32 guided section"),
    (APRIORI_GUID, "DXE APRIORI file"),
    ("8c8ce578-8a3d-4f1c-9935-896185c32dd3", "FFSv2 file system"),
    ("5473c07a-3dcb-4dca-bd6f-1e9689e7349a", "FFSv3 file system"),
    ("7a9354d9-0468-444a-81ce-0bf617d890df", "FFSv1 file system"),
    ("d6a2cb7f-6a18-4e2f-b43b-9920a733700a", "DxeCore"),
    ("1b45cc0a-156a-428a-af62-49864da0e6e6", "PEI APRIORI file"),
    (
        "5b1b31a1-9562-11d2-8e3f-00a0c969723b",
        "gEfiLoadedImageProtocolGuid",
    ),
    (
        "09576e91-6d3f-11d2-8e39-00a0c969723b",
        "gEfiDevicePathProtocolGuid",
    ),
    (
        "964e5b21-6459-11d2-8e39-00a0c969723b",
        "gEfiBlockIoProtocolGuid",
    ),
    (
        "964e5b22-6459-11d2-8e39-00a0c969723b",
        "gEfiSimpleFileSystemProtocolGuid",
    ),
    (
        "387477c1-69c7-11d2-8e39-00a0c969723b",
        "gEfiSimpleTextInProtocolGuid",
    ),
    (
        "387477c2-69c7-11d2-8e39-00a0c969723b",
        "gEfiSimpleTextOutProtocolGuid",
    ),
    (
        "ce345171-ba0b-11d2-8e4f-00a0c969723b",
        "gEfiDiskIoProtocolGuid",
    ),
    ("09576e92-6d3f-11d2-8e39-00a0c969723b", "gEfiFileInfoGuid"),
    (
        "220e73b6-6bdb-4413-8405-b974b8ba6b08",
        "gEfiFirmwareVolume2ProtocolGuid",
    ),
    (
        "2f707ebb-4a1a-11d4-9a38-0090273fc14d",
        "gEfiPciRootBridgeIoProtocolGuid",
    ),
    (
        "9042a9de-23dc-4a38-96fb-7aded080516a",
        "gEfiGraphicsOutputProtocolGuid",
    ),
    (
        "03583ff6-cb36-4940-947e-b9b39f4afaf7",
        "gEfiSmbiosProtocolGuid",
    ),
    (
        "ef9fc172-a1b2-4693-b327-6d32fc416042",
        "gEfiHiiDatabaseProtocolGuid",
    ),
    (
        "1d85cd7f-f43d-11d2-9a0c-0090273fc14d",
        "gEfiUnicodeCollationProtocolGuid",
    ),
    (
        "bb25cf6f-f1d4-11d2-9a0c-0090273fc14d",
        "gEfiSerialIoProtocolGuid",
    ),
    (
        "a46423e3-4617-49f1-b9ff-d1bfa9115839",
        "gEfiSecurityArchProtocolGuid",
    ),
    (
        "26baccb1-6f42-11d4-bce7-0080c73c8881",
        "gEfiCpuArchProtocolGuid",
    ),
    (
        "26baccb3-6f42-11d4-bce7-0080c73c8881",
        "gEfiTimerArchProtocolGuid",
    ),
    (
        "26baccb2-6f42-11d4-bce7-0080c73c8881",
        "gEfiMetronomeArchProtocolGuid",
    ),
    (
        "665e3ff6-46cc-11d4-9a38-0090273fc14d",
        "gEfiBdsArchProtocolGuid",
    ),
    (
        "665e3ff5-46cc-11d4-9a38-0090273fc14d",
        "gEfiWatchdogTimerArchProtocolGuid",
    ),
    (
        "27cfac87-46cc-11d4-9a38-0090273fc14d",
        "gEfiRealTimeClockArchProtocolGuid",
    ),
    (
        "27cfac88-46cc-11d4-9a38-0090273fc14d",
        "gEfiResetArchProtocolGuid",
    ),
    (
        "1e5668e2-8481-11d4-bcf1-0080c73c8881",
        "gEfiVariableArchProtocolGuid",
    ),
    (
        "6441f818-6362-4e44-b570-7dba31dd2453",
        "gEfiVariableWriteArchProtocolGuid",
    ),
    (
        "1da97072-bddc-4b30-99f1-72a0b56fff2a",
        "gEfiMonotonicCounterArchProtocolGuid",
    ),
    (
        "5053697e-2cbc-4819-90d9-0580deee5754",
        "gEfiCapsuleArchProtocolGuid",
    ),
    (
        "b7dfb4e1-052f-449f-87be-9818fc91b733",
        "gEfiRuntimeArchProtocolGuid",
    ),
];

// EDK2 DxeProtocolNotify.c requires these services for a driver without DEPEX.
const ARCH_PROTOCOLS: &[&str] = &[
    "a46423e3-4617-49f1-b9ff-d1bfa9115839",
    "26baccb1-6f42-11d4-bce7-0080c73c8881",
    "26baccb2-6f42-11d4-bce7-0080c73c8881",
    "26baccb3-6f42-11d4-bce7-0080c73c8881",
    "665e3ff5-46cc-11d4-9a38-0090273fc14d",
    "665e3ff6-46cc-11d4-9a38-0090273fc14d",
    "27cfac87-46cc-11d4-9a38-0090273fc14d",
    "27cfac88-46cc-11d4-9a38-0090273fc14d",
    "1e5668e2-8481-11d4-bcf1-0080c73c8881",
    "6441f818-6362-4e44-b570-7dba31dd2453",
    "1da97072-bddc-4b30-99f1-72a0b56fff2a",
    "5053697e-2cbc-4819-90d9-0580deee5754",
    "b7dfb4e1-052f-449f-87be-9818fc91b733",
];

/// These core protocols are assumptions, not architectural driver services.
/// The JSON output records the initial set.
const BASE_PROTOCOLS: &[&str] = &[
    "220e73b6-6bdb-4413-8405-b974b8ba6b08",
    "5b1b31a1-9562-11d2-8e3f-00a0c969723b",
    "09576e91-6d3f-11d2-8e39-00a0c969723b",
];

/// The command line options of the `uefi-map` action.
pub struct Options {
    pub image: PathBuf,
    pub member: Option<String>,
    pub out: PathBuf,
    pub logs: Vec<PathBuf>,
    pub sdk: Vec<PathBuf>,
}

#[derive(Serialize)]
struct Report {
    schema: u32,
    image: String,
    image_bytes: u64,
    image_sha256: String,
    member: Option<String>,
    volumes: Vec<VolumeEntry>,
    files: Vec<FileEntry>,
    apriori: Vec<String>,
    guid_names: BTreeMap<String, String>,
    assumed_base_protocols: BTreeMap<String, String>,
    protocols: BTreeMap<String, ProtocolReport>,
    dispatch: DispatchReport,
    logs: Vec<LogReport>,
    diagnostics: Vec<String>,
    limits: Limits,
}

#[derive(Serialize)]
struct Limits {
    max_image_bytes: u64,
    max_decoded_bytes: usize,
    max_depth: usize,
}

#[derive(Serialize)]
struct VolumeEntry {
    index: usize,
    buffer: String,
    depth: usize,
    offset: u64,
    length: u64,
    header_length: u16,
    revision: u16,
    attributes: u32,
    file_system_guid: String,
    file_system_name: Option<String>,
    file_count: usize,
    files: Vec<String>,
    apriori: Vec<String>,
}

#[derive(Serialize)]
struct FileEntry {
    guid: String,
    name: Option<String>,
    label: String,
    ffs_type: u8,
    ffs_type_name: String,
    version: Option<String>,
    volume: usize,
    offset: u64,
    size: u32,
    attributes: u8,
    apriori_index: Option<usize>,
    sections: Vec<SectionEntry>,
    images: Vec<ImageEntry>,
    depex: Option<DepexEntry>,
    #[serde(skip)]
    image_bytes: Vec<Vec<u8>>,
    #[serde(skip)]
    depex_guid_bytes: Vec<[u8; 16]>,
}

#[derive(Serialize, Clone)]
struct SectionEntry {
    kind: u8,
    kind_name: String,
    size: u32,
}

#[derive(Serialize, Clone)]
struct ImageEntry {
    kind: String,
    size: u32,
    sha256: String,
    machine: Option<u16>,
}

#[derive(Serialize, Clone)]
struct DepexEntry {
    kind: String,
    bytes: usize,
    sha256: String,
    expression: Option<Expr>,
    text: Option<String>,
    error: Option<String>,
    sor: bool,
    scheduling: Vec<ScheduleNote>,
}

#[derive(Serialize, Clone)]
struct ScheduleNote {
    order: &'static str,
    guid: String,
    name: Option<String>,
}

#[derive(Serialize, Default)]
struct ProtocolReport {
    name: Option<String>,
    available: bool,
    from_base_protocols: bool,
    depended_by: Vec<String>,
    reference_candidates: Vec<String>,
    dispatched_reference_candidates: Vec<String>,
    observed_installs: usize,
    note: &'static str,
}

#[derive(Serialize)]
struct DispatchReport {
    order: Vec<DispatchStep>,
    never_dispatched: Vec<NeverDispatched>,
    solver: &'static str,
}

#[derive(Serialize)]
struct DispatchStep {
    guid: String,
    label: String,
    apriori_index: Option<usize>,
    depex: String,
}

#[derive(Serialize)]
struct NeverDispatched {
    guid: String,
    label: String,
    depex: Option<String>,
    reason: String,
    unsatisfied: Vec<Unsatisfied>,
    scheduling: Vec<ScheduleNote>,
}

#[derive(Serialize)]
struct Unsatisfied {
    guid: String,
    name: Option<String>,
    reference_candidates: Vec<String>,
    dispatched_candidates: Vec<String>,
}

#[derive(Serialize)]
struct LogReport {
    path: String,
    compression: String,
    lines: u64,
    truncated: bool,
    loads: usize,
    observed: Vec<ObservedLoad>,
    installs: BTreeMap<String, usize>,
    loaded_but_not_mapped: Vec<String>,
    mapped_but_not_loaded: Vec<String>,
    order_mismatches: Vec<OrderMismatch>,
}

#[derive(Serialize, Clone)]
struct ObservedLoad {
    line: u64,
    name: String,
    address: Option<String>,
    entry_point: Option<String>,
}

#[derive(Serialize)]
struct OrderMismatch {
    first_in_static: String,
    first_in_observed: String,
    note: &'static str,
}

/// Execute the `uefi-map` action.
pub fn run(options: &Options) -> Result<()> {
    let (bytes, source) = load_image(options)?;
    let mut names = builtin_names();
    for path in &options.sdk {
        load_source_names(path, &mut names)?;
    }

    let mut walk = Walk::new(names);
    walk.walk_buffer(&bytes, &source, 0);
    let mut report = walk.finish(&bytes, &source, options.member.clone());

    for path in &options.logs {
        report.logs.push(correlate_log(path, &report)?);
    }
    apply_observed_installs(&mut report);

    fs::create_dir_all(&options.out)?;
    fs::write(
        options.out.join("uefi-map.json"),
        serde_json::to_vec_pretty(&report)?,
    )?;
    fs::write(options.out.join("uefi-map.dot"), render_dot(&report))?;
    fs::write(options.out.join("uefi-map.md"), render_markdown(&report))?;

    println!(
        "Mapped {} volumes and {} files. Dispatched {}, never dispatched {}. Output: {}",
        report.volumes.len(),
        report.files.len(),
        report.dispatch.order.len(),
        report.dispatch.never_dispatched.len(),
        options.out.display()
    );
    Ok(())
}

fn load_image(options: &Options) -> Result<(Vec<u8>, String)> {
    let source = match &options.member {
        Some(member) => format!("{}:{member}", options.image.display()),
        None => options.image.display().to_string(),
    };
    let bytes = match &options.member {
        Some(member) => {
            let file = File::open(&options.image)
                .with_context(|| format!("Cannot open {}", options.image.display()))?;
            let mut archive =
                zip::ZipArchive::new(file).context("The package is not a ZIP file")?;
            let mut entry = archive
                .by_name(member)
                .with_context(|| format!("The package has no member {member}"))?;
            let declared = entry.size();
            ensure!(
                declared <= MAX_IMAGE_BYTES,
                "The ZIP member declares {declared} bytes, above the {MAX_IMAGE_BYTES} byte limit"
            );
            let mut bytes = Vec::with_capacity(declared as usize);
            entry
                .by_ref()
                .take(MAX_IMAGE_BYTES + 1)
                .read_to_end(&mut bytes)
                .context("Cannot read the ZIP member")?;
            ensure!(
                bytes.len() as u64 <= MAX_IMAGE_BYTES,
                "The ZIP member is larger than the {MAX_IMAGE_BYTES} byte limit"
            );
            bytes
        }
        None => {
            let metadata = fs::metadata(&options.image)
                .with_context(|| format!("Cannot read {}", options.image.display()))?;
            ensure!(
                metadata.len() <= MAX_IMAGE_BYTES,
                "The image has {} bytes, above the {MAX_IMAGE_BYTES} byte limit",
                metadata.len()
            );
            let file = File::open(&options.image)?;
            let mut bytes = Vec::with_capacity(metadata.len() as usize);
            file.take(MAX_IMAGE_BYTES + 1)
                .read_to_end(&mut bytes)
                .context("Cannot read the image")?;
            ensure!(
                bytes.len() as u64 <= MAX_IMAGE_BYTES,
                "The image is larger than the {MAX_IMAGE_BYTES} byte limit"
            );
            bytes
        }
    };
    Ok((bytes, source))
}

fn builtin_names() -> BTreeMap<String, String> {
    BUILTIN_NAMES
        .iter()
        .map(|(guid, name)| ((*guid).to_owned(), (*name).to_owned()))
        .collect()
}

fn load_source_names(path: &Path, names: &mut BTreeMap<String, String>) -> Result<()> {
    let mut files = Vec::new();
    collect_source_files(path, &mut files, 0)?;
    for file in files {
        let text = match fs::read_to_string(&file) {
            Ok(text) => text,
            Err(error) => {
                eprintln!("qcom-vp: skip {}: {error}", file.display());
                continue;
            }
        };
        if text.len() > (4 << 20) {
            continue;
        }
        match file.extension().and_then(|value| value.to_str()) {
            Some("dec") => parse_dec(&text, names),
            Some("h") => parse_header(&text, names),
            Some("inf") => parse_inf(&text, names),
            _ => {}
        }
    }
    Ok(())
}

fn parse_inf(text: &str, names: &mut BTreeMap<String, String>) {
    let mut base_name = None;
    for line in text.lines() {
        let line = line.split('#').next().unwrap_or("").trim();
        let Some((left, right)) = line.split_once('=') else {
            continue;
        };
        let key = left.trim();
        let value = right.trim();
        if key.eq_ignore_ascii_case("BASE_NAME") {
            base_name = Some(value.to_owned());
        } else if key.eq_ignore_ascii_case("FILE_GUID") {
            if let Some(guid) = parse_guid_values(value) {
                if let Some(name) = &base_name {
                    names.entry(guid).or_insert_with(|| name.clone());
                }
            }
        }
    }
}

fn collect_source_files(path: &Path, out: &mut Vec<PathBuf>, depth: usize) -> Result<()> {
    if depth > 12 || out.len() >= 20_000 {
        return Ok(());
    }
    let metadata = fs::metadata(path)?;
    if metadata.is_file() {
        let extension = path.extension().and_then(|value| value.to_str());
        if matches!(extension, Some("dec" | "h" | "inf")) {
            out.push(path.to_path_buf());
        }
        return Ok(());
    }
    for entry in fs::read_dir(path)? {
        let entry = entry?;
        collect_source_files(&entry.path(), out, depth + 1)?;
    }
    Ok(())
}

fn parse_dec(text: &str, names: &mut BTreeMap<String, String>) {
    for line in text.lines() {
        let line = line.split("##").next().unwrap_or("");
        let Some((left, right)) = line.split_once('=') else {
            continue;
        };
        if !right.contains('{') {
            continue;
        }
        let name = left.split_whitespace().last().unwrap_or("").trim();
        if name.is_empty() || !name.starts_with('g') {
            continue;
        }
        if let Some(guid) = parse_guid_values(right) {
            names.entry(guid).or_insert_with(|| name.to_owned());
        }
    }
}

fn parse_header(text: &str, names: &mut BTreeMap<String, String>) {
    let mut cursor = 0;
    while let Some(position) = text[cursor..].find("#define") {
        let start = cursor + position + "#define".len();
        let rest = &text[start..];
        let Some(brace) = rest.find('{') else {
            cursor = start;
            continue;
        };
        let name = rest[..brace].split_whitespace().next().unwrap_or("");
        let after = &rest[brace..];
        let Some(end) = after.find('}') else {
            break;
        };
        if name.starts_with('g') || name.contains("GUID") || name.contains("Guid") {
            if let Some(guid) = parse_guid_values(&after[..=end]) {
                names.entry(guid).or_insert_with(|| name.to_owned());
            }
        }
        cursor = start + brace + end + 1;
    }
}

fn parse_guid_values(text: &str) -> Option<String> {
    let mut values = Vec::new();
    let mut cursor = 0;
    while let Some(position) = text[cursor..].find("0x") {
        let start = cursor + position + 2;
        let digits: String = text[start..]
            .chars()
            .take_while(|value| value.is_ascii_hexdigit())
            .collect();
        if digits.is_empty() {
            cursor = start;
            continue;
        }
        values.push(u32::from_str_radix(&digits, 16).ok()?);
        cursor = start + digits.len();
        if values.len() > 16 {
            return None;
        }
    }
    match values.len() {
        11 => {
            let tail: Vec<u8> = values[5..11].iter().map(|value| *value as u8).collect();
            Some(canonical_guid(
                values[0],
                values[1] as u16,
                values[2] as u16,
                values[3] as u8,
                values[4] as u8,
                &tail,
            ))
        }
        16 => {
            let bytes: Vec<u8> = values.iter().map(|value| *value as u8).collect();
            Some(canonical_guid(
                u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]),
                u16::from_le_bytes([bytes[4], bytes[5]]),
                u16::from_le_bytes([bytes[6], bytes[7]]),
                bytes[8],
                bytes[9],
                &bytes[10..16],
            ))
        }
        _ => None,
    }
}

fn canonical_guid(data1: u32, data2: u16, data3: u16, b0: u8, b1: u8, tail: &[u8]) -> String {
    let mut text = format!("{data1:08x}-{data2:04x}-{data3:04x}-{b0:02x}{b1:02x}-");
    for byte in tail {
        text.push_str(&format!("{byte:02x}"));
    }
    text
}

fn guid_raw(text: &str) -> Option<[u8; 16]> {
    let mut fields = text.split('-');
    let data1 = u32::from_str_radix(fields.next()?, 16).ok()?;
    let data2 = u16::from_str_radix(fields.next()?, 16).ok()?;
    let data3 = u16::from_str_radix(fields.next()?, 16).ok()?;
    let tail: String = fields.collect();
    if tail.len() != 16 {
        return None;
    }
    let mut raw = [0u8; 16];
    raw[..4].copy_from_slice(&data1.to_le_bytes());
    raw[4..6].copy_from_slice(&data2.to_le_bytes());
    raw[6..8].copy_from_slice(&data3.to_le_bytes());
    for index in 0..8 {
        raw[8 + index] = u8::from_str_radix(&tail[index * 2..index * 2 + 2], 16).ok()?;
    }
    Some(raw)
}

struct Walk {
    volumes: Vec<VolumeEntry>,
    files: Vec<FileEntry>,
    apriori: Vec<String>,
    diagnostics: Vec<String>,
    names: BTreeMap<String, String>,
    processed_buffers: BTreeSet<[u8; 32]>,
    next_buffer: usize,
    decoded_bytes: usize,
}

impl Walk {
    fn new(names: BTreeMap<String, String>) -> Self {
        Self {
            volumes: Vec::new(),
            files: Vec::new(),
            apriori: Vec::new(),
            diagnostics: Vec::new(),
            names,
            processed_buffers: BTreeSet::new(),
            next_buffer: 0,
            decoded_bytes: 0,
        }
    }

    fn buffer_id(&mut self) -> usize {
        let value = self.next_buffer;
        self.next_buffer += 1;
        value
    }

    fn walk_buffer(&mut self, data: &[u8], label: &str, depth: usize) {
        if depth > MAX_DEPTH {
            self.diagnostics.push(format!(
                "Depth limit reached at {label}. The walk stops here."
            ));
            return;
        }
        if depth > 0 {
            let mut hasher = Sha256::new();
            hasher.update(data);
            if !self.processed_buffers.insert(hasher.finalize().into()) {
                self.diagnostics
                    .push(format!("Skip a duplicate payload at {label}."));
                return;
            }
        }
        if self.try_section_stream(data, label, depth) {
            return;
        }
        if let Some(header) = valid_fv(data, 0) {
            let buffer = self.buffer_id();
            self.parse_fv(data, 0, &header, buffer, label, depth);
            return;
        }
        self.discover(data, label, depth);
    }

    /// Walk a payload that starts with firmware file sections.
    ///
    /// The function returns false when the first section header is not valid.
    fn try_section_stream(&mut self, data: &[u8], label: &str, depth: usize) -> bool {
        let Some((_, kind, _)) = section_header(data, 0) else {
            return false;
        };
        if !known_section(kind) {
            return false;
        }
        self.walk_sections(data, 0, label, depth, None);
        true
    }

    fn discover(&mut self, data: &[u8], label: &str, depth: usize) {
        // Parse enclosing firmware volumes first. Their nested sections hold
        // the compressed volumes, and the duplicate check then skips a second
        // decode of the same payload.
        let mut offset = 0;
        while let Some(position) = find_from(data, b"_FVH", offset) {
            offset = position + 4;
            let Some(start) = position.checked_sub(0x28) else {
                continue;
            };
            let Some(header) = valid_fv(data, start) else {
                continue;
            };
            let buffer = self.buffer_id();
            self.parse_fv(data, start, &header, buffer, label, depth);
        }
        if let Some(pattern) = guid_raw(LZMA_GUID) {
            for position in find_all(data, &pattern) {
                for header_size in [4usize, 8usize] {
                    if position < header_size {
                        continue;
                    }
                    let start = position - header_size;
                    if !is_guided_header(data, start, header_size) {
                        continue;
                    }
                    self.handle_guided(data, start, header_size, label, depth, None);
                }
            }
        }
    }

    fn parse_fv(
        &mut self,
        data: &[u8],
        start: usize,
        header: &FvHeader,
        _buffer: usize,
        label: &str,
        depth: usize,
    ) {
        if !header.checksum_ok {
            self.diagnostics.push(format!(
                "The firmware volume at {start:#x} in {label} has an invalid header checksum. The map includes it."
            ));
        }
        let length = header.length as usize;
        let mut files = Vec::new();
        let index = self.volumes.len();
        let file_system_guid =
            uefi_depex::guid(&header.file_system_guid).unwrap_or_else(|_| "unknown".to_owned());
        let file_system_name = self.names.get(&file_system_guid).cloned();
        self.volumes.push(VolumeEntry {
            index,
            buffer: label.to_owned(),
            depth,
            offset: start as u64,
            length: header.length,
            header_length: header.header_length,
            revision: header.revision,
            attributes: header.attributes,
            file_system_guid,
            file_system_name,
            file_count: 0,
            files: Vec::new(),
            apriori: Vec::new(),
        });
        let erased_state = if header.attributes & 0x0000_0800 != 0 {
            0xff
        } else {
            0x00
        };
        let mut offset = align8(header.header_length as usize);
        while offset + 24 <= length {
            let name = &data[start + offset..start + offset + 16];
            let state = data[start + offset + 23];
            if state == erased_state {
                break;
            }
            let mut file_size = read_u24(data, start + offset + 20) as usize;
            let mut header_size = 24;
            let attributes = data[start + offset + 19];
            if attributes & 0x01 != 0 {
                if offset + 28 > length {
                    self.diagnostics.push(format!(
                        "A large FFS file header is truncated at {offset:#x}."
                    ));
                    break;
                }
                file_size = read_u32(data, start + offset + 24).unwrap_or(0) as usize;
                header_size = 28;
            }
            if file_size < header_size || offset + file_size > length {
                self.diagnostics.push(format!(
                    "An FFS file at {offset:#x} has size {file_size:#x}. The walk stops in this volume."
                ));
                break;
            }
            let ffs_type = data[start + offset + 18];
            if ffs_type == 0xf0 {
                // A pad file carries an all-FF name. Advance past it.
                offset = align8(offset + file_size);
                continue;
            }
            if name.iter().all(|byte| *byte == 0xff) || name.iter().all(|byte| *byte == 0x00) {
                self.diagnostics.push(format!(
                    "An FFS file at {offset:#x} has an erased name. The walk stops in this volume."
                ));
                break;
            }
            let Some(file) = self.read_file(
                data,
                start + offset,
                header_size,
                file_size,
                index,
                label,
                depth,
                &mut files,
            ) else {
                break;
            };
            self.files.push(file);
            offset = align8(offset + file_size);
        }
        self.volumes[index].file_count = files.len();
        self.volumes[index].files = files;
    }

    #[allow(clippy::too_many_arguments)]
    fn read_file(
        &mut self,
        data: &[u8],
        start: usize,
        header_size: usize,
        file_size: usize,
        volume: usize,
        label: &str,
        depth: usize,
        list: &mut Vec<String>,
    ) -> Option<FileEntry> {
        let guid = uefi_depex::guid(&data[start..start + 16]).ok()?;
        let ffs_type = data[start + 18];
        let attributes = data[start + 19];
        let body_start = start.checked_add(header_size)?;
        let body_end = start.checked_add(file_size)?;
        if body_end > data.len() {
            self.diagnostics.push(format!(
                "The FFS file {guid} crosses the buffer end. It is skipped."
            ));
            return None;
        }
        let mut entry = FileEntry {
            guid: guid.clone(),
            name: None,
            label: guid.clone(),
            ffs_type,
            ffs_type_name: ffs_type_name(ffs_type).to_owned(),
            version: None,
            volume,
            offset: start as u64,
            size: file_size as u32,
            attributes,
            apriori_index: None,
            sections: Vec::new(),
            images: Vec::new(),
            depex: None,
            image_bytes: Vec::new(),
            depex_guid_bytes: Vec::new(),
        };
        if ffs_type != 0xf0 {
            self.walk_sections(
                &data[..body_end],
                body_start,
                label,
                depth,
                Some(&mut entry),
            );
        }
        if entry.name.is_none() {
            entry.name = self.names.get(&guid).cloned();
        }
        entry.label = entry.name.clone().unwrap_or_else(|| entry.guid.clone());
        list.push(entry.label.clone());
        Some(entry)
    }

    fn walk_sections(
        &mut self,
        data: &[u8],
        start: usize,
        label: &str,
        depth: usize,
        mut entry: Option<&mut FileEntry>,
    ) {
        let mut offset = start;
        while offset + 4 <= data.len() {
            let Some((size, kind, header_size)) = section_header(data, offset) else {
                if data[offset..]
                    .iter()
                    .all(|byte| *byte == 0 || *byte == 0xff)
                {
                    break;
                }
                self.diagnostics.push(format!(
                    "A section header at {offset:#x} in {label} is not valid."
                ));
                break;
            };
            if offset + size > data.len() {
                self.diagnostics.push(format!(
                    "The section at {offset:#x} in {label} has size {size:#x} beyond the buffer."
                ));
                break;
            }
            if let Some(file) = entry.as_deref_mut() {
                file.sections.push(SectionEntry {
                    kind,
                    kind_name: section_kind_name(kind).to_owned(),
                    size: size as u32,
                });
            }
            let body = &data[offset + header_size..offset + size];
            match kind {
                0x01 => {
                    if body.len() < 5 {
                        self.diagnostics
                            .push(format!("A compression section in {label} is truncated."));
                    } else {
                        let compression = body[4];
                        if compression > 1 {
                            self.diagnostics.push(format!(
                                "The compression type {compression} in {label} is not supported."
                            ));
                        } else if compression == 1 {
                            self.diagnostics
                                .push(format!("The Tiano compression in {label} is not decoded."));
                        } else {
                            self.walk_sections(
                                &body[5..],
                                0,
                                label,
                                depth + 1,
                                entry.as_deref_mut(),
                            );
                        }
                    }
                }
                0x02 => {
                    self.handle_guided(
                        data,
                        offset,
                        header_size,
                        label,
                        depth,
                        entry.as_deref_mut(),
                    );
                }
                0x03 => {
                    self.walk_sections(body, 0, label, depth + 1, entry.as_deref_mut());
                }
                0x10..=0x12 => {
                    if let Some(file) = entry.as_deref_mut() {
                        file.images.push(ImageEntry {
                            kind: match kind {
                                0x10 => "PE32",
                                0x11 => "PIC",
                                _ => "TE",
                            }
                            .to_owned(),
                            size: body.len() as u32,
                            sha256: hex_digest(body),
                            machine: image_machine(kind, body),
                        });
                        file.image_bytes.push(body.to_vec());
                    }
                }
                0x13 | 0x1b | 0x1c => {
                    if let Some(file) = entry.as_deref_mut() {
                        let kind_name = match kind {
                            0x13 => "DXE_DEPEX",
                            0x1b => "PEI_DEPEX",
                            _ => "SMM_DEPEX",
                        };
                        let mut depex = DepexEntry {
                            kind: kind_name.to_owned(),
                            bytes: body.len(),
                            sha256: hex_digest(body),
                            expression: None,
                            text: None,
                            error: None,
                            sor: false,
                            scheduling: Vec::new(),
                        };
                        match uefi_depex::decode(body) {
                            Ok(expression) => {
                                depex.text = Some(expression.display(&self.names));
                                depex.sor = expression.has_sor();
                                depex.scheduling = expression
                                    .scheduling()
                                    .into_iter()
                                    .map(|(order, guid)| ScheduleNote {
                                        order: match order {
                                            uefi_depex::SchedulingOrder::Before => "before",
                                            uefi_depex::SchedulingOrder::After => "after",
                                        },
                                        name: self.names.get(&guid).cloned(),
                                        guid,
                                    })
                                    .collect();
                                depex.expression = Some(expression);
                            }
                            Err(error) => {
                                depex.error = Some(format!("{error:#}"));
                                self.diagnostics.push(format!(
                                    "Cannot decode the dependency section of {label}: {error:#}"
                                ));
                            }
                        }
                        if kind == 0x13 || !is_dispatched_driver(file.ffs_type) {
                            file.depex_guid_bytes = raw_depex_guids(body);
                            file.depex = Some(depex);
                        }
                    }
                }
                0x14 => {
                    if let Some(file) = entry.as_deref_mut() {
                        file.version = utf16_text(body);
                    }
                }
                0x15 => {
                    if let Some(file) = entry.as_deref_mut() {
                        if let Some(name) = utf16_text(body) {
                            file.name = Some(name);
                        }
                    }
                }
                0x17 => {
                    self.walk_buffer(body, &format!("{label} volume image"), depth + 1);
                }
                0x18 => {
                    if let Some(file) = entry.as_deref_mut() {
                        if file.guid == APRIORI_GUID && body.len() > 16 {
                            self.capture_apriori(file.volume, &body[16..]);
                        }
                    }
                }
                0x19 => {
                    if let Some(file) = entry.as_deref_mut() {
                        if file.guid == APRIORI_GUID {
                            self.capture_apriori(file.volume, body);
                        }
                    }
                }
                _ => {}
            }
            offset = align4(offset + size);
        }
    }

    fn capture_apriori(&mut self, volume: usize, body: &[u8]) {
        for chunk in body.chunks_exact(16) {
            if chunk.iter().all(|byte| *byte == 0 || *byte == 0xff) {
                continue;
            }
            if let Ok(guid) = uefi_depex::guid(chunk) {
                self.volumes[volume].apriori.push(guid.clone());
                self.apriori.push(guid);
            }
        }
    }

    fn handle_guided(
        &mut self,
        data: &[u8],
        start: usize,
        header_size: usize,
        label: &str,
        depth: usize,
        entry: Option<&mut FileEntry>,
    ) {
        let guid_at = start + header_size;
        if guid_at + 20 > data.len() {
            self.diagnostics
                .push(format!("A guided section at {start:#x} is truncated."));
            return;
        }
        let guid = match uefi_depex::guid(&data[guid_at..guid_at + 16]) {
            Ok(value) => value,
            Err(error) => {
                self.diagnostics.push(format!(
                    "A guided section at {start:#x} has no GUID: {error:#}"
                ));
                return;
            }
        };
        let data_offset = read_u16(data, guid_at + 16).unwrap_or(0) as usize;
        let attributes = read_u16(data, guid_at + 18).unwrap_or(0);
        let section_size = read_u24(data, start) as usize;
        let end = if section_size == 0xff_ffff {
            start
                .saturating_add(read_u32(data, start + 4).unwrap_or(0) as usize)
                .min(data.len())
        } else {
            start.saturating_add(section_size).min(data.len())
        };
        match guid.as_str() {
            LZMA_GUID => {
                if attributes & 0x0001 == 0 {
                    self.diagnostics.push(format!(
                        "The LZMA guided section at {start:#x} has no processing attribute."
                    ));
                    return;
                }
                let mut candidates = Vec::new();
                let direct = start.saturating_add(data_offset);
                if direct < end {
                    candidates.push(direct);
                }
                let alternative = guid_at.saturating_add(20).saturating_add(data_offset);
                if alternative < end && !candidates.contains(&alternative) {
                    candidates.push(alternative);
                }
                if let Some(scan) = find_lzma_header(&data[guid_at..end]) {
                    let scanned = guid_at + scan;
                    if scanned < end && !candidates.contains(&scanned) {
                        candidates.push(scanned);
                    }
                }
                let mut stream = None;
                for candidate in &candidates {
                    match observer::decompress(&data[*candidate..end]) {
                        Ok(bytes) => {
                            stream = Some(bytes);
                            break;
                        }
                        Err(error) => self.diagnostics.push(format!(
                            "An LZMA decode attempt at {candidate:#x} failed: {error:#}"
                        )),
                    }
                }
                let Some(bytes) = stream else {
                    self.diagnostics.push(format!(
                        "No LZMA stream decodes in the guided section at {start:#x}."
                    ));
                    return;
                };
                self.decoded_bytes = self.decoded_bytes.saturating_add(bytes.len());
                if self.decoded_bytes > MAX_DECODED_BYTES {
                    self.diagnostics.push(format!(
                        "The decoded total passed the {MAX_DECODED_BYTES} byte limit."
                    ));
                    return;
                }
                self.walk_buffer(
                    &bytes,
                    &format!("{label} lzma section at {start:#x}"),
                    depth + 1,
                );
            }
            CRC32_GUID => {
                let candidate = start.saturating_add(data_offset);
                if candidate >= end {
                    self.diagnostics
                        .push(format!("The CRC32 section at {start:#x} is truncated."));
                    return;
                }
                let stored = read_u32(data, candidate.saturating_sub(4)).unwrap_or(0);
                let payload = &data[candidate..end];
                let computed = crc32fast::hash(payload);
                if stored != computed {
                    self.diagnostics.push(format!(
                        "The CRC32 guided section at {start:#x} stores {stored:#x} and holds {computed:#x}."
                    ));
                }
                self.walk_sections(payload, 0, label, depth + 1, entry);
            }
            _ => {
                self.diagnostics.push(format!(
                    "The guided section {guid} at {start:#x} in {label} is not decoded."
                ));
            }
        }
    }

    fn finish(&mut self, image: &[u8], source: &str, member: Option<String>) -> Report {
        let mut seen = BTreeSet::new();
        self.apriori.retain(|guid| seen.insert(guid.clone()));
        for file in self.files.iter_mut() {
            file.apriori_index = self.volumes[file.volume]
                .apriori
                .iter()
                .position(|guid| *guid == file.guid);
        }
        let names = self.names.clone();
        let base: BTreeSet<String> = BASE_PROTOCOLS
            .iter()
            .map(|guid| (*guid).to_owned())
            .collect();
        let references = collect_references(&self.files);
        let (protocols, dispatch) =
            analyse_dispatch(&self.files, &self.volumes, &base, &names, &references);

        let mut used: BTreeSet<String> = self.apriori.iter().cloned().collect();
        for volume in &self.volumes {
            used.insert(volume.file_system_guid.clone());
        }
        for file in &self.files {
            used.insert(file.guid.clone());
            if let Some(depex) = &file.depex {
                if let Some(expression) = &depex.expression {
                    used.extend(expression.guids());
                }
                for note in &depex.scheduling {
                    used.insert(note.guid.clone());
                }
            }
        }
        used.extend(protocols.keys().cloned());
        let guid_names: BTreeMap<String, String> = used
            .into_iter()
            .filter_map(|guid| names.get(&guid).map(|name| (guid, name.clone())))
            .collect();

        Report {
            schema: 1,
            image: source.to_owned(),
            image_bytes: image.len() as u64,
            image_sha256: hex_digest(image),
            member,
            volumes: std::mem::take(&mut self.volumes),
            files: std::mem::take(&mut self.files),
            apriori: self.apriori.clone(),
            guid_names,
            assumed_base_protocols: BASE_PROTOCOLS
                .iter()
                .filter_map(|guid| {
                    names
                        .get(*guid)
                        .map(|name| ((*guid).to_owned(), name.clone()))
                })
                .collect(),
            protocols,
            dispatch,
            logs: Vec::new(),
            diagnostics: std::mem::take(&mut self.diagnostics),
            limits: Limits {
                max_image_bytes: MAX_IMAGE_BYTES,
                max_decoded_bytes: MAX_DECODED_BYTES,
                max_depth: MAX_DEPTH,
            },
        }
    }
}

struct FvHeader {
    length: u64,
    header_length: u16,
    revision: u16,
    attributes: u32,
    file_system_guid: [u8; 16],
    checksum_ok: bool,
}

fn valid_fv(data: &[u8], start: usize) -> Option<FvHeader> {
    if start + 0x38 > data.len() {
        return None;
    }
    if &data[start + 0x28..start + 0x2c] != b"_FVH" {
        return None;
    }
    let mut file_system_guid = [0u8; 16];
    file_system_guid.copy_from_slice(&data[start + 0x10..start + 0x20]);
    if file_system_guid
        .iter()
        .all(|byte| *byte == 0 || *byte == 0xff)
    {
        return None;
    }
    let length = read_u64(data, start + 0x20)?;
    if length < 0x48 || length > (data.len() - start) as u64 {
        return None;
    }
    let header_length = read_u16(data, start + 0x30)?;
    if header_length < 0x38 || header_length as u64 > length {
        return None;
    }
    let revision = *data.get(start + 0x37)? as u16;
    if revision != 2 {
        return None;
    }
    let attributes = read_u32(data, start + 0x2c)?;
    let header = &data[start..start + header_length as usize];
    let mut sum: u16 = 0;
    for chunk in header.chunks_exact(2) {
        sum = sum.wrapping_add(u16::from_le_bytes([chunk[0], chunk[1]]));
    }
    Some(FvHeader {
        length,
        header_length,
        revision,
        attributes,
        file_system_guid,
        checksum_ok: sum == 0,
    })
}

fn section_header(data: &[u8], offset: usize) -> Option<(usize, u8, usize)> {
    if offset + 4 > data.len() {
        return None;
    }
    let mut size = read_u24(data, offset) as usize;
    let kind = data[offset + 3];
    let mut header = 4;
    if size == 0xff_ffff {
        size = read_u32(data, offset + 4)? as usize;
        header = 8;
    }
    if size < header {
        return None;
    }
    Some((size, kind, header))
}

fn known_section(kind: u8) -> bool {
    matches!(kind, 0x01..=0x03 | 0x10..=0x19 | 0x1b | 0x1c)
}

fn section_kind_name(kind: u8) -> &'static str {
    match kind {
        0x01 => "COMPRESSION",
        0x02 => "GUID_DEFINED",
        0x03 => "DISPOSABLE",
        0x10 => "PE32",
        0x11 => "PIC",
        0x12 => "TE",
        0x13 => "DXE_DEPEX",
        0x14 => "VERSION",
        0x15 => "UI",
        0x16 => "COMPATIBILITY16",
        0x17 => "FIRMWARE_VOLUME_IMAGE",
        0x18 => "FREEFORM_SUBTYPE_GUID",
        0x19 => "RAW",
        0x1b => "PEI_DEPEX",
        0x1c => "SMM_DEPEX",
        _ => "UNKNOWN",
    }
}

fn ffs_type_name(kind: u8) -> &'static str {
    match kind {
        0x01 => "RAW",
        0x02 => "FREEFORM",
        0x03 => "SECURITY_CORE",
        0x04 => "PEI_CORE",
        0x05 => "DXE_CORE",
        0x06 => "PEIM",
        0x07 => "DXE_DRIVER",
        0x08 => "COMBINED_PEIM_DRIVER",
        0x09 => "APPLICATION",
        0x0a => "SMM",
        0x0b => "FIRMWARE_VOLUME_IMAGE",
        0x0c => "COMBINED_SMM_DXE",
        0x0d => "SMM_CORE",
        0x0e => "MM_STANDALONE",
        0x0f => "MM_CORE_STANDALONE",
        0xf0 => "PAD",
        _ => "UNKNOWN",
    }
}

fn image_machine(kind: u8, body: &[u8]) -> Option<u16> {
    match kind {
        0x12 => (body.len() >= 4).then(|| u16::from_le_bytes([body[2], body[3]])),
        0x10 => {
            if body.len() < 0x40 {
                return None;
            }
            let offset =
                u32::from_le_bytes([body[0x3c], body[0x3d], body[0x3e], body[0x3f]]) as usize;
            if offset + 6 > body.len() || &body[offset..offset + 4] != b"PE\0\0" {
                return None;
            }
            Some(u16::from_le_bytes([body[offset + 4], body[offset + 5]]))
        }
        _ => None,
    }
}

fn find_from(data: &[u8], pattern: &[u8], start: usize) -> Option<usize> {
    if start >= data.len() || pattern.is_empty() || data.len() - start < pattern.len() {
        return None;
    }
    data[start..]
        .windows(pattern.len())
        .position(|window| window == pattern)
        .map(|position| position + start)
}

fn find_all(data: &[u8], pattern: &[u8]) -> Vec<usize> {
    let mut offsets = Vec::new();
    let mut cursor = 0;
    while let Some(position) = find_from(data, pattern, cursor) {
        offsets.push(position);
        cursor = position + 1;
    }
    offsets
}

fn is_guided_header(data: &[u8], start: usize, header_size: usize) -> bool {
    let Some((size, kind, _)) = section_header(data, start) else {
        return false;
    };
    if kind != 0x02 {
        return false;
    }
    if header_size == 8 && size != 0xff_ffff {
        return false;
    }
    if start + size > data.len() {
        return false;
    }
    start + header_size + 20 <= data.len()
}

fn find_lzma_header(data: &[u8]) -> Option<usize> {
    let window = data.len().min(64);
    for offset in 0..window {
        if data[offset] != 0x5d {
            continue;
        }
        if offset + 13 > data.len() {
            continue;
        }
        let dictionary = u32::from_le_bytes([
            data[offset + 1],
            data[offset + 2],
            data[offset + 3],
            data[offset + 4],
        ]);
        if !(1u32 << 12..=1u32 << 30).contains(&dictionary) {
            continue;
        }
        let size = u64::from_le_bytes([
            data[offset + 5],
            data[offset + 6],
            data[offset + 7],
            data[offset + 8],
            data[offset + 9],
            data[offset + 10],
            data[offset + 11],
            data[offset + 12],
        ]);
        if size == 0 || size > (1 << 32) {
            continue;
        }
        return Some(offset);
    }
    None
}

fn raw_depex_guids(bytes: &[u8]) -> Vec<[u8; 16]> {
    let mut values = Vec::new();
    let mut cursor = 0;
    while cursor < bytes.len() {
        match bytes[cursor] {
            0x00..=0x02 => {
                if cursor + 17 > bytes.len() {
                    break;
                }
                let mut raw = [0u8; 16];
                raw.copy_from_slice(&bytes[cursor + 1..cursor + 17]);
                if raw.iter().any(|byte| *byte != 0) {
                    values.push(raw);
                }
                cursor += 17;
            }
            _ => cursor += 1,
        }
    }
    values.sort();
    values.dedup();
    values
}

fn collect_references(files: &[FileEntry]) -> BTreeMap<usize, BTreeSet<String>> {
    let mut patterns: HashSet<[u8; 16]> = HashSet::new();
    let mut text: BTreeMap<[u8; 16], String> = BTreeMap::new();
    for guid in ARCH_PROTOCOLS {
        let raw = guid_raw(guid).expect("Architectural protocol GUID");
        patterns.insert(raw);
        text.insert(raw, (*guid).to_owned());
    }
    for file in files {
        if file
            .depex
            .as_ref()
            .is_some_and(|depex| !depex.scheduling.is_empty())
        {
            continue;
        }
        for raw in &file.depex_guid_bytes {
            patterns.insert(*raw);
            if let Ok(value) = uefi_depex::guid(raw) {
                text.insert(*raw, value);
            }
        }
    }
    let mut references = BTreeMap::new();
    if patterns.is_empty() {
        return references;
    }
    for (index, file) in files.iter().enumerate() {
        let mut found = BTreeSet::new();
        for bytes in &file.image_bytes {
            if bytes.len() < 16 {
                continue;
            }
            for window in bytes.windows(16) {
                let mut key = [0u8; 16];
                key.copy_from_slice(window);
                if patterns.contains(&key) {
                    if let Some(value) = text.get(&key) {
                        found.insert(value.clone());
                    }
                }
            }
        }
        if !found.is_empty() {
            references.insert(index, found);
        }
    }
    references
}

fn is_dispatched_driver(kind: u8) -> bool {
    matches!(kind, 0x07 | 0x08 | 0x0c)
}

fn schedule_driver(
    index: usize,
    files: &[FileEntry],
    scheduled: &mut BTreeSet<usize>,
    batch: &mut Vec<usize>,
) {
    if !scheduled.insert(index) {
        return;
    }
    // Scheduling operands name FFS files, not installed protocols.
    for (before, file) in files.iter().enumerate() {
        if !is_dispatched_driver(file.ffs_type) {
            continue;
        }
        if matches!(file.depex.as_ref().and_then(|d| d.expression.as_ref()),
            Some(Expr::Before(target)) if target == &files[index].guid)
        {
            schedule_driver(before, files, scheduled, batch);
        }
    }
    batch.push(index);
    for (after, file) in files.iter().enumerate() {
        if !is_dispatched_driver(file.ffs_type) {
            continue;
        }
        if matches!(file.depex.as_ref().and_then(|d| d.expression.as_ref()),
            Some(Expr::After(target)) if target == &files[index].guid)
        {
            schedule_driver(after, files, scheduled, batch);
        }
    }
}

fn analyse_dispatch(
    files: &[FileEntry],
    volumes: &[VolumeEntry],
    base: &BTreeSet<String>,
    names: &BTreeMap<String, String>,
    references: &BTreeMap<usize, BTreeSet<String>>,
) -> (BTreeMap<String, ProtocolReport>, DispatchReport) {
    let mut available: BTreeSet<String> = base.clone();
    let mut dispatched: BTreeSet<usize> = BTreeSet::new();
    let mut order: Vec<usize> = Vec::new();

    // APRIORI bypasses DEPEX within its own volume.
    for volume in volumes {
        for guid in &volume.apriori {
            if let Some(index) = files.iter().position(|file| {
                file.volume == volume.index
                    && file.guid == *guid
                    && is_dispatched_driver(file.ffs_type)
            }) {
                if dispatched.insert(index) {
                    order.push(index);
                    if let Some(values) = references.get(&index) {
                        available.extend(values.iter().cloned());
                    }
                }
            }
        }
    }
    loop {
        let mut batch = Vec::new();
        for (index, file) in files.iter().enumerate() {
            if !is_dispatched_driver(file.ffs_type) || dispatched.contains(&index) {
                continue;
            }
            let satisfied = match &file.depex {
                None => ARCH_PROTOCOLS.iter().all(|guid| available.contains(*guid)),
                Some(depex) => match &depex.expression {
                    Some(Expr::Before(_) | Expr::After(_) | Expr::Sor(_)) => false,
                    Some(expression) => expression.evaluate(&available),
                    None => false,
                },
            };
            if satisfied {
                schedule_driver(index, files, &mut dispatched, &mut batch);
            }
        }
        if batch.is_empty() {
            break;
        }
        // DxeCore schedules a batch before any driver in that batch executes.
        for index in batch {
            order.push(index);
            if let Some(values) = references.get(&index) {
                available.extend(values.iter().cloned());
            }
        }
    }

    let mut protocols: BTreeMap<String, ProtocolReport> = BTreeMap::new();
    for file in files {
        if let Some(depex) = &file.depex {
            if let Some(expression) = &depex.expression {
                if !depex.scheduling.is_empty() {
                    continue;
                }
                for guid in expression.guids() {
                    let report = protocols
                        .entry(guid.clone())
                        .or_insert_with(|| ProtocolReport {
                            name: names.get(&guid).cloned(),
                            note: REFERENCE_NOTE,
                            ..ProtocolReport::default()
                        });
                    report.depended_by.push(file.label.clone());
                }
            }
        }
    }
    for (index, values) in references {
        for guid in values {
            let report = protocols
                .entry(guid.clone())
                .or_insert_with(|| ProtocolReport {
                    name: names.get(guid).cloned(),
                    note: REFERENCE_NOTE,
                    ..ProtocolReport::default()
                });
            report
                .reference_candidates
                .push(files[*index].label.clone());
            if dispatched.contains(index) {
                report
                    .dispatched_reference_candidates
                    .push(files[*index].label.clone());
            }
        }
    }
    for (guid, report) in &mut protocols {
        report.available = available.contains(guid);
        report.from_base_protocols = base.contains(guid);
        report.depended_by.sort();
        report.depended_by.dedup();
        report.reference_candidates.sort();
        report.reference_candidates.dedup();
        report.dispatched_reference_candidates.sort();
        report.dispatched_reference_candidates.dedup();
    }

    let mut steps = Vec::new();
    for index in &order {
        let file = &files[*index];
        steps.push(DispatchStep {
            guid: file.guid.clone(),
            label: file.label.clone(),
            apriori_index: file.apriori_index,
            depex: file
                .depex
                .as_ref()
                .and_then(|depex| depex.text.clone())
                .unwrap_or_else(|| "All architectural protocols (no DEPEX)".to_owned()),
        });
    }

    let mut never = Vec::new();
    for (index, file) in files.iter().enumerate() {
        if !is_dispatched_driver(file.ffs_type) || dispatched.contains(&index) {
            continue;
        }
        let depex = file.depex.as_ref();
        let text = depex.and_then(|value| value.text.clone());
        let mut unsatisfied = Vec::new();
        let mut reason = match depex {
            Some(value) if value.error.is_some() => format!(
                "The dependency expression is invalid: {}",
                value.error.clone().unwrap_or_default()
            ),
            Some(value) if value.expression.is_none() => {
                "The dependency expression is absent after a decode error.".to_owned()
            }
            _ => "Dependencies are not satisfied at the fixed point.".to_owned(),
        };
        if let Some(expression) = depex.and_then(|value| value.expression.as_ref()) {
            if matches!(expression, Expr::False) {
                reason = "The dependency expression is FALSE.".to_owned();
            }
            if expression.has_sor() {
                reason.push_str(
                    " SOR requires an explicit driver request, which this map does not assume.",
                );
            }
            for guid in expression.guids() {
                if !expression.scheduling().is_empty() {
                    reason = "The target FFS file was not scheduled through dependency evaluation."
                        .to_owned();
                    break;
                }
                if available.contains(&guid) {
                    continue;
                }
                let report = protocols.get(&guid);
                unsatisfied.push(Unsatisfied {
                    guid: guid.clone(),
                    name: names.get(&guid).cloned(),
                    reference_candidates: report
                        .map(|value| value.reference_candidates.clone())
                        .unwrap_or_default(),
                    dispatched_candidates: report
                        .map(|value| value.dispatched_reference_candidates.clone())
                        .unwrap_or_default(),
                });
            }
        }
        if depex.is_none() {
            for guid in ARCH_PROTOCOLS
                .iter()
                .filter(|guid| !available.contains(**guid))
            {
                let report = protocols.get(*guid);
                unsatisfied.push(Unsatisfied {
                    guid: (*guid).to_owned(),
                    name: names.get(*guid).cloned(),
                    reference_candidates: report
                        .map(|p| p.reference_candidates.clone())
                        .unwrap_or_default(),
                    dispatched_candidates: report
                        .map(|p| p.dispatched_reference_candidates.clone())
                        .unwrap_or_default(),
                });
            }
            reason = "A driver without DEPEX requires all architectural protocols.".to_owned();
        }
        never.push(NeverDispatched {
            guid: file.guid.clone(),
            label: file.label.clone(),
            depex: text,
            reason,
            unsatisfied,
            scheduling: depex
                .map(|value| value.scheduling.clone())
                .unwrap_or_default(),
        });
    }

    (
        protocols,
        DispatchReport {
            order: steps,
            never_dispatched: never,
            solver: "Static fixpoint. A protocol becomes available when a dispatched candidate producer references it. The result is an over-approximation.",
        },
    )
}

fn correlate_log(path: &Path, report: &Report) -> Result<LogReport> {
    let compressed = path
        .extension()
        .and_then(|value| value.to_str())
        .map(|value| value.eq_ignore_ascii_case("zst"))
        .unwrap_or(false);
    let reader: Box<dyn BufRead> = if compressed {
        let mut child = Command::new("zstd")
            .arg("-dc")
            .arg(path)
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn()
            .with_context(|| format!("Cannot start zstd for {}", path.display()))?;
        let stdout = child.stdout.take().context("zstd has no output pipe")?;
        Box::new(BufReader::new(stdout.take(MAX_LOG_BYTES + 1)))
    } else {
        Box::new(BufReader::new(File::open(path)?))
    };
    let mut loads = Vec::new();
    let mut installs: BTreeMap<String, usize> = BTreeMap::new();
    let mut lines = 0u64;
    let mut bytes = 0u64;
    let mut truncated = false;
    for line in reader.lines() {
        let line = line?;
        lines += 1;
        bytes = bytes.saturating_add(line.len() as u64 + 1);
        if bytes > MAX_LOG_BYTES {
            truncated = true;
            break;
        }
        if let Some(load) = parse_load(&line, lines) {
            if loads.len() < MAX_LOADS_PER_LOG {
                loads.push(load);
            }
        }
        if let Some(guid) = parse_install(&line) {
            *installs.entry(guid).or_default() += 1;
        }
    }

    let static_order: Vec<String> = report
        .dispatch
        .order
        .iter()
        .map(|step| normalize_name(&step.label))
        .collect();
    let observed: Vec<String> = loads
        .iter()
        .map(|load| normalize_name(&load.name))
        .collect();
    let static_set: BTreeSet<&String> = static_order.iter().collect();
    let observed_set: BTreeSet<&String> = observed.iter().collect();
    let mut loaded_but_not_mapped = Vec::new();
    for name in &observed {
        if !static_set.contains(name) {
            loaded_but_not_mapped.push(name.clone());
        }
    }
    loaded_but_not_mapped.sort();
    loaded_but_not_mapped.dedup();
    let mut mapped_but_not_loaded = Vec::new();
    for name in &static_order {
        if !observed_set.contains(name) {
            mapped_but_not_loaded.push(name.clone());
        }
    }
    mapped_but_not_loaded.sort();
    mapped_but_not_loaded.dedup();

    let first_observed: Vec<String> = {
        let mut seen = BTreeSet::new();
        observed
            .iter()
            .filter(|name| static_set.contains(*name) && seen.insert((*name).clone()))
            .cloned()
            .collect()
    };
    let first_static: Vec<String> = {
        let mut seen = BTreeSet::new();
        static_order
            .iter()
            .filter(|name| observed_set.contains(*name) && seen.insert((*name).clone()))
            .cloned()
            .collect()
    };
    let mut mismatches = Vec::new();
    for (left, right) in first_observed.iter().zip(first_static.iter()) {
        if left != right {
            mismatches.push(OrderMismatch {
                first_in_static: right.clone(),
                first_in_observed: left.clone(),
                note: "The observed order differs from the static order at this position.",
            });
            if mismatches.len() >= 20 {
                break;
            }
        }
    }

    Ok(LogReport {
        path: path.display().to_string(),
        compression: if compressed { "zstd" } else { "plain" }.to_owned(),
        lines,
        truncated,
        loads: loads.len(),
        observed: loads,
        installs,
        loaded_but_not_mapped,
        mapped_but_not_loaded,
        order_mismatches: mismatches,
    })
}

fn parse_load(line: &str, number: u64) -> Option<ObservedLoad> {
    let position = line.find("Loading driver at ")?;
    let rest = &line[position + "Loading driver at ".len()..];
    let mut address = None;
    let mut entry_point = None;
    if let Some(stripped) = rest.strip_prefix("0x").or_else(|| rest.strip_prefix("0X")) {
        let end = stripped
            .find(|value: char| !value.is_ascii_hexdigit())
            .unwrap_or(stripped.len());
        address = Some(stripped[..end].to_owned());
    }
    let remainder = if let Some(position) = rest.find("EntryPoint=") {
        let value = &rest[position + "EntryPoint=".len()..];
        let value = value
            .strip_prefix("0x")
            .or_else(|| value.strip_prefix("0X"))
            .unwrap_or(value);
        let end = value
            .find(|character: char| !character.is_ascii_hexdigit())
            .unwrap_or(value.len());
        entry_point = Some(value[..end].to_owned());
        value[end..].trim()
    } else {
        rest
    };
    let name = remainder.split_whitespace().last()?.trim();
    if name.is_empty() {
        return None;
    }
    Some(ObservedLoad {
        line: number,
        name: name.to_owned(),
        address,
        entry_point,
    })
}

fn parse_install(line: &str) -> Option<String> {
    for marker in ["InstallProtocolInterface: ", "ReinstallProtocolInterface: "] {
        if let Some(position) = line.find(marker) {
            let rest = &line[position + marker.len()..];
            let token = rest.split_whitespace().next()?;
            let normalized = token.trim().to_ascii_lowercase();
            if normalized.len() == 36 {
                return Some(normalized);
            }
        }
    }
    None
}

fn normalize_name(name: &str) -> String {
    let base = name
        .rsplit(['/', '\\'])
        .next()
        .unwrap_or(name)
        .trim()
        .to_ascii_lowercase();
    let base = base.strip_suffix(".efi").unwrap_or(&base);
    let mut value = String::new();
    for character in base.chars() {
        if character.is_ascii_alphanumeric() {
            value.push(character);
        }
    }
    value
}

fn apply_observed_installs(report: &mut Report) {
    for log in &report.logs {
        for (guid, count) in &log.installs {
            if let Some(protocol) = report.protocols.get_mut(guid) {
                protocol.observed_installs += *count;
            }
        }
    }
}

fn render_dot(report: &Report) -> String {
    let mut dot = String::new();
    dot.push_str("digraph uefi_map {\n  rankdir=LR;\n  graph [fontname=\"sans\"];\n");
    dot.push_str("  node [fontname=\"sans\", shape=box, style=rounded];\n");
    for (index, step) in report.dispatch.order.iter().enumerate() {
        let _ = writeln!(
            dot,
            "  \"file:{}\" [label=\"{}\\n{}\"];",
            escape(&step.guid),
            escape(&step.label),
            index
        );
    }
    for never in &report.dispatch.never_dispatched {
        let _ = writeln!(
            dot,
            "  \"file:{}\" [label=\"{}\\nnever dispatched\", color=\"#b00020\", fontcolor=\"#b00020\"];",
            escape(&never.guid),
            escape(&never.label)
        );
    }
    for (guid, protocol) in &report.protocols {
        let label = protocol.name.clone().unwrap_or_else(|| guid.clone());
        let color = if protocol.available {
            "#0b6e0b"
        } else {
            "#8a6d00"
        };
        let _ = writeln!(
            dot,
            "  \"proto:{}\" [label=\"{}\", shape=ellipse, color=\"{}\"];",
            escape(guid),
            escape(&label),
            color
        );
    }
    for (guid, protocol) in &report.protocols {
        let mut depender = BTreeSet::new();
        for file in &protocol.depended_by {
            if !depender.insert(file.clone()) {
                continue;
            }
            if let Some(owner) = report
                .files
                .iter()
                .find(|candidate| candidate.label == *file)
            {
                let _ = writeln!(
                    dot,
                    "  \"file:{}\" -> \"proto:{}\" [style=dashed, label=\"DEPEX\"];",
                    escape(&owner.guid),
                    escape(guid)
                );
            }
        }
        for file in &protocol.reference_candidates {
            if let Some(owner) = report
                .files
                .iter()
                .find(|candidate| candidate.label == *file)
            {
                let _ = writeln!(
                    dot,
                    "  \"file:{}\" -> \"proto:{}\" [style=solid, color=\"#777777\", label=\"candidate\"];",
                    escape(&owner.guid),
                    escape(guid)
                );
            }
        }
    }
    dot.push_str("}\n");
    dot
}

fn escape(value: &str) -> String {
    value.replace('\\', "\\\\").replace('"', "\\\"")
}

fn escape_markdown(value: &str) -> String {
    value.replace('|', "\\|")
}

fn render_markdown(report: &Report) -> String {
    let mut text = String::new();
    let _ = writeln!(text, "# UEFI map of `{}`\n", report.image);
    let _ = writeln!(
        text,
        "Image SHA-256: `{}`. Size: {} bytes.\n",
        report.image_sha256, report.image_bytes
    );
    let _ = writeln!(
        text,
        "The map holds {} firmware volumes and {} firmware files. The static solver dispatched {} drivers and left {} drivers undispatched.\n",
        report.volumes.len(),
        report.files.len(),
        report.dispatch.order.len(),
        report.dispatch.never_dispatched.len()
    );

    let _ = writeln!(text, "## Firmware volumes\n");
    let _ = writeln!(
        text,
        "| # | Buffer | Depth | Offset | Length | Header | File system | Files |"
    );
    let _ = writeln!(text, "| --- | --- | --- | --- | --- | --- | --- | --- |");
    for volume in &report.volumes {
        let _ = writeln!(
            text,
            "| {} | {} | {} | {:#x} | {:#x} | {:#x} | {} | {} |",
            volume.index,
            volume.buffer,
            volume.depth,
            volume.offset,
            volume.length,
            volume.header_length,
            volume
                .file_system_name
                .clone()
                .unwrap_or_else(|| volume.file_system_guid.clone()),
            volume.file_count
        );
    }

    let _ = writeln!(text, "\n## Firmware files\n");
    let _ = writeln!(
        text,
        "| File | GUID | Type | UI name | Version | Images | DEPEX | APRIORI |"
    );
    let _ = writeln!(text, "| --- | --- | --- | --- | --- | --- | --- | --- |");
    for file in &report.files {
        let images = if file.images.is_empty() {
            "-".to_owned()
        } else {
            file.images
                .iter()
                .map(|image| {
                    format!(
                        "{} {} bytes `{}`",
                        image.kind,
                        image.size,
                        &image.sha256[..12.min(image.sha256.len())]
                    )
                })
                .collect::<Vec<String>>()
                .join(", ")
        };
        let _ = writeln!(
            text,
            "| {} | `{}` | {} | {} | {} | {} | {} | {} |",
            escape_markdown(&file.label),
            file.guid,
            file.ffs_type_name,
            file.name.clone().unwrap_or_else(|| "-".to_owned()),
            file.version.clone().unwrap_or_else(|| "-".to_owned()),
            images,
            file.depex
                .as_ref()
                .and_then(|depex| depex.text.clone())
                .unwrap_or_else(|| "-".to_owned())
                .replace('|', "\\|"),
            file.apriori_index
                .map(|value| (value + 1).to_string())
                .unwrap_or_else(|| "-".to_owned())
        );
    }

    let _ = writeln!(text, "\n## DXE APRIORI order\n");
    if report.apriori.is_empty() {
        let _ = writeln!(text, "The image has no APRIORI file.\n");
    } else {
        for (index, guid) in report.apriori.iter().enumerate() {
            let name = report
                .files
                .iter()
                .find(|file| file.guid == *guid)
                .map(|file| file.label.clone())
                .unwrap_or_else(|| "absent from this image".to_owned());
            let _ = writeln!(text, "{}. {} ({})", index + 1, name, guid);
        }
        let _ = writeln!(text);
    }

    let _ = writeln!(text, "## Static dispatch order\n");
    let _ = writeln!(text, "| # | Driver | APRIORI | Dependency |");
    let _ = writeln!(text, "| --- | --- | --- | --- |");
    for (index, step) in report.dispatch.order.iter().enumerate() {
        let _ = writeln!(
            text,
            "| {} | {} | {} | `{}` |",
            index + 1,
            step.label,
            step.apriori_index
                .map(|value| (value + 1).to_string())
                .unwrap_or_else(|| "-".to_owned()),
            step.depex
        );
    }

    let _ = writeln!(text, "\n## Drivers that never dispatch\n");
    if report.dispatch.never_dispatched.is_empty() {
        let _ = writeln!(text, "No driver is left undispatched.\n");
    } else {
        for driver in &report.dispatch.never_dispatched {
            let _ = writeln!(text, "### {}\n", driver.label);
            let _ = writeln!(text, "GUID: `{}`\n", driver.guid);
            let _ = writeln!(
                text,
                "Dependency: `{}`\n",
                driver
                    .depex
                    .clone()
                    .unwrap_or_else(|| "All architectural protocols (no DEPEX)".to_owned())
            );
            let _ = writeln!(text, "Reason: {}\n", driver.reason);
            if driver.unsatisfied.is_empty() {
                let _ = writeln!(text, "No single protocol GUID is unsatisfied.\n");
            } else {
                let _ = writeln!(
                    text,
                    "| Unsatisfied GUID | Name | Reference candidates | Dispatched candidates |"
                );
                let _ = writeln!(text, "| --- | --- | --- | --- |");
                for value in &driver.unsatisfied {
                    let _ = writeln!(
                        text,
                        "| `{}` | {} | {} | {} |",
                        value.guid,
                        value.name.clone().unwrap_or_else(|| "-".to_owned()),
                        join_or_dash(&value.reference_candidates),
                        join_or_dash(&value.dispatched_candidates)
                    );
                }
                let _ = writeln!(text);
            }
        }
    }

    let _ = writeln!(text, "## Protocol dependencies\n");
    let _ = writeln!(
        text,
        "A reference candidate contains the GUID in its image. It is not a proven producer.\n"
    );
    let _ = writeln!(
        text,
        "| Protocol | Available | Base | DEPEX consumers | Reference candidates | Observed installs |"
    );
    let _ = writeln!(text, "| --- | --- | --- | --- | --- | --- |");
    for (guid, protocol) in &report.protocols {
        let _ = writeln!(
            text,
            "| `{}` {} | {} | {} | {} | {} | {} |",
            guid,
            protocol.name.clone().unwrap_or_default(),
            if protocol.available { "yes" } else { "no" },
            if protocol.from_base_protocols {
                "yes"
            } else {
                "no"
            },
            join_or_dash(&protocol.depended_by),
            join_or_dash(&protocol.reference_candidates),
            protocol.observed_installs
        );
    }

    if !report.logs.is_empty() {
        let _ = writeln!(text, "\n## Log correlation\n");
        for log in &report.logs {
            let _ = writeln!(text, "### `{}`\n", log.path);
            let _ = writeln!(
                text,
                "Format: {}. Lines: {}. Observed loads: {}. Truncated: {}.\n",
                log.compression, log.lines, log.loads, log.truncated
            );
            let _ = writeln!(
                text,
                "Installed protocols in the log: {}\n",
                log.installs.len()
            );
            for (guid, count) in &log.installs {
                let name = report
                    .protocols
                    .get(guid)
                    .and_then(|value| value.name.clone())
                    .unwrap_or_else(|| "-".to_owned());
                let _ = writeln!(text, "- `{}` {}: {}", guid, name, count);
            }
            if !log.loaded_but_not_mapped.is_empty() {
                let _ = writeln!(
                    text,
                    "\nLoaded but absent from the map: {}\n",
                    join_or_dash(&log.loaded_but_not_mapped)
                );
            }
            if !log.mapped_but_not_loaded.is_empty() {
                let _ = writeln!(
                    text,
                    "Mapped but absent from the log: {}\n",
                    join_or_dash(&log.mapped_but_not_loaded)
                );
            }
            if !log.order_mismatches.is_empty() {
                let _ = writeln!(text, "| Static first | Observed first | Note |");
                let _ = writeln!(text, "| --- | --- | --- |");
                for value in &log.order_mismatches {
                    let _ = writeln!(
                        text,
                        "| {} | {} | {} |",
                        value.first_in_static, value.first_in_observed, value.note
                    );
                }
                let _ = writeln!(text);
            }
        }
    }

    if !report.diagnostics.is_empty() {
        let _ = writeln!(text, "\n## Diagnostics\n");
        for value in &report.diagnostics {
            let _ = writeln!(text, "- {}", value);
        }
    }
    text
}

fn join_or_dash(values: &[String]) -> String {
    if values.is_empty() {
        "-".to_owned()
    } else {
        values.join(", ")
    }
}

fn read_u16(data: &[u8], offset: usize) -> Option<u16> {
    let slice = data.get(offset..offset + 2)?;
    Some(u16::from_le_bytes([slice[0], slice[1]]))
}

fn read_u24(data: &[u8], offset: usize) -> u32 {
    match data.get(offset..offset + 3) {
        Some(slice) => u32::from(slice[0]) | u32::from(slice[1]) << 8 | u32::from(slice[2]) << 16,
        None => 0,
    }
}

fn read_u32(data: &[u8], offset: usize) -> Option<u32> {
    let slice = data.get(offset..offset + 4)?;
    Some(u32::from_le_bytes([slice[0], slice[1], slice[2], slice[3]]))
}

fn read_u64(data: &[u8], offset: usize) -> Option<u64> {
    let slice = data.get(offset..offset + 8)?;
    Some(u64::from_le_bytes([
        slice[0], slice[1], slice[2], slice[3], slice[4], slice[5], slice[6], slice[7],
    ]))
}

fn align8(value: usize) -> usize {
    (value + 7) & !7
}

fn align4(value: usize) -> usize {
    (value + 3) & !3
}

fn hex_digest(bytes: &[u8]) -> String {
    let mut hasher = Sha256::new();
    hasher.update(bytes);
    format!("{:x}", hasher.finalize())
}

fn utf16_text(bytes: &[u8]) -> Option<String> {
    let values: Vec<u16> = bytes
        .chunks_exact(2)
        .map(|chunk| u16::from_le_bytes([chunk[0], chunk[1]]))
        .take_while(|value| *value != 0)
        .collect();
    if values.is_empty() {
        return None;
    }
    let text: String = char::decode_utf16(values)
        .map(|value| value.unwrap_or('\u{fffd}'))
        .collect();
    let text = text.trim().to_owned();
    (!text.is_empty()).then_some(text)
}

#[cfg(test)]
mod tests {
    use super::*;

    const FS_GUID: &str = "8c8ce578-8a3d-4f1c-9935-896185c32dd3";
    const GUID_A: &str = "11111111-2222-3333-4444-555555555555";
    const GUID_B: &str = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
    const DRIVER_1: &str = "10000000-2222-3333-4444-555555555555";
    const DRIVER_2: &str = "20000000-2222-3333-4444-555555555555";
    const DRIVER_3: &str = "30000000-2222-3333-4444-555555555555";
    const HOLDER: &str = "40000000-2222-3333-4444-555555555555";

    fn raw(guid: &str) -> [u8; 16] {
        guid_raw(guid).expect("The test GUID must parse")
    }

    fn u16s(text: &str) -> Vec<u8> {
        let mut bytes: Vec<u8> = text
            .encode_utf16()
            .flat_map(|value| value.to_le_bytes())
            .collect();
        bytes.extend_from_slice(&[0, 0]);
        bytes
    }

    fn section(kind: u8, body: &[u8]) -> Vec<u8> {
        let size = align4(4 + body.len());
        assert!(size < 0xff_ffff);
        let mut out = vec![0u8; size];
        out[0] = (size & 0xff) as u8;
        out[1] = ((size >> 8) & 0xff) as u8;
        out[2] = ((size >> 16) & 0xff) as u8;
        out[3] = kind;
        out[4..4 + body.len()].copy_from_slice(body);
        out
    }

    fn section_ext(kind: u8, body: &[u8]) -> Vec<u8> {
        let size = align4(8 + body.len());
        let mut out = vec![0u8; size];
        out[0] = 0xff;
        out[1] = 0xff;
        out[2] = 0xff;
        out[3] = kind;
        out[4..8].copy_from_slice(&(size as u32).to_le_bytes());
        out[8..8 + body.len()].copy_from_slice(body);
        out
    }

    fn depex(tokens: &[u8]) -> Vec<u8> {
        section(0x13, tokens)
    }

    fn push(guid: &str) -> Vec<u8> {
        let mut out = vec![0x02];
        out.extend_from_slice(&raw(guid));
        out
    }

    fn ffs(guid: &str, kind: u8, body: &[u8]) -> Vec<u8> {
        let size = 24 + body.len();
        let mut out = vec![0u8; size];
        out[..16].copy_from_slice(&raw(guid));
        out[18] = kind;
        out[20] = (size & 0xff) as u8;
        out[21] = ((size >> 8) & 0xff) as u8;
        out[22] = ((size >> 16) & 0xff) as u8;
        out[23] = 0x07;
        out[24..].copy_from_slice(body);
        out
    }

    fn fv(files: &[Vec<u8>]) -> Vec<u8> {
        let header_length = 0x48usize;
        let payload: usize = files.iter().map(|file| align8(file.len())).sum();
        let total = header_length + payload;
        let mut volume = vec![0u8; total];
        volume[0x10..0x20].copy_from_slice(&raw(FS_GUID));
        volume[0x20..0x28].copy_from_slice(&(total as u64).to_le_bytes());
        volume[0x28..0x2c].copy_from_slice(b"_FVH");
        volume[0x2c..0x30].copy_from_slice(&0x0004_feffu32.to_le_bytes());
        volume[0x30..0x32].copy_from_slice(&(header_length as u16).to_le_bytes());
        volume[0x37] = 2;
        volume[0x38..0x3c].copy_from_slice(&0x1000u32.to_le_bytes());
        volume[0x3c..0x40].copy_from_slice(&1u32.to_le_bytes());
        let mut sum: u16 = 0;
        for chunk in volume[..header_length].chunks_exact(2) {
            sum = sum.wrapping_add(u16::from_le_bytes([chunk[0], chunk[1]]));
        }
        let checksum = 0u16.wrapping_sub(sum);
        volume[0x32..0x34].copy_from_slice(&checksum.to_le_bytes());
        let mut offset = header_length;
        for file in files {
            volume[offset..offset + file.len()].copy_from_slice(file);
            offset += align8(file.len());
        }
        volume
    }

    fn map(bytes: &[u8]) -> Report {
        let mut walk = Walk::new(builtin_names());
        walk.walk_buffer(bytes, "test", 0);
        walk.finish(bytes, "test", None)
    }

    #[test]
    fn guid_round_trip() {
        for guid in [GUID_A, GUID_B, HOST_GUID] {
            let bytes = raw(guid);
            assert_eq!(uefi_depex::guid(&bytes).expect("format"), guid);
        }
    }

    const HOST_GUID: &str = "70c8d679-5e72-489e-a0a1-27e7a5d0cb35";

    #[test]
    fn dec_name_is_resolved() {
        let mut names = BTreeMap::new();
        parse_dec(
            "  gQcomScmProtocolGuid = { 0x77ed108d, 0x8524, 0x4b8b, { 0x9d, 0x2e, 0x34, 0x98, 0x7a, 0xec, 0xb9, 0xc1 } }\n",
            &mut names,
        );
        assert_eq!(
            names.get("77ed108d-8524-4b8b-9d2e-34987aecb9c1"),
            Some(&"gQcomScmProtocolGuid".to_owned())
        );
    }

    #[test]
    fn header_name_is_resolved() {
        let mut names = BTreeMap::new();
        parse_header(
            "#define MY_PROTOCOL_GUID { 0x330ed179, 0x7c94, 0x407c, { 0xaf, 0x99, 0xdd, 0xcc, 0x62, 0x44, 0xa0, 0x31 } }\n",
            &mut names,
        );
        assert_eq!(
            names.get("330ed179-7c94-407c-af99-ddcc6244a031"),
            Some(&"MY_PROTOCOL_GUID".to_owned())
        );
    }

    #[test]
    fn maps_ui_name_and_version() {
        let body = [
            section(0x15, &u16s("ExampleDriver")),
            section(0x14, &u16s("1.2")),
            section(0x10, &[0u8; 16]),
        ]
        .concat();
        let report = map(&fv(&[ffs(DRIVER_1, 0x07, &body)]));
        assert_eq!(report.volumes.len(), 1);
        assert_eq!(report.files.len(), 1);
        assert_eq!(report.files[0].name.as_deref(), Some("ExampleDriver"));
        assert_eq!(report.files[0].version.as_deref(), Some("1.2"));
        assert_eq!(report.files[0].images.len(), 1);
        assert_eq!(report.files[0].images[0].size, 16);
    }

    #[test]
    fn maps_extended_section_size() {
        let body = [section_ext(0x10, &[0u8; 20])].concat();
        let report = map(&fv(&[ffs(DRIVER_1, 0x07, &body)]));
        assert_eq!(report.files[0].images[0].size, 20);
        assert!(report.diagnostics.is_empty(), "{:?}", report.diagnostics);
    }

    #[test]
    fn walks_nested_volume_image() {
        let inner = fv(&[ffs(DRIVER_2, 0x07, &section(0x10, &[0u8; 8]))]);
        let outer_body = section(0x17, &inner);
        let report = map(&fv(&[ffs(HOLDER, 0x0b, &outer_body)]));
        assert_eq!(report.volumes.len(), 2, "{:?}", report.diagnostics);
        assert_eq!(report.files.len(), 2);
        assert!(report.files.iter().any(|file| file.guid == DRIVER_2));
    }

    #[test]
    fn decodes_dependency_and_reports_unsatisfied() {
        let missing = [push(GUID_A), push(GUID_B), vec![0x03, 0x08]].concat();
        let free = [vec![0x06u8, 0x08]].concat();
        let report = map(&fv(&[
            ffs(DRIVER_1, 0x07, &depex(&missing)),
            ffs(DRIVER_2, 0x07, &depex(&free)),
        ]));
        assert_eq!(report.dispatch.order.len(), 1);
        assert_eq!(report.dispatch.order[0].guid, DRIVER_2);
        assert_eq!(report.dispatch.never_dispatched.len(), 1);
        let never = &report.dispatch.never_dispatched[0];
        assert_eq!(never.guid, DRIVER_1);
        let guids: Vec<&str> = never
            .unsatisfied
            .iter()
            .map(|value| value.guid.as_str())
            .collect();
        assert!(guids.contains(&GUID_A));
        assert!(guids.contains(&GUID_B));
    }

    #[test]
    fn candidate_reference_enables_later_driver() {
        let mut image = vec![0u8; 32];
        image[4..20].copy_from_slice(&raw(GUID_A));
        let producer = [
            section(0x15, &u16s("Producer")),
            section(0x10, &image),
            depex(&[0x06, 0x08]),
        ]
        .concat();
        let consumer = depex(&[push(GUID_A), vec![0x08]].concat());
        let report = map(&fv(&[
            ffs(DRIVER_1, 0x07, &producer),
            ffs(DRIVER_2, 0x07, &consumer),
        ]));
        assert_eq!(report.dispatch.order.len(), 2, "{:?}", report.diagnostics);
        assert_eq!(report.dispatch.order[0].guid, DRIVER_1);
        assert_eq!(report.dispatch.order[1].guid, DRIVER_2);
        assert!(report.protocols[GUID_A].available);
        assert_eq!(
            report.protocols[GUID_A].reference_candidates,
            vec!["Producer".to_owned()]
        );
    }

    #[test]
    fn dependency_before_producer_needs_a_second_pass() {
        let mut image = vec![0u8; 32];
        image[4..20].copy_from_slice(&raw(GUID_A));
        let producer = [
            section(0x15, &u16s("Producer")),
            section(0x10, &image),
            depex(&[0x06, 0x08]),
        ]
        .concat();
        let consumer = depex(&[push(GUID_A), vec![0x08]].concat());
        let report = map(&fv(&[
            ffs(DRIVER_1, 0x07, &consumer),
            ffs(DRIVER_2, 0x07, &producer),
        ]));
        assert_eq!(report.dispatch.order.len(), 2, "{:?}", report.diagnostics);
        assert_eq!(report.dispatch.order[0].guid, DRIVER_2);
        assert_eq!(report.dispatch.order[1].guid, DRIVER_1);
    }

    #[test]
    fn apriori_bypasses_depex_only_in_its_volume() {
        let inner = fv(&[ffs(DRIVER_2, 0x07, &depex(&[0x07, 0x08]))]);
        let entries = [raw(DRIVER_1), raw(DRIVER_2)].concat();
        let report = map(&fv(&[
            ffs(DRIVER_1, 0x07, &depex(&[0x07, 0x08])),
            ffs(APRIORI_GUID, 0x02, &section(0x19, &entries)),
            ffs(HOLDER, 0x0b, &section(0x17, &inner)),
        ]));
        let order: Vec<_> = report
            .dispatch
            .order
            .iter()
            .map(|step| step.guid.as_str())
            .collect();
        assert_eq!(order, [DRIVER_1]);
        assert_eq!(report.dispatch.never_dispatched[0].guid, DRIVER_2);
    }

    #[test]
    fn absent_depex_waits_for_architectural_services() {
        let report = map(&fv(&[
            ffs(DRIVER_1, 0x07, &section(0x10, &[0; 16])),
            ffs(DRIVER_2, 0x07, &depex(&[0x06, 0x08])),
        ]));
        let order: Vec<_> = report
            .dispatch
            .order
            .iter()
            .map(|step| step.guid.as_str())
            .collect();
        assert_eq!(order, [DRIVER_2]);
        assert_eq!(report.dispatch.never_dispatched[0].guid, DRIVER_1);
        let services: Vec<u8> = ARCH_PROTOCOLS.iter().flat_map(|guid| raw(guid)).collect();
        let provider = [section(0x10, &services), depex(&[0x06, 0x08])].concat();
        let report = map(&fv(&[
            ffs(DRIVER_1, 0x07, &section(0x10, &[0; 16])),
            ffs(DRIVER_2, 0x07, &provider),
        ]));
        let order: Vec<_> = report
            .dispatch
            .order
            .iter()
            .map(|step| step.guid.as_str())
            .collect();
        assert_eq!(order, [DRIVER_2, DRIVER_1]);
        assert!(report.dispatch.never_dispatched.is_empty());
    }

    #[test]
    fn sor_and_non_dxe_files_do_not_dispatch_automatically() {
        let report = map(&fv(&[
            ffs(DRIVER_1, 0x07, &depex(&[0x09, 0x06, 0x08])),
            ffs(DRIVER_2, 0x09, &depex(&[0x06, 0x08])),
            ffs(DRIVER_3, 0x0a, &depex(&[0x06, 0x08])),
        ]));
        assert!(report.dispatch.order.is_empty());
        assert_eq!(report.dispatch.never_dispatched.len(), 1);
        assert_eq!(report.dispatch.never_dispatched[0].guid, DRIVER_1);
    }

    #[test]
    fn before_and_after_follow_target_scheduling() {
        let before = [vec![0x00], raw(DRIVER_2).to_vec(), vec![0x08]].concat();
        let after = [vec![0x01], raw(DRIVER_2).to_vec(), vec![0x08]].concat();
        let report = map(&fv(&[
            ffs(DRIVER_3, 0x07, &depex(&after)),
            ffs(DRIVER_2, 0x07, &depex(&[0x06, 0x08])),
            ffs(DRIVER_1, 0x07, &depex(&before)),
        ]));
        let order: Vec<_> = report
            .dispatch
            .order
            .iter()
            .map(|step| step.guid.as_str())
            .collect();
        assert_eq!(order, [DRIVER_1, DRIVER_2, DRIVER_3]);
        assert!(!report.protocols.contains_key(DRIVER_2));
        let report = map(&fv(&[
            ffs(DRIVER_1, 0x07, &depex(&before)),
            ffs(DRIVER_2, 0x07, &depex(&[0x07, 0x08])),
            ffs(DRIVER_3, 0x07, &depex(&after)),
        ]));
        assert!(report.dispatch.order.is_empty());
        assert_eq!(report.dispatch.never_dispatched.len(), 3);
    }

    #[test]
    fn apriori_order_is_read_and_applied() {
        let mut apriori_body = Vec::new();
        apriori_body.extend_from_slice(&raw(DRIVER_3));
        apriori_body.extend_from_slice(&raw(DRIVER_1));
        let apriori = ffs(APRIORI_GUID, 0x02, &section(0x19, &apriori_body));
        let free = [vec![0x06u8, 0x08]].concat();
        let report = map(&fv(&[
            ffs(DRIVER_1, 0x07, &depex(&free)),
            ffs(DRIVER_2, 0x07, &depex(&free)),
            ffs(DRIVER_3, 0x07, &depex(&free)),
            apriori,
        ]));
        assert_eq!(
            report.apriori,
            vec![DRIVER_3.to_owned(), DRIVER_1.to_owned()]
        );
        let order: Vec<&str> = report
            .dispatch
            .order
            .iter()
            .map(|step| step.guid.as_str())
            .collect();
        assert_eq!(order, vec![DRIVER_3, DRIVER_1, DRIVER_2]);
        assert_eq!(report.dispatch.order[0].apriori_index, Some(0));
        assert_eq!(report.dispatch.order[1].apriori_index, Some(1));
    }

    #[test]
    fn invalid_dependency_is_a_reason() {
        let report = map(&fv(&[ffs(DRIVER_1, 0x07, &depex(&[0x02, 0x00]))]));
        assert_eq!(report.dispatch.order.len(), 0);
        assert_eq!(report.dispatch.never_dispatched.len(), 1);
        assert!(report.dispatch.never_dispatched[0]
            .reason
            .contains("invalid"));
    }

    #[test]
    fn truncated_section_is_reported() {
        let mut body = section(0x10, &[0u8; 8]);
        body[0] = 0xff;
        body[1] = 0x00;
        body[2] = 0x00;
        let report = map(&fv(&[ffs(DRIVER_1, 0x07, &body)]));
        assert!(!report.diagnostics.is_empty());
    }

    #[test]
    fn invalid_volume_is_not_a_volume() {
        let mut bytes = fv(&[ffs(DRIVER_1, 0x07, &section(0x10, &[0u8; 4]))]);
        bytes[0x32] ^= 0xff;
        let report = map(&bytes);
        // The checksum is invalid, but the length and signature are valid.
        assert_eq!(report.volumes.len(), 1);
        assert!(report
            .diagnostics
            .iter()
            .any(|value| value.contains("checksum")));
    }

    #[test]
    fn pad_file_is_skipped() {
        // The real volumes start with a pad file. Its name is all 0xFF and its
        // state is inverted by the erase polarity.
        let mut pad = vec![0xffu8; 0x2c];
        pad[16..18].copy_from_slice(&[0xaa, 0xbb]);
        pad[18] = 0xf0;
        pad[19] = 0x00;
        pad[20..23].copy_from_slice(&[0x2c, 0, 0]);
        pad[23] = 0xf8;
        let report = map(&fv(&[pad, ffs(DRIVER_1, 0x07, &section(0x10, &[0u8; 4]))]));
        assert_eq!(report.files.len(), 1, "{:?}", report.diagnostics);
        assert_eq!(report.files[0].guid, DRIVER_1);
        assert_eq!(report.volumes[0].file_count, 1);
    }

    #[test]
    fn log_lines_are_parsed() {
        let load = parse_load(
            "Loading driver at 0x0007F8A000 EntryPoint=0x0007F8A500 ScmDxeCompat.efi",
            7,
        )
        .expect("load line");
        assert_eq!(load.name, "ScmDxeCompat.efi");
        assert_eq!(load.address.as_deref(), Some("0007F8A000"));
        assert_eq!(load.entry_point.as_deref(), Some("0007F8A500"));
        assert_eq!(
            normalize_name(&load.name),
            normalize_name("\\EFI\\OEM\\ScmDxeCompat")
        );
        assert_eq!(
            parse_install(
                "InstallProtocolInterface: 77ED108D-8524-4B8B-9D2E-34987AECB9C1 7F8A2C40"
            ),
            Some("77ed108d-8524-4b8b-9d2e-34987aecb9c1".to_owned())
        );
        assert_eq!(
            parse_load("no dispatcher line", 1).map(|value| value.name),
            None
        );
    }

    #[test]
    fn log_correlation_reports_missing_driver() {
        let free = [vec![0x06u8, 0x08]].concat();
        let bytes = fv(&[ffs(DRIVER_1, 0x07, &depex(&free))]);
        let report = map(&bytes);
        let path =
            std::env::temp_dir().join(format!("qcom-vp-uefi-map-test-{}.log", std::process::id()));
        fs::write(
            &path,
            "Loading driver at 0x1000 EntryPoint=0x1010 OtherDxe.efi\nInstallProtocolInterface: 11111111-2222-3333-4444-555555555555 1000\n",
        )
        .expect("write the log");
        let log = correlate_log(&path, &report).expect("correlate the log");
        fs::remove_file(&path).ok();
        assert_eq!(log.loads, 1);
        assert_eq!(log.loaded_but_not_mapped, vec!["otherdxe".to_owned()]);
        assert_eq!(log.mapped_but_not_loaded, vec![normalize_name(DRIVER_1)]);
        assert_eq!(log.installs.len(), 1);
    }

    #[test]
    fn dot_and_markdown_render() {
        let free = [vec![0x06u8, 0x08]].concat();
        let report = map(&fv(&[ffs(DRIVER_1, 0x07, &depex(&free))]));
        let dot = render_dot(&report);
        assert!(dot.contains("digraph uefi_map"));
        assert!(dot.contains(DRIVER_1));
        let markdown = render_markdown(&report);
        assert!(markdown.contains("Static dispatch order"));
        assert!(markdown.contains(DRIVER_1));
    }
}
