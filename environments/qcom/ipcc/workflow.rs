use std::collections::BTreeMap;
use std::env;
use std::fmt::Write as FmtWrite;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Output};

const PRODUCER_PREFIX_SIZE: usize = 0x1000;
const PRODUCER_PREFIX_BYTE: u8 = 0xa5;
const EXPECTED_ACCESS: &str = "0x600d0000";

fn main() {
    if let Err(error) = run() {
        eprintln!("error: {error}");
        std::process::exit(1);
    }
}

fn run() -> Result<(), String> {
    let args: Vec<String> = env::args().skip(1).collect();
    let Some(command) = args.first().map(String::as_str) else {
        return Err(usage());
    };
    let options = parse_options(&args[1..], command)?;
    match command {
        "prepare" => prepare(&options),
        "check" => check(&options),
        _ => Err(usage()),
    }
}

fn usage() -> String {
    "usage: workflow prepare --out NEW_DIR [--fixture DIR] [--helper FILE]\n       workflow check --out EXPERIMENT_DIR".to_owned()
}

fn parse_options(args: &[String], command: &str) -> Result<BTreeMap<String, String>, String> {
    let allowed: &[&str] = match command {
        "prepare" => &["--out", "--fixture", "--helper"],
        "check" => &["--out"],
        _ => return Err(usage()),
    };
    if args.len() % 2 != 0 {
        return Err(usage());
    }
    let mut options = BTreeMap::new();
    for pair in args.chunks_exact(2) {
        if !allowed.contains(&pair[0].as_str()) {
            return Err(format!("unknown option {}\n{}", pair[0], usage()));
        }
        if options.insert(pair[0].clone(), pair[1].clone()).is_some() {
            return Err(format!("option {} was supplied more than once", pair[0]));
        }
    }
    if !options.contains_key("--out") {
        return Err(format!("--out is required\n{}", usage()));
    }
    Ok(options)
}

fn prepare(options: &BTreeMap<String, String>) -> Result<(), String> {
    let out = PathBuf::from(&options["--out"]);
    let fixture = options
        .get("--fixture")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("tools/qcom/qcom-vp/tests/fixtures/ipcc"));
    let helper = options
        .get("--helper")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("platforms/qcom/apss-gic.lua"));

    if !fixture.is_dir() {
        return Err(format!("fixture directory does not exist: {}", fixture.display()));
    }
    if !helper.is_file() {
        return Err(format!("APSS GIC helper does not exist: {}", helper.display()));
    }

    fs::create_dir(&out).map_err(|error| {
        format!(
            "cannot create new preparation directory {}: {error}",
            out.display()
        )
    })?;
    let inputs = out.join("inputs");
    let build = out.join(".build");
    fs::create_dir(&inputs).map_err(|error| format!("cannot create {}: {error}", inputs.display()))?;
    fs::create_dir(&build).map_err(|error| format!("cannot create {}: {error}", build.display()))?;

    let tme_bin = compile_guest(
        &fixture,
        &build,
        "tme",
        "riscv32-none-elf",
        "0x200000",
        &["-march=rv32im_zicsr", "-mabi=ilp32"],
    )?;
    let producer_bin = compile_producer(&fixture, &build)?;

    let producer_bytes = fs::read(&producer_bin)
        .map_err(|error| format!("cannot read {}: {error}", producer_bin.display()))?;
    if producer_bytes.is_empty() {
        return Err("Clang produced an empty APSS guest binary".to_owned());
    }
    let producer_size = producer_bytes.len();
    let mut padded_producer = Vec::with_capacity(PRODUCER_PREFIX_SIZE + producer_size);
    padded_producer.resize(PRODUCER_PREFIX_SIZE, PRODUCER_PREFIX_BYTE);
    padded_producer.extend_from_slice(&producer_bytes);
    fs::write(inputs.join("producer.padded.bin"), padded_producer)
        .map_err(|error| format!("cannot write padded APSS guest: {error}"))?;
    fs::copy(&tme_bin, inputs.join("tme.bin"))
        .map_err(|error| format!("cannot copy TME guest binary: {error}"))?;
    fs::create_dir(inputs.join("qcom"))
        .map_err(|error| format!("cannot create the helper directory: {error}"))?;
    fs::copy(&helper, inputs.join("qcom/apss-gic.lua"))
        .map_err(|error| format!("cannot copy APSS GIC Lua helper: {error}"))?;

    let source_platform = fs::read_to_string(fixture.join("platform.lua"))
        .map_err(|error| format!("cannot read fixture platform.lua: {error}"))?;
    let mut platform = source_platform;
    replace_once(
        &mut platform,
        "local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'",
        "local root=assert(os.getenv('QCOM_VP_ENV_ROOT'))..'/'",
        "the probe-root environment variable",
    )?;
    replace_once(
        &mut platform,
        "local directory=assert(os.getenv('QCOM_VP_PLATFORM_DIR'))..'/'",
        "local directory=root",
        "the platform-helper directory",
    )?;
    replace_once(
        &mut platform,
        "bin_file=root..'producer.bin',offset=0",
        &format!(
            "bin_file=root..'producer.padded.bin',offset=0,bin_file_offset=0x{PRODUCER_PREFIX_SIZE:x},bin_file_size=0x{producer_size:x}"
        ),
        "the APSS guest load entry",
    )?;
    fs::write(inputs.join("platform.lua"), platform.as_bytes())
        .map_err(|error| format!("cannot write environment platform.lua: {error}"))?;

    fs::remove_dir_all(&build)
        .map_err(|error| format!("cannot remove temporary guest build directory: {error}"))?;

    let files = [
        ("platform.lua", "inputs/platform.lua"),
        ("qcom/apss-gic.lua", "inputs/qcom/apss-gic.lua"),
        ("producer.padded.bin", "inputs/producer.padded.bin"),
        ("tme.bin", "inputs/tme.bin"),
    ];
    let mut hashes = Vec::with_capacity(files.len());
    for (name, relative_path) in files {
        let path = out.join(relative_path);
        validate_relative_path(name)?;
        validate_relative_path(relative_path)?;
        hashes.push((name, relative_path, sha256(&path)?));
    }
    let manifest = create_manifest(&hashes);
    fs::write(out.join("environment.toml"), manifest.as_bytes())
        .map_err(|error| format!("cannot write environment.toml: {error}"))?;

    println!("Prepared IPCC environment: {}", out.join("environment.toml").display());
    println!("The APSS binary has a {PRODUCER_PREFIX_SIZE:#x}-byte 0xa5 prefix.");
    println!("Lua loads {producer_size:#x} APSS bytes from file offset {PRODUCER_PREFIX_SIZE:#x} at guest address 0x300000.");
    Ok(())
}

fn compile_guest(
    fixture: &Path,
    build: &Path,
    name: &str,
    target: &str,
    address: &str,
    extra: &[&str],
) -> Result<PathBuf, String> {
    let elf = build.join(format!("{name}.elf"));
    let bin = build.join(format!("{name}.bin"));
    let source = fixture.join(format!("{name}.S"));
    let mut clang = Command::new("clang");
    clang
        .arg(format!("--target={target}"))
        .args(["-fuse-ld=lld", "-nostdlib"])
        .arg(format!("-Wl,-Ttext={address}"))
        .arg(source)
        .args(extra)
        .arg("-o")
        .arg(&elf);
    require_success(clang.output(), "clang", &elf)?;
    let mut objcopy = Command::new("llvm-objcopy");
    objcopy.args(["-O", "binary"]).arg(&elf).arg(&bin);
    require_success(objcopy.output(), "llvm-objcopy", &bin)?;
    Ok(bin)
}

fn compile_producer(fixture: &Path, build: &Path) -> Result<PathBuf, String> {
    let elf = build.join("producer.elf");
    let bin = build.join("producer.bin");
    let mut clang = Command::new("clang");
    clang
        .arg("--target=aarch64-none-elf")
        .args(["-fuse-ld=lld", "-nostdlib"])
        .arg("-Wl,-Ttext=0x300000")
        .arg(fixture.join("producer.S"))
        .args([
            "-ffreestanding",
            "-fno-builtin",
            "-mgeneral-regs-only",
            "-O2",
        ])
        .arg(fixture.join("driver.c"))
        .arg("-DCASE=0")
        .arg("-o")
        .arg(&elf);
    require_success(clang.output(), "clang", &elf)?;
    let mut objcopy = Command::new("llvm-objcopy");
    objcopy.args(["-O", "binary"]).arg(&elf).arg(&bin);
    require_success(objcopy.output(), "llvm-objcopy", &bin)?;
    Ok(bin)
}

fn require_success(result: std::io::Result<Output>, tool: &str, output: &Path) -> Result<(), String> {
    let result = result.map_err(|error| format!("cannot start {tool}: {error}"))?;
    if result.status.success() {
        return Ok(());
    }
    let stderr = String::from_utf8_lossy(&result.stderr);
    Err(format!(
        "{tool} failed while creating {} (status {}):\n{}",
        output.display(),
        result.status,
        stderr.trim_end()
    ))
}

fn replace_once(text: &mut String, needle: &str, replacement: &str, label: &str) -> Result<(), String> {
    let count = text.matches(needle).count();
    if count != 1 {
        return Err(format!(
            "expected exactly one {label} in fixture platform.lua, found {count}"
        ));
    }
    *text = text.replacen(needle, replacement, 1);
    Ok(())
}

fn validate_relative_path(path: &str) -> Result<(), String> {
    let path = Path::new(path);
    if path.as_os_str().is_empty() || path.is_absolute() {
        return Err(format!("invalid relative manifest path: {}", path.display()));
    }
    for component in path.components() {
        if !matches!(component, std::path::Component::Normal(_)) {
            return Err(format!("manifest path is not a safe relative path: {}", path.display()));
        }
    }
    Ok(())
}

fn sha256(path: &Path) -> Result<String, String> {
    let output = Command::new("sha256sum")
        .arg("--")
        .arg(path)
        .output()
        .map_err(|error| format!("cannot start sha256sum: {error}"))?;
    if !output.status.success() {
        return Err(format!(
            "sha256sum failed for {}: {}",
            path.display(),
            String::from_utf8_lossy(&output.stderr).trim_end()
        ));
    }
    let text = String::from_utf8_lossy(&output.stdout);
    let hash = text.split_ascii_whitespace().next().unwrap_or_default();
    if hash.len() != 64 || !hash.bytes().all(|byte| byte.is_ascii_hexdigit()) {
        return Err(format!("sha256sum returned an invalid hash for {}", path.display()));
    }
    Ok(hash.to_ascii_lowercase())
}

fn create_manifest(files: &[(&str, &str, String)]) -> String {
    let mut text = String::new();
    writeln!(&mut text, "schema = 1").unwrap();
    writeln!(&mut text, "name = {}", toml_string("ipcc-queue-alias-synthetic")).unwrap();
    writeln!(
        &mut text,
        "description = {}",
        toml_string("Does the constructed APSS producer and RV32 TME guest exercise source-aware IPCC routing, shared queue state, interrupts, and aliases before the intentional unmapped access?")
    )
    .unwrap();
    writeln!(
        &mut text,
        "entry = {}",
        toml_string("Constructed synthetic CPU reset conditions: the APSS Cortex-A53 uses reset vector 0x300000 with EL3 and EL2 support declared, and the RV32 TME CLIC CPU uses reset vector 0x200000. The composition defines synthetic memory maps and required device routes. No PBL, XBL, QSEE, UEFI, ABL, or prior OEM firmware state is established.")
    )
    .unwrap();
    writeln!(
        &mut text,
        "security_policy = {}",
        toml_string("This is a synthetic non-retail environment. It does not implement or qualify device authentication, authorization, OEM trust policy, or physical security. No security result is inferred.")
    )
    .unwrap();
    writeln!(
        &mut text,
        "platform = {}",
        toml_string("platform.lua")
    )
    .unwrap();
    writeln!(&mut text, "limitations = [").unwrap();
    for limitation in [
        "The APSS and TME start from constructed reset vectors and synthetic RAM, not from the OEM boot chain.",
        "The composition models only the IPCC, interrupt, memory, CPU, and alias paths required by this guest pair.",
        "The guests are authored synthetic test code. Their execution does not establish physical-device equivalence or timing equivalence.",
        "The APSS guest image contains a 0xa5 file prefix. The Lua loader selects the guest bytes with bin_file_offset and bin_file_size; the prefix is not mapped at the guest address.",
        "The expected exit code 2 follows the intentional UNMODELED_ACCESS at 0x600d0000. It does not indicate kernel entry, boot success, or security qualification.",
        "The host runs the Lua composition as trusted code. It is not a sandbox, and external dependencies must be declared by the environment author.",
    ] {
        writeln!(&mut text, "  {},", toml_string(limitation)).unwrap();
    }
    writeln!(&mut text, "]").unwrap();
    for (name, path, hash) in files {
        writeln!(&mut text).unwrap();
        writeln!(&mut text, "[[files]]").unwrap();
        writeln!(&mut text, "name = {}", toml_string(name)).unwrap();
        writeln!(&mut text, "path = {}", toml_string(path)).unwrap();
        writeln!(&mut text, "sha256 = {}", toml_string(hash)).unwrap();
    }
    text
}

fn toml_string(value: &str) -> String {
    let mut text = String::with_capacity(value.len() + 2);
    text.push('"');
    for character in value.chars() {
        match character {
            '"' => text.push_str("\\\""),
            '\\' => text.push_str("\\\\"),
            '\n' => text.push_str("\\n"),
            '\r' => text.push_str("\\r"),
            '\t' => text.push_str("\\t"),
            character if character.is_control() => {
                write!(&mut text, "\\u{:04X}", character as u32).unwrap();
            }
            character => text.push(character),
        }
    }
    text.push('"');
    text
}

fn check(options: &BTreeMap<String, String>) -> Result<(), String> {
    let out = PathBuf::from(&options["--out"]);
    let result_path = out.join("result.json");
    let stderr_path = out.join("stderr.log");
    if !result_path.is_file() {
        return Err(format!("experiment result is missing: {}", result_path.display()));
    }
    if !stderr_path.is_file() {
        return Err(format!("experiment stderr is missing: {}", stderr_path.display()));
    }

    let filter = "length == 1 and (.[0] | (.schema == 1) and (.outcome == \"exited\") and (.exit_code == 2) and (.signal == null) and (.timed_out == false) and (.timeout_seconds > 0) and (.error == null))";
    let jq = Command::new("jq")
        .args(["-e", "-s", filter])
        .arg(&result_path)
        .output()
        .map_err(|error| format!("cannot start jq: {error}; jq is required by this checker"))?;
    if !jq.status.success() {
        return Err(format!(
            "result.json does not record normal child exit 2 without timeout or error:\n{}{}",
            String::from_utf8_lossy(&jq.stdout),
            String::from_utf8_lossy(&jq.stderr)
        ));
    }

    let stderr = fs::read_to_string(&stderr_path)
        .map_err(|error| format!("cannot read {}: {error}", stderr_path.display()))?;
    if stderr.contains("0xbad00000") {
        return Err("stderr records the IPCC guest's bad-state failure address 0xbad00000".to_owned());
    }
    let mut accesses = stderr
        .lines()
        .filter(|line| line.starts_with("UNMODELED_ACCESS "));
    let access = accesses
        .next()
        .ok_or("The guest did not produce an UNMODELED_ACCESS record")?;
    if accesses.next().is_some() {
        return Err("Expected exactly one UNMODELED_ACCESS record".to_owned());
    }
    let mut addresses = access
        .split_ascii_whitespace()
        .filter_map(|token| token.strip_prefix("address="));
    if addresses.next() != Some(EXPECTED_ACCESS) || addresses.next().is_some() {
        return Err(format!(
            "The sole UNMODELED_ACCESS record must name address={EXPECTED_ACCESS}, got: {access}"
        ));
    }

    println!("The recorded process outcome and stderr match the expected IPCC sentinel.");
    println!("This check does not establish boot success, kernel entry, or security qualification.");
    Ok(())
}
