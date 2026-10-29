//! Read-only APSS observation through QEMU's GDB remote protocol.
//!
//! Sites apply only to the hash-bound SM8850 and SM8845 OEM images. UEFI's SEC
//! image loads the DXE core and exposes its relocated entry in x20. The core's
//! StartImage dispatch exposes each loaded PE entry in x8. This follows real
//! firmware dispatch, rather than guessing a relocated ABL address.
//! The observer never writes registers, memory, or the guest PC. Z1 requests
//! use QEMU hardware breakpoints. Single-step stops prove completed instructions.
//! Observation stops also hold SystemC time through a private control socket.
//! Inspection holds do not alter guest watchdog policy or programmed deadlines.
//!
//! Each profile records the package hashes and binary coordinates.
//! See docs/qcom/sm88xx/oplus/observer-sites.md for the offline disassembly evidence.
//! Both DXE and ABL PEs have preferred base zero and equal raw/virtual section
//! offsets. Their relocation directories do not modify the observed sites.
//! Both packages call ExitBootServices at DXE RVA 0x24d8.
//! Their final kernel branch executes BLR x19 after an x1-x3 clearing helper.
//! The selected boot-v4 kernel is already an uncompressed AArch64 Image.
//! Its bytes start at boot.img +4096. The runtime x19 supplies its load address.
//! No guessed kernel load address or host decompression enters the guest path.

use anyhow::{bail, ensure, Context, Result};
use sha2::{Digest, Sha256};
use std::collections::HashMap;
use std::fs::{self, DirBuilder, File, OpenOptions};
use std::io::{BufReader, Read, Write};
use std::os::unix::fs::DirBuilderExt;
use std::os::unix::net::UnixStream;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

const CORE_ENTRY: u64 = 0x1000;
const CORE_DISPATCH: u64 = 0x6f44;
const ABL_ENTRY: u64 = 0x189c;

struct Profile {
    kernel_bytes: u64,
    kernel_sha256: &'static str,
    uefi_sha256: &'static str,
    abl_sha256: &'static str,
    core_volume_sha256: &'static str,
    abl_volume_sha256: &'static str,
    core_stream: usize,
    core_pe: usize,
    uefi_entry: u64,
    core_discovery: u64,
    exit_call: u64,
    transfer: u64,
}

const PROFILES: [Profile; 2] = [
    Profile {
        kernel_bytes: 39889408,
        kernel_sha256: "2609ff5297e2716fd061daf3f51661b4ea068ed96cd17a320bef32d7d3d71393",
        uefi_sha256: "da8ad1b0ffc2eff91d7d34d8574a682b7007e0b052439fbbcde989d5393071f5",
        abl_sha256: "b3cd257aee11de8720fbf5557d52326d38bdf1e9176794d85c791efa4a714278",
        core_volume_sha256: "0329b1be2fc320149437cc29d2de83a886fe40d0d4a348371cb56175fc92a4d6",
        abl_volume_sha256: "3898da4264df336e0ba9f8d251baa397337f3455b3f35d73fd77059505371d80",
        core_stream: 0x55268,
        core_pe: 0x73c,
        uefi_entry: 0xc6800000,
        core_discovery: 0xc6817e60,
        exit_call: 0x157f4,
        transfer: 0x15a24,
    },
    Profile {
        kernel_bytes: 40024576,
        kernel_sha256: "e47ec8fecfcaca4fe80ccc020af242d956225e09441db632db2190a89c522aa5",
        uefi_sha256: "13722b7ed849359e7afc731dfdc1eb0c1fc1815907d708f4b94cadb7e03be4f4",
        abl_sha256: "83767a6f1b2ce098c6e4ebc60c546833652a923548f3b1d1cd737533f576b0bf",
        core_volume_sha256: "ae119f8b8b083df44e131a7591b6a7bee7c04a88905c78214157095cba38e65e",
        abl_volume_sha256: "f73a11a42e6427b2517ee19c11e854c00161070e9a864825c8f550f70a2bf025",
        core_stream: 0x54268,
        core_pe: 0x6fc,
        uefi_entry: 0xa7000000,
        core_discovery: 0xa7017f0c,
        exit_call: 0x156e4,
        transfer: 0x15914,
    },
];

impl Profile {
    fn select(kernel: &crate::handoff::KernelIdentity) -> Result<&'static Self> {
        ensure!(
            kernel.member == "IMAGES/boot.img",
            "The observer does not support the selected kernel member"
        );
        PROFILES
            .iter()
            .find(|profile| {
                kernel.bytes == profile.kernel_bytes && kernel.sha256 == profile.kernel_sha256
            })
            .context("The automatic observer has no profile for this kernel image. Use --apss-gdb to run without it")
    }
}

pub struct Sites {
    profile: &'static Profile,
    uefi: Vec<u8>,
    core: Vec<u8>,
    abl: Vec<u8>,
}

/// Owns the private debugger endpoint. Removal also runs on error returns.
pub struct SocketDirectory {
    directory: PathBuf,
    socket: PathBuf,
    clock_socket: PathBuf,
}

impl SocketDirectory {
    pub fn new() -> Result<Self> {
        let nonce = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)?
            .as_nanos();
        let directory = PathBuf::from(format!("/tmp/qcom-vp-{}-{nonce:x}", std::process::id()));
        let socket = directory.join("apss.sock");
        let clock_socket = directory.join("clock.sock");
        ensure!(
            socket.as_os_str().as_encoded_bytes().len()
                .max(clock_socket.as_os_str().as_encoded_bytes().len()) < 108,
            "The APSS debugger socket path exceeds the Unix socket limit"
        );
        DirBuilder::new()
            .mode(0o700)
            .create(&directory)
            .context("Cannot create the private APSS debugger directory")?;
        Ok(Self { directory, socket, clock_socket })
    }

    pub fn socket(&self) -> &Path {
        &self.socket
    }

    pub fn clock_socket(&self) -> &Path {
        &self.clock_socket
    }
}

impl Drop for SocketDirectory {
    fn drop(&mut self) {
        let _ = fs::remove_file(&self.socket);
        let _ = fs::remove_file(&self.clock_socket);
        let _ = fs::remove_dir(&self.directory);
    }
}

fn member(archive: &mut zip::ZipArchive<File>, name: &str, expected: &str) -> Result<Vec<u8>> {
    let mut bytes = Vec::new();
    archive.by_name(name)?.read_to_end(&mut bytes)?;
    ensure!(
        format!("{:x}", Sha256::digest(&bytes)) == expected,
        "The observer does not support the selected {name} identity"
    );
    Ok(bytes)
}

pub(crate) fn decompress(bytes: &[u8]) -> Result<Vec<u8>> {
    // xz decodes the OEM EFI LZMA guided sections. It does not execute firmware.
    let mut child = Command::new("xz")
        .args([
            "--format=lzma",
            "--decompress",
            "--stdout",
            "--single-stream",
        ])
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .context("The kernel observer requires xz on PATH to decode the OEM EFI guided sections")?;
    let mut input = child.stdin.take().context("xz has no input pipe")?;
    let (output, written) = std::thread::scope(|scope| {
        let writer = scope.spawn(move || input.write_all(bytes));
        let output = child.wait_with_output();
        let written = writer.join();
        (output, written)
    });
    let output = output?;
    let written = written.map_err(|_| anyhow::anyhow!("The LZMA input writer failed"))?;
    ensure!(
        output.status.success(),
        "Cannot decode the selected EFI guided section: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    // xz can close stdin after the first stream, before trailing container bytes.
    if let Err(error) = written {
        ensure!(
            error.kind() == std::io::ErrorKind::BrokenPipe,
            "Cannot supply the EFI guided section: {error}"
        );
    }
    Ok(output.stdout)
}

impl Sites {
    pub fn load(package: &Path, kernel: &crate::handoff::KernelIdentity) -> Result<Self> {
        let profile = Profile::select(kernel)?;
        let mut archive = zip::ZipArchive::new(File::open(package)?)?;
        let uefi = member(&mut archive, "IMAGES/uefi.elf", profile.uefi_sha256)?;
        let abl = member(&mut archive, "IMAGES/abl.elf", profile.abl_sha256)?;
        let core_volume = decompress(&uefi[profile.core_stream..])?;
        let abl_volume = decompress(&abl[0x1078..])?;
        ensure!(
            format!("{:x}", Sha256::digest(&core_volume)) == profile.core_volume_sha256,
            "The decoded DXE volume differs"
        );
        ensure!(
            format!("{:x}", Sha256::digest(&abl_volume)) == profile.abl_volume_sha256,
            "The decoded ABL volume differs"
        );
        // These PEs use identical file and virtual section offsets, base zero.
        let core = core_volume
            .get(profile.core_pe..profile.core_pe + 0x30000)
            .context("The DXE PE is truncated")?
            .to_vec();
        let abl = abl_volume
            .get(0xb8..0xbe0b8)
            .context("The ABL PE is truncated")?
            .to_vec();
        ensure!(
            &core[..2] == b"MZ" && &abl[..2] == b"MZ",
            "The selected EFI PE headers differ"
        );
        Ok(Self {
            profile,
            uefi,
            core,
            abl,
        })
    }
}

/// Owns the observer thread until it completes or cancellation joins it.
pub struct Observation {
    thread: Option<JoinHandle<Result<VerifiedEntry>>>,
    cancelled: Arc<AtomicBool>,
}

impl Observation {
    pub fn is_finished(&self) -> bool {
        self.thread
            .as_ref()
            .is_some_and(|thread| thread.is_finished())
    }

    pub fn cancel(&self) {
        self.cancelled.store(true, Ordering::Relaxed);
    }

    pub fn finish(mut self) -> Result<VerifiedEntry> {
        self.thread
            .take()
            .context("The APSS observer was already joined")?
            .join()
            .map_err(|_| anyhow::anyhow!("The APSS observer panicked"))?
    }
}

impl Drop for Observation {
    fn drop(&mut self) {
        if let Some(thread) = self.thread.take() {
            self.cancel();
            let _ = thread.join();
        }
    }
}

/// Retains the debugger connection with APSS paused after the entry proof.
pub struct VerifiedEntry {
    remote: Remote,
}

impl VerifiedEntry {
    /// Resume APSS through GDB detach. This does not prove later kernel execution.
    pub fn detach(mut self) -> Result<()> {
        ensure!(
            self.remote.command("D")? == b"OK",
            "The APSS debugger did not detach after verified kernel entry"
        );
        self.remote.clock.resume()?;
        Ok(())
    }
}

pub fn start(
    socket: PathBuf,
    clock_socket: PathBuf,
    out: PathBuf,
    sites: Sites,
    kernel: crate::handoff::KernelIdentity,
    deadline: Instant,
) -> Result<Observation> {
    let cancelled = Arc::new(AtomicBool::new(false));
    let observer_cancelled = Arc::clone(&cancelled);
    let thread = std::thread::Builder::new()
        .name("apss-observer".to_owned())
        .spawn(move || observe(&socket, &clock_socket, &out, &sites, &kernel, deadline, observer_cancelled))
        .context("Cannot start the APSS observer")?;
    Ok(Observation {
        thread: Some(thread),
        cancelled,
    })
}

/// Owns one simulation-time hold. Closing the socket releases that hold on error.
struct ClockControl {
    stream: UnixStream,
    deadline: Instant,
    cancelled: Arc<AtomicBool>,
    paused_at: Option<u64>,
}

impl ClockControl {
    fn connect(path: &Path, deadline: Instant, cancelled: Arc<AtomicBool>) -> Result<Self> {
        loop {
            ensure!(!cancelled.load(Ordering::Relaxed), "Observation clock connection was cancelled");
            ensure!(Instant::now() < deadline, "Observation clock connection timed out");
            match UnixStream::connect(path) {
                Ok(stream) => {
                    stream.set_read_timeout(Some(Duration::from_millis(100)))?;
                    stream.set_write_timeout(Some(Duration::from_millis(100)))?;
                    return Ok(Self { stream, deadline, cancelled, paused_at: None });
                }
                Err(error) if matches!(error.kind(),
                    std::io::ErrorKind::NotFound | std::io::ErrorKind::ConnectionRefused) => {
                    std::thread::sleep(Duration::from_millis(10));
                }
                Err(error) => return Err(error).context("Cannot connect the observation clock"),
            }
        }
    }

    fn change(&mut self, command: u8) -> Result<u64> {
        self.stream.write_all(&[command]).context("Cannot command the observation clock")?;
        let mut reply = [0u8; 9];
        let mut received = 0;
        while received < reply.len() {
            ensure!(!self.cancelled.load(Ordering::Relaxed), "Observation clock request was cancelled");
            ensure!(Instant::now() < self.deadline, "Observation clock request timed out");
            match self.stream.read(&mut reply[received..]) {
                Ok(0) => bail!("The observation clock disconnected"),
                Ok(count) => received += count,
                Err(error) if matches!(error.kind(), std::io::ErrorKind::WouldBlock
                    | std::io::ErrorKind::TimedOut | std::io::ErrorKind::Interrupted) => {}
                Err(error) => return Err(error).context("Cannot read the observation clock response"),
            }
        }
        ensure!(reply[0] == b'O', "The observation clock rejected its request");
        Ok(u64::from_le_bytes(reply[1..].try_into()?))
    }

    fn pause(&mut self) -> Result<()> {
        if self.paused_at.is_none() {
            self.paused_at = Some(self.change(b'P')?);
        }
        Ok(())
    }

    fn resume(&mut self) -> Result<()> {
        if self.paused_at.is_some() {
            self.change(b'R')?;
            self.paused_at = None;
        }
        Ok(())
    }

    fn check_held(&mut self) -> Result<()> {
        let before = self.paused_at.context("Guest memory inspection requires a simulation-time hold")?;
        ensure!(
            self.change(b'T')? == before,
            "Simulation time advanced during guest memory inspection"
        );
        Ok(())
    }
}

struct Remote {
    stream: BufReader<UnixStream>,
    clock: ClockControl,
    deadline: Instant,
    registers: HashMap<String, (u32, usize)>,
    cancelled: Arc<AtomicBool>,
    /// The thread that reported the last stop. Register reads and steps use it.
    thread: Option<String>,
    /// The last stop reply, for failure reports.
    last_stop: String,
}

impl Remote {
    fn connect(socket: &Path, clock_socket: &Path, deadline: Instant, cancelled: Arc<AtomicBool>) -> Result<Self> {
        loop {
            ensure!(
                !cancelled.load(Ordering::Relaxed),
                "The APSS observer was cancelled"
            );
            ensure!(
                Instant::now() < deadline,
                "The APSS observer connection timed out"
            );
            match UnixStream::connect(socket) {
                Ok(stream) => {
                    stream.set_read_timeout(Some(Duration::from_millis(100)))?;
                    stream.set_write_timeout(Some(Duration::from_millis(100)))?;
                    let mut clock = ClockControl::connect(clock_socket, deadline, Arc::clone(&cancelled))?;
                    clock.pause()?;
                    return Ok(Self {
                        stream: BufReader::with_capacity(65536, stream),
                        clock,
                        deadline,
                        registers: HashMap::new(),
                        cancelled,
                        thread: None,
                        last_stop: String::new(),
                    });
                }
                Err(error)
                    if matches!(
                        error.kind(),
                        std::io::ErrorKind::NotFound | std::io::ErrorKind::ConnectionRefused
                    ) =>
                {
                    std::thread::sleep(Duration::from_millis(10))
                }
                Err(error) => return Err(error).context("Cannot connect the APSS observer"),
            }
        }
    }

    fn byte(&mut self) -> Result<u8> {
        loop {
            // Check deadlines before each socket read, not for every buffered byte.
            if self.stream.buffer().is_empty() {
                ensure!(
                    !self.cancelled.load(Ordering::Relaxed),
                    "The APSS observer was cancelled"
                );
                ensure!(
                    Instant::now() < self.deadline,
                    "The APSS observer timed out"
                );
            }
            let mut byte = [0];
            match self.stream.read_exact(&mut byte) {
                Ok(()) => return Ok(byte[0]),
                Err(error)
                    if matches!(
                        error.kind(),
                        std::io::ErrorKind::WouldBlock
                            | std::io::ErrorKind::TimedOut
                            | std::io::ErrorKind::Interrupted
                    ) => {}
                Err(error) => {
                    return Err(error).context("The APSS debugger disconnected before handoff")
                }
            }
        }
    }

    fn command(&mut self, command: &str) -> Result<Vec<u8>> {
        let decoded = self.reply(command)?;
        ensure!(
            !decoded.starts_with(b"E"),
            "APSS debugger error: {} for request {command}",
            String::from_utf8_lossy(&decoded)
        );
        Ok(decoded)
    }

    // Returns the decoded reply, including an `Exx` error reply.
    fn reply(&mut self, command: &str) -> Result<Vec<u8>> {
        let checksum = command.bytes().fold(0_u8, u8::wrapping_add);
        write!(self.stream.get_mut(), "${command}#{checksum:02x}")?;
        if command == "c" || command == "s" || command.starts_with("vCont;") {
            self.clock.resume()?;
        }
        loop {
            match self.byte()? {
                b'+' => continue,
                b'-' => bail!("The APSS debugger rejected a packet"),
                b'$' => break,
                other => bail!("Unexpected APSS debugger packet prefix {other:#x}"),
            }
        }
        let mut encoded = Vec::new();
        loop {
            let byte = self.byte()?;
            if byte == b'#' {
                break;
            }
            ensure!(
                encoded.len() < 1024 * 1024,
                "The APSS debugger packet exceeds the limit"
            );
            encoded.push(byte);
        }
        let digits = [self.byte()?, self.byte()?];
        let checksum = u8::from_str_radix(std::str::from_utf8(&digits)?, 16)?;
        ensure!(
            encoded
                .iter()
                .fold(0_u8, |sum, byte| sum.wrapping_add(*byte))
                == checksum,
            "The APSS debugger packet checksum differs"
        );
        self.stream.get_mut().write_all(b"+")?;
        let mut decoded = Vec::new();
        let mut cursor = 0;
        while cursor < encoded.len() {
            match encoded[cursor] {
                b'}' => {
                    cursor += 1;
                    decoded.push(*encoded.get(cursor).context("Truncated RSP escape")? ^ 0x20);
                }
                b'*' => {
                    cursor += 1;
                    let count = *encoded.get(cursor).context("Truncated RSP repeat")?;
                    ensure!((32..=126).contains(&count), "Invalid RSP repeat count");
                    let previous = *decoded.last().context("RSP repeat has no preceding byte")?;
                    decoded.resize(decoded.len() + usize::from(count - 29), previous);
                }
                byte => decoded.push(byte),
            }
            cursor += 1;
        }
        Ok(decoded)
    }

    fn xml(&mut self, name: &str, next: &mut u32, depth: usize) -> Result<()> {
        ensure!(
            depth < 8,
            "The debugger XML include depth exceeds the limit"
        );
        let mut xml = Vec::new();
        loop {
            let response =
                self.command(&format!("qXfer:features:read:{name}:{:x},800", xml.len()))?;
            let marker = *response.first().context("The debugger has no target XML")?;
            ensure!(matches!(marker, b'm' | b'l'), "Invalid target XML response");
            ensure!(
                response.len() > 1 && xml.len() < 1024 * 1024,
                "The target XML is empty or too large"
            );
            xml.extend_from_slice(&response[1..]);
            if marker == b'l' {
                break;
            }
        }
        // GDB's target DTD uses xi:include without a namespace declaration.
        let mut reader = quick_xml::Reader::from_str(std::str::from_utf8(&xml)?);
        loop {
            let element = match reader.read_event()? {
                quick_xml::events::Event::Start(element)
                | quick_xml::events::Event::Empty(element) => element,
                quick_xml::events::Event::Eof => break,
                _ => continue,
            };
            let attribute = |key: &[u8]| -> Result<Option<std::borrow::Cow<'_, str>>> {
                Ok(element
                    .try_get_attribute(key)?
                    .map(|value| value.unescape_value())
                    .transpose()?)
            };
            match element.local_name().as_ref() {
                b"include" => self.xml(
                    &attribute(b"href")?.context("Missing XML include path")?,
                    next,
                    depth + 1,
                )?,
                b"reg" => {
                    let number = attribute(b"regnum")?
                        .map(|value| value.parse())
                        .transpose()?
                        .unwrap_or(*next);
                    let bits: usize = attribute(b"bitsize")?
                        .context("Missing register width")?
                        .parse()?;
                    let name = attribute(b"name")?.context("Missing register name")?;
                    self.registers
                        .insert(name.to_ascii_lowercase(), (number, bits));
                    *next = number.checked_add(1).context("Register number overflow")?;
                }
                _ => {}
            }
        }
        Ok(())
    }

    fn reg(&mut self, name: &str) -> Result<u64> {
        let &(number, bits) = self
            .registers
            .get(name)
            .with_context(|| format!("The APSS debugger does not expose {name}"))?;
        ensure!(
            bits <= 64 && bits % 8 == 0,
            "Unsupported register width for {name}"
        );
        let bytes = unhex(&self.command(&format!("p{number:x}"))?)?;
        ensure!(
            bytes.len() == bits / 8,
            "The debugger register width differs for {name}"
        );
        let mut value = [0; 8];
        value[..bytes.len()].copy_from_slice(&bytes);
        Ok(u64::from_le_bytes(value))
    }

    fn memory(&mut self, address: u64, length: usize) -> Result<Vec<u8>> {
        let mut bytes = Vec::with_capacity(length);
        while bytes.len() < length {
            // QEMU accepts up to MAX_PACKET_LENGTH / 2 bytes per read.
            let count = (length - bytes.len()).min(0x8000);
            let at = address
                .checked_add(bytes.len() as u64)
                .context("Guest memory range overflow")?;
            let request = format!("m{at:x},{count:x}");
            let reply = self.command(&request)?;
            let chunk = unhex(&reply)?;
            ensure!(
                chunk.len() == count,
                "The debugger guest memory read is incomplete"
            );
            bytes.extend_from_slice(&chunk);
        }
        self.clock.check_held()?;
        Ok(bytes)
    }

    fn breakpoint(&mut self, address: u64, insert: bool) -> Result<()> {
        ensure!(
            self.command(&format!(
                "{}1,{address:x},4",
                if insert { 'Z' } else { 'z' }
            ))? == b"OK",
            "The APSS debugger does not support hardware execution breakpoints"
        );
        Ok(())
    }

    fn resume(&mut self, step: bool) -> Result<u64> {
        if !step {
            return self.resume_once(false);
        }
        // The QBox CPU loop can leave the stepped instruction unexecuted, for example
        // when a synchronization exit interrupts it. The stop then reports the same
        // thread at the same PC. Retry a bounded number of times. A completed step
        // always changes the PC, except a branch to itself, which the caller rejects.
        let start = self.reg("pc")?;
        let mut pc = start;
        for _ in 0..64 {
            pc = self.resume_once(true)?;
            if pc != start {
                break;
            }
        }
        Ok(pc)
    }

    fn resume_once(&mut self, step: bool) -> Result<u64> {
        // Several APSS CPUs run UEFI. A step must advance only the stopped thread,
        // or another CPU can report its own pending stop instead.
        let command = match (step, &self.thread) {
            (true, Some(thread)) => format!("vCont;s:{thread}"),
            (true, None) => "s".to_owned(),
            (false, _) => "c".to_owned(),
        };
        let stop = self.command(&command)?;
        self.clock.pause()?;
        ensure!(
            matches!(stop.first(), Some(b'S' | b'T')) && stop.get(1..3) == Some(b"05"),
            "APSS did not stop on the requested execution event: {}",
            String::from_utf8_lossy(&stop)
        );
        self.last_stop = String::from_utf8_lossy(&stop).into_owned();
        // A T packet is "T05" followed by "name:value;" pairs, for example "T05thread:02;".
        let reported = String::from_utf8_lossy(stop.get(3..).unwrap_or_default())
            .split(';')
            .find_map(|field| field.strip_prefix("thread:").map(str::to_owned));
        if let Some(thread) = reported {
            if step {
                if let Some(expected) = &self.thread {
                    ensure!(
                        &thread == expected,
                        "APSS thread {thread} stopped during the step of thread {expected}"
                    );
                }
            }
            ensure!(
                self.command(&format!("Hg{thread}"))? == b"OK",
                "The APSS debugger cannot select thread {thread}"
            );
            self.thread = Some(thread);
        }
        self.reg("pc")
    }

    fn until(&mut self, address: u64) -> Result<()> {
        self.breakpoint(address, true)?;
        let pc = self.resume(false)?;
        self.breakpoint(address, false)?;
        ensure!(
            pc == address,
            "APSS stopped at {pc:#x} before the requested firmware site {address:#x}"
        );
        Ok(())
    }

    fn code(&mut self, address: u64, expected: &[u8]) -> Result<()> {
        ensure!(
            self.memory(address, expected.len())? == expected,
            "The executing firmware bytes differ at {address:#x}"
        );
        Ok(())
    }
}

fn unhex(bytes: &[u8]) -> Result<Vec<u8>> {
    ensure!(
        bytes.len() % 2 == 0,
        "The debugger returned an odd hex length"
    );
    bytes
        .chunks_exact(2)
        .map(|pair| Ok(u8::from_str_radix(std::str::from_utf8(pair)?, 16)?))
        .collect()
}

fn observe(
    socket: &Path,
    clock_socket: &Path,
    out: &Path,
    sites: &Sites,
    kernel: &crate::handoff::KernelIdentity,
    deadline: Instant,
    cancelled: Arc<AtomicBool>,
) -> Result<VerifiedEntry> {
    let mut remote = Remote::connect(socket, clock_socket, deadline, cancelled)?;
    remote.command("qSupported:qXfer:features:read+")?;
    remote.xml("target.xml", &mut 0, 0)?;
    let stop = remote.command("?")?;
    ensure!(
        matches!(stop.first(), Some(b'S' | b'T')),
        "APSS is not paused at reset"
    );
    // These must exist before the guest runs. Missing state is a hard failure.
    for name in ["pc", "cpsr", "scr_el3", "sctlr_el1", "sctlr_el2"] {
        ensure!(
            remote.registers.contains_key(name),
            "The APSS debugger does not expose {name}"
        );
    }
    let profile = sites.profile;
    remote.until(profile.uefi_entry)?;
    remote.code(profile.uefi_entry, &sites.uefi[4096..4128])?;
    remote.until(profile.core_discovery)?;
    let core_entry = remote.reg("x20")?;
    let core_base = core_entry
        .checked_sub(CORE_ENTRY)
        .context("Invalid relocated DXE entry")?;
    remote.code(core_entry, &sites.core[0x1000..0x1020])?;
    // Observe DXE entry, then the actual StartImage dispatch for loaded images.
    remote.until(core_entry)?;
    let dispatch = core_base
        .checked_add(CORE_DISPATCH)
        .context("DXE address overflow")?;
    remote.code(
        dispatch,
        &sites.core[CORE_DISPATCH as usize..CORE_DISPATCH as usize + 4],
    )?;
    remote.breakpoint(dispatch, true)?;
    let abl_entry = loop {
        let pc = remote.resume(false)?;
        ensure!(
            pc == dispatch,
            "APSS stopped at {pc:#x} outside the image dispatch {dispatch:#x}"
        );
        remote.breakpoint(dispatch, false)?;
        let entry = remote.reg("x8")?;
        let signature = remote.memory(entry, 32)?;
        if signature == sites.abl[ABL_ENTRY as usize..ABL_ENTRY as usize + 32] {
            let pc = remote.resume(true)?;
            ensure!(
                pc == entry,
                "The firmware stopped at {pc:#x}, not the selected ABL entry {entry:#x}"
            );
            break entry;
        }
        // Leave the dispatch site before reinstalling its hardware breakpoint.
        let pc = remote.resume(true)?;
        ensure!(
            pc == entry,
            "UEFI stopped at {pc:#x}, not the dispatched image entry {entry:#x} (stop {})",
            remote.last_stop
        );
        remote.breakpoint(dispatch, true)?;
    };
    let abl_base = abl_entry
        .checked_sub(ABL_ENTRY)
        .context("Invalid relocated ABL entry")?;
    let exit_return = profile.exit_call + 4;
    for rva in [profile.exit_call, exit_return, profile.transfer] {
        remote.code(abl_base + rva, &sites.abl[rva as usize..rva as usize + 4])?;
    }
    loop {
        remote.until(abl_base + profile.exit_call)?;
        let exit_target = remote.reg("x8")?;
        let core_exit = core_base + 0x24d8;
        ensure!(
            remote.resume(true)? == exit_target,
            "ABL did not execute its boot-services call"
        );
        if exit_target != core_exit {
            // A DXE driver can replace the table slot with a wrapper that logs the
            // call and then enters the core service. Require that entry before
            // ABL's return site, so the selected implementation still runs.
            let return_site = abl_base + exit_return;
            remote.breakpoint(core_exit, true)?;
            remote.breakpoint(return_site, true)?;
            let pc = remote.resume(false)?;
            remote.breakpoint(core_exit, false)?;
            remote.breakpoint(return_site, false)?;
            ensure!(
                pc == core_exit,
                "The ExitBootServices wrapper at {exit_target:#x} returned without the selected implementation"
            );
        }
        remote.until(abl_base + exit_return)?;
        if remote.reg("x0")? == 0 {
            break;
        }
        // ABL's actual error branch returns to GetMemoryMap and retries the
        // call with a new map key. Observe that guest loop, not host retries.
    }
    remote.until(abl_base + profile.transfer)?;
    let kernel_entry = remote.reg("x19")?;
    ensure!(
        remote.resume(true)? == kernel_entry,
        "ABL did not transfer to the kernel entry"
    );
    let registers = [
        remote.reg("x0")?,
        remote.reg("x1")?,
        remote.reg("x2")?,
        remote.reg("x3")?,
    ];
    let cpsr = remote.reg("cpsr")?;
    let el = ((cpsr >> 2) & 3) as u8;
    ensure!(
        matches!(el, 1 | 2),
        "Unsupported kernel entry exception level {el}"
    );
    let scr = remote.reg("scr_el3")?;
    let sctlr = remote.reg(if el == 1 { "sctlr_el1" } else { "sctlr_el2" })?;
    let loaded = remote.memory(kernel_entry, usize::try_from(kernel.bytes)?)?;
    ensure!(
        format!("{:x}", Sha256::digest(&loaded)) == kernel.sha256,
        "The executing kernel differs from the selected package kernel"
    );
    let header = remote.memory(registers[0], 40)?;
    ensure!(
        header[..4] == [0xd0, 0x0d, 0xfe, 0xed],
        "The kernel entry FDT magic differs"
    );
    let length = u32::from_be_bytes(header[4..8].try_into()?) as usize;
    ensure!(
        (40..=2 * 1024 * 1024).contains(&length),
        "The kernel entry FDT size is invalid"
    );
    let fdt = remote.memory(registers[0], length)?;
    crate::handoff::check_fdt(&fdt)?;
    // The selected Image starts with CCMP, not a branch. An exception or a
    // pre-execution breakpoint cannot satisfy this post-instruction PC check.
    ensure!(
        remote.resume(true)? == kernel_entry.checked_add(4).context("Kernel PC overflow")?,
        "The first kernel instruction did not complete normally"
    );
    ensure!(
        remote.reg("cpsr")? & 0xf == cpsr & 0xf && remote.reg("scr_el3")? == scr,
        "The first kernel instruction changed execution state"
    );
    let evidence = crate::handoff::Evidence {
        schema: 1,
        uefi_entry_pc: profile.uefi_entry,
        abl_entry_pc: abl_entry,
        boot_services_exit_observed: true,
        abl_transfer_pc: abl_base + profile.transfer,
        kernel_entry_pc: kernel_entry,
        loaded_kernel_address: kernel_entry,
        fdt_address: registers[0],
        executed_instruction_pc: kernel_entry,
        instruction_completed: true,
        entry_registers: registers,
        exception_level: el,
        non_secure: scr & 1 != 0 && scr & (1 << 62) == 0,
        sctlr,
        daif: cpsr & 0x3c0,
        kernel_member: kernel.member.clone(),
    };
    for (name, bytes) in [
        ("loaded-kernel.bin", loaded.as_slice()),
        ("entry-fdt.bin", fdt.as_slice()),
    ] {
        let mut file = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(out.join(name))?;
        file.write_all(bytes)?;
        file.sync_all()?;
    }
    crate::save_json(&out.join("kernel-entry.json"), &evidence)?;
    crate::handoff::verify(out, kernel)?;
    // Keep APSS paused until the owner stops the child or explicitly detaches.
    Ok(VerifiedEntry { remote })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn profiles_require_exact_kernel_identities() {
        for expected in &PROFILES {
            let mut kernel = crate::handoff::KernelIdentity {
                member: "IMAGES/boot.img".into(),
                sha256: expected.kernel_sha256.into(),
                bytes: expected.kernel_bytes,
            };
            let selected = Profile::select(&kernel).unwrap();
            assert_eq!(selected.uefi_sha256, expected.uefi_sha256);
            assert_eq!(selected.abl_sha256, expected.abl_sha256);
            kernel.bytes += 1;
            assert!(Profile::select(&kernel).is_err());
            kernel.bytes = expected.kernel_bytes;
            kernel.sha256.replace_range(..1, "f");
            assert!(Profile::select(&kernel).is_err());
            kernel.sha256 = expected.kernel_sha256.into();
            kernel.member = "IMAGES/boot_b.img".into();
            assert!(Profile::select(&kernel).is_err());
        }
    }

    #[test]
    fn hex_rejects_missing_or_invalid_bytes() {
        assert_eq!(unhex(b"00ff10").unwrap(), [0, 255, 16]);
        assert!(unhex(b"0").is_err());
        assert!(unhex(b"xx").is_err());
    }

    #[test]
    fn endpoint_directory_is_private_and_removed() {
        use std::os::unix::fs::PermissionsExt;
        let endpoint = SocketDirectory::new().unwrap();
        let directory = endpoint.directory.clone();
        assert_eq!(
            fs::metadata(&directory).unwrap().permissions().mode() & 0o777,
            0o700
        );
        assert!(endpoint.socket().as_os_str().as_encoded_bytes().len() < 108);
        drop(endpoint);
        assert!(!directory.exists());
    }
}
