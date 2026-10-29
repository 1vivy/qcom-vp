mod experiment;
mod firmware;
mod handoff;
mod log_archive;
mod observer;
mod reconstruction;
mod references;
mod retention;
mod sparse;
mod storage;
mod uefi_depex;
mod uefi_map;

use anyhow::{bail, ensure, Context, Result};
use clap::{Parser, Subcommand};
use firmware::{AppliedPatch, Input, Manifest};
use serde::{Deserialize, Serialize};
use std::fs::{self, File, OpenOptions};
use std::io::Write;
use std::os::unix::process::ExitStatusExt;
use std::path::{Component, Path, PathBuf};
use std::process::{Child, Command, ExitStatus, Stdio};
use std::time::{Duration, Instant};

#[derive(Parser)]
#[command(
    version,
    about = "Prepare Qualcomm firmware and execute the virtual platform"
)]
struct Cli {
    #[command(subcommand)]
    command: Action,
}

#[derive(Subcommand)]
enum Action {
    /// Cap retained boot artifacts in an explicitly selected directory.
    Retain {
        #[arg(long)]
        root: PathBuf,
        #[arg(long, default_value_t = 64, value_parser = clap::value_parser!(u64).range(1..))]
        limit_gib: u64,
    },
    /// Check the firmware identities and assemble private storage.
    /// Both supported boards can use the common pinned TME runtime donor.
    Prepare {
        #[arg(long)]
        firmware: PathBuf,
        #[arg(long)]
        out: PathBuf,
    },
    /// Execute the firmware with independent mutable state.
    Run {
        #[arg(long)]
        prepared: PathBuf,
        #[arg(long)]
        out: PathBuf,
        /// Stop after the verified first kernel instruction. Do not detach.
        #[arg(
            long,
            value_parser = ["kernel-entry"],
            required_unless_present = "continue_after",
            conflicts_with = "continue_after"
        )]
        stop_at: Option<String>,
        /// Verify kernel entry, then detach and retain guest UART output.
        #[arg(
            long,
            value_parser = ["kernel-entry"],
            required_unless_present = "stop_at",
            conflicts_with = "stop_at"
        )]
        continue_after: Option<String>,
        #[arg(long, default_value_t = 180)]
        timeout_seconds: u64,
        #[arg(long, default_value_t = 0)]
        apss_gdb: u16,
        #[arg(long, default_value_t = 0)]
        tme_gdb: u16,
        #[arg(long, default_value_t = 0)]
        cpucp_gdb: u16,
        #[arg(long)]
        debug_pause: bool,
        /// Enable the stock Oplus UART record in the mutable LUN copy.
        #[arg(long)]
        enable_oem_uart: bool,
    },
    /// Map firmware volumes and dependencies from one UEFI image.
    UefiMap {
        /// UEFI image. The image can be a ZIP package, a raw volume, or a partition.
        #[arg(long)]
        image: PathBuf,
        /// ZIP member that holds the image.
        #[arg(long)]
        member: Option<String>,
        /// Output directory for the JSON, DOT, and Markdown files.
        #[arg(long)]
        out: PathBuf,
        /// UART or system log for load correlation. The tool accepts .zst files.
        #[arg(long)]
        log: Vec<PathBuf>,
        /// Directory or file with GUID name definitions.
        #[arg(long)]
        sdk: Vec<PathBuf>,
    },
    /// Run a declared research environment in a new output directory.
    /// The environment supplies inputs and provenance. The platform Lua file
    /// remains the machine description. This action does not verify boot.
    Experiment {
        /// Environment description in TOML schema 1.
        #[arg(long)]
        environment: PathBuf,
        /// New output directory for the input snapshot, logs, and result.
        #[arg(long)]
        out: PathBuf,
        /// System executable. The default is the sibling qcom-vp-system.
        #[arg(long)]
        system: Option<PathBuf>,
        #[arg(long, default_value_t = 30, value_parser = clap::value_parser!(u64).range(1..))]
        timeout_seconds: u64,
        /// Continue from the complete persistent state of a finished run.
        #[arg(long, conflicts_with = "clone_from")]
        resume_from: Option<PathBuf>,
        /// Copy a finished environment with its identity and persistent state.
        #[arg(long, conflicts_with = "resume_from")]
        clone_from: Option<PathBuf>,
    },
    /// Check evidence references in root Markdown, docs, and models against the manifest.
    References {
        /// Repository whose root Markdown, docs, and models the action scans.
        #[arg(long, default_value = ".")]
        repo: PathBuf,
        /// Reference manifest. The default is docs/references.toml in the repository.
        /// Relative store roots start at the manifest directory.
        #[arg(long)]
        manifest: Option<PathBuf>,
    },
}

#[derive(Serialize, Deserialize)]
struct DiskIdentity {
    filename: String,
    sparse_sha256: String,
}

fn default_cpucp_offset() -> u64 {
    firmware::DEFAULT_CPUCP_OFFSET
}

#[derive(Serialize, Deserialize)]
struct Prepared {
    schema: u32,
    inputs: Manifest,
    pbl: Input,
    tme: Input,
    /// Offset of the CPUCP image inside `pbl`.
    #[serde(default = "default_cpucp_offset")]
    cpucp_offset: u64,
    /// Board file copy. The run uses this copy, not an installed file.
    board_file: Input,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    tme_runtime: Option<PreparedTmeRuntime>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    sequencer_root: Option<Input>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    oem_root: Option<Input>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    calibration_fuses: Option<Input>,
    patches: Vec<AppliedPatch>,
    storage: storage::StorageReport,
    disks: Vec<DiskIdentity>,
    kernel: handoff::KernelIdentity,
}

#[derive(Serialize, Deserialize)]
struct PreparedTmeRuntime {
    original: Input,
    donor: Input,
    adapter: Input,
    api_map: Input,
    provenance: reconstruction::ReconstructionProvenance,
}

impl PreparedTmeRuntime {
    fn artifacts(&self) -> [&Input; 4] {
        [&self.original, &self.donor, &self.adapter, &self.api_map]
    }

    fn verify(&self, root: &Path, manifest: &Manifest, tme: &Input) -> Result<()> {
        let donor = manifest
            .tme_runtime_donor
            .as_ref()
            .context("The prepared TME runtime donor identity is absent")?;
        ensure!(
            self.original
                .sha256
                .eq_ignore_ascii_case(&manifest.tme.sha256)
                && self.donor.sha256.eq_ignore_ascii_case(&donor.sha256),
            "The prepared TME runtime inputs do not match the manifest"
        );
        for (input, name) in self.artifacts().into_iter().zip([
            "tme-original.bin",
            "tme-runtime-donor.bin",
            "tme-runtime-adapter.bin",
            "tme-runtime-api.map",
        ]) {
            ensure!(
                input.path == Path::new(name),
                "Invalid TME runtime artifact filename"
            );
            firmware::check_hash(&local_file(root, &input.path)?, &input.sha256)?;
        }
        let reconstructed = reconstruction::reconstruct(
            &fs::read(local_file(root, &self.original.path)?)?,
            &fs::read(local_file(root, &self.donor.path)?)?,
            &fs::read(local_file(root, &self.adapter.path)?)?,
        )?;
        ensure!(
            self.provenance == reconstructed.provenance
                && tme.sha256 == self.provenance.output_sha256
                && self.api_map.sha256 == self.provenance.api_map_sha256
                && tme.path == Path::new("tme.bin"),
            "The prepared TME reconstruction provenance does not match"
        );
        Ok(())
    }
}

fn prepare_tme_runtime(
    manifest: &Manifest,
    out: &Path,
    tme: &mut Input,
) -> Result<Option<PreparedTmeRuntime>> {
    let Some(donor) = &manifest.tme_runtime_donor else {
        return Ok(None);
    };
    let executable = std::env::current_exe()?.canonicalize()?;
    let adapter_path = executable
        .parent()
        .context("The executable has no parent directory")?
        .join("../share/qcom-vp/firmware/qcom/sm88xx/tme-runtime/tme-runtime-adapter.bin");
    let original_bytes = fs::read(local_file(out, &tme.path)?)?;
    let donor_bytes = fs::read(&donor.path)?;
    let adapter_bytes =
        fs::read(&adapter_path).context("Cannot read the installed TME runtime adapter")?;
    let reconstructed = reconstruction::reconstruct(&original_bytes, &donor_bytes, &adapter_bytes)?;
    let original = firmware::copy_input(
        &Input {
            path: local_file(out, &tme.path)?,
            sha256: manifest.tme.sha256.clone(),
        },
        &out.join("tme-original.bin"),
    )?;
    let donor = firmware::copy_input(donor, &out.join("tme-runtime-donor.bin"))?;
    let adapter = firmware::copy_input(
        &Input {
            path: adapter_path,
            sha256: reconstructed.provenance.adapter_sha256.clone(),
        },
        &out.join("tme-runtime-adapter.bin"),
    )?;
    let api_map = Input {
        path: PathBuf::from("tme-runtime-api.map"),
        sha256: reconstructed.provenance.api_map_sha256.clone(),
    };
    let mut map_file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(out.join(&api_map.path))?;
    map_file.write_all(reconstructed.api_map.as_bytes())?;
    map_file.sync_all()?;
    let mut image_file = OpenOptions::new()
        .write(true)
        .truncate(true)
        .open(local_file(out, &tme.path)?)?;
    image_file.write_all(&reconstructed.image)?;
    image_file.sync_all()?;
    tme.sha256
        .clone_from(&reconstructed.provenance.output_sha256);
    Ok(Some(PreparedTmeRuntime {
        original,
        donor,
        adapter,
        api_map,
        provenance: reconstructed.provenance,
    }))
}

fn save_json(path: &Path, value: &impl Serialize) -> Result<()> {
    let mut file = OpenOptions::new().write(true).create_new(true).open(path)?;
    serde_json::to_writer_pretty(&mut file, value)?;
    file.write_all(b"\n")?;
    file.sync_all()?;
    Ok(())
}

fn create_output(path: &Path) -> Result<PathBuf> {
    fs::create_dir(path)
        .with_context(|| format!("Cannot create new output directory {}", path.display()))?;
    path.canonicalize()
        .context("Cannot resolve the output directory")
}

fn local_file(root: &Path, name: &Path) -> Result<PathBuf> {
    ensure!(
        name.components().count() == 1
            && matches!(name.components().next(), Some(Component::Normal(_))),
        "The prepared filename is not local"
    );
    let path = root.join(name);
    ensure!(
        fs::symlink_metadata(&path)?.file_type().is_file(),
        "The prepared input is not a regular file"
    );
    Ok(path)
}

fn describe(prepared: &Prepared) {
    println!(
        "platform={} board={} auth={} rom_compatibility={}",
        prepared.inputs.platform,
        prepared.inputs.board,
        prepared.inputs.auth,
        prepared.inputs.rom_compatibility
    );
    println!("package_sha256={}", prepared.inputs.package.sha256);
    println!(
        "pbl_sha256={} tme_sha256={}",
        prepared.pbl.sha256, prepared.tme.sha256
    );
    if let Some(runtime) = &prepared.tme_runtime {
        println!(
            "tme_runtime_provenance={}",
            serde_json::to_string(&runtime.provenance).expect("Hash identities serialize")
        );
    }
    match (
        &prepared.sequencer_root,
        &prepared.inputs.sequencer_entitlement_sha256,
    ) {
        (Some(root), Some(digest)) => println!(
            "sequencer_root_sha256={} sequencer_entitlement_sha256={}",
            root.sha256, digest
        ),
        _ => println!("sequencer_trust=absent"),
    }
    if let Some(root) = &prepared.oem_root {
        println!("oem_root_sha256={}", root.sha256);
    } else {
        println!("oem_root=absent");
    }
    println!("authentication_qualification=not-established");
    for record in &prepared.storage.omitted_programs {
        println!(
            "substitute=omit-secdata record={}",
            serde_json::to_string(record).expect("String records serialize")
        );
    }
    for patch in &prepared.patches {
        println!(
            "patch={} target={} result_sha256={} reason={}",
            patch.name, patch.target, patch.result_sha256, patch.reason
        );
    }
}

/// Copies the board file into the preparation. A manifest `board_file` takes precedence over the installed file for the label.
fn prepare_board(manifest: &Manifest, out: &Path) -> Result<Input> {
    let source = match &manifest.board_file {
        Some(file) => file.path.clone(),
        None => {
            let executable = std::env::current_exe()?.canonicalize()?;
            let boards = executable
                .parent()
                .context("The executable has no parent directory")?
                .join("../share/qcom-vp/boards/qcom/sm88xx");
            let (vendor, device) = manifest.board.split_once('-').with_context(|| {
                format!(
                    "Board '{}' has no installed board file. Set board_file in the manifest",
                    manifest.board
                )
            })?;
            boards
                .join(vendor)
                .join(format!("{device}.lua"))
                .canonicalize()
                .with_context(|| {
                    format!(
                        "Board '{}' has no installed board file. Set board_file in the manifest",
                        manifest.board
                    )
                })?
        }
    };
    ensure!(
        fs::read_to_string(&source)
            .with_context(|| format!("Cannot read {}", source.display()))?
            .contains("ufs_boot_lun"),
        "The board file must define ufs_boot_lun"
    );
    let sha256 = firmware::hash(&source)?;
    firmware::copy_input(&Input { path: source, sha256 }, &out.join("board.lua"))
}

fn prepare(path: &Path, out: &Path) -> Result<()> {
    let manifest = Manifest::load(path)?;
    println!(
        "platform={} board={} auth={} package_sha256={}",
        manifest.platform, manifest.board, manifest.auth, manifest.package.sha256
    );
    let out = create_output(out)?;
    retention::enforce(out.parent().context("The output parent is absent")?)?;
    // Assembly requires an empty directory. Retention cannot select an output without its completed manifest.
    let storage = storage::assemble(&manifest.package.path, &out, manifest.omit_secdata)?;
    let _output_guard = retention::Guard::acquire(&out)?;
    save_json(&out.join("inputs.json"), &manifest)?;
    let mut pbl = firmware::copy_rom(&manifest.pbl, firmware::PBL_SIZE, &out.join("pbl.bin"))?;
    let mut tme = firmware::copy_rom(&manifest.tme, firmware::TME_SIZE, &out.join("tme.bin"))?;
    let board_file = prepare_board(&manifest, &out)?;
    let sequencer_root = match &manifest.sequencer_root {
        Some(root) => {
            let destination = out.join("sequencer-root.bin");
            let copied = firmware::copy_input(root, &destination)?;
            firmware::check_sequencer_root(&destination, &copied.sha256)?;
            Some(copied)
        }
        None => None,
    };
    let oem_root = manifest
        .oem_root
        .as_ref()
        .map(|root| firmware::copy_input(root, &out.join("oem-root.der")))
        .transpose()?;
    let calibration_fuses = manifest
        .calibration_fuses
        .as_ref()
        .map(|page| firmware::copy_input(page, &out.join("calibration-fuses.bin")))
        .transpose()?;
    let mut patches = Vec::new();
    // Apply generic patches in manifest order before reconstruction. The pinned
    // reconstruction rejects any patch that changes its required TME preimage.
    for recipe in &manifest.patches {
        let applied = firmware::apply_patch(recipe, &out)?;
        match applied.target.as_str() {
            "pbl" => pbl.sha256.clone_from(&applied.result_sha256),
            "tme" => tme.sha256.clone_from(&applied.result_sha256),
            _ => unreachable!(),
        }
        patches.push(applied);
    }
    let tme_runtime = prepare_tme_runtime(&manifest, &out, &mut tme)?;
    let boot_member = storage
        .consumed_members
        .iter()
        .find(|member| member.name == "IMAGES/boot.img")
        .context("The prepared medium does not contain the selected boot image")?;
    let kernel = handoff::extract_reference(&manifest.package.path, &boot_member.name, &out)?;
    firmware::check_hash(&manifest.package.path, &manifest.package.sha256)?;
    let mut disks = Vec::new();
    for lun in &storage.luns {
        let path = local_file(&out, Path::new(&lun.filename))?;
        disks.push(DiskIdentity {
            filename: lun.filename.clone(),
            sparse_sha256: sparse::hash(&path)?,
        });
    }
    let prepared = Prepared {
        schema: 2,
        cpucp_offset: manifest.cpucp_offset(),
        inputs: manifest,
        pbl,
        tme,
        board_file,
        tme_runtime,
        sequencer_root,
        oem_root,
        calibration_fuses,
        patches,
        storage,
        disks,
        kernel,
    };
    describe(&prepared);
    save_json(&out.join("prepared.json"), &prepared)?;
    println!("Preparation passed: {}", out.display());
    Ok(())
}

fn lua_string(path: &Path) -> Result<String> {
    let text = path.to_str().context("The Lua path is not UTF-8")?;
    let mut result = String::from("\"");
    for byte in text.bytes() {
        match byte {
            b'"' => result.push_str("\\\""),
            b'\\' => result.push_str("\\\\"),
            32..=126 => result.push(byte as char),
            _ => result.push_str(&format!("\\{byte:03}")),
        }
    }
    result.push('"');
    Ok(result)
}

struct RunOptions {
    timeout_seconds: u64,
    ports: [u16; 3],
    debug_pause: bool,
    continue_after_entry: bool,
    enable_oem_uart: bool,
}

/// Reaps the platform on every return path, including setup and observer errors.
struct PlatformProcess {
    child: Child,
    status: Option<ExitStatus>,
}

impl PlatformProcess {
    fn poll(&mut self) -> Result<Option<ExitStatus>> {
        if self.status.is_none() {
            self.status = self
                .child
                .try_wait()
                .context("Cannot observe the platform process")?;
        }
        Ok(self.status)
    }

    /// Return whether the host sent a termination request.
    fn stop(&mut self) -> Result<bool> {
        if self.poll()?.is_some() {
            return Ok(false);
        }
        if let Err(error) = self.child.kill() {
            if self.poll()?.is_some() {
                return Ok(false);
            }
            return Err(error).context("Cannot stop the platform process");
        }
        self.status = Some(
            self.child
                .wait()
                .context("Cannot reap the platform process")?,
        );
        Ok(true)
    }
}

impl Drop for PlatformProcess {
    fn drop(&mut self) {
        if self.status.is_none() {
            let _ = self.child.kill();
            let _ = self.child.wait();
        }
    }
}

fn run(prepared_root: &Path, out: &Path, options: RunOptions) -> Result<()> {
    ensure!(
        options.timeout_seconds > 0,
        "The execution timeout must be positive"
    );
    for (i, port) in options.ports.iter().enumerate() {
        ensure!(
            *port == 0 || !options.ports[..i].contains(port),
            "Debugger ports must be distinct"
        );
    }
    ensure!(
        !options.debug_pause || options.ports.iter().all(|port| *port != 0),
        "A paused run requires all three debugger ports"
    );
    ensure!(
        !options.continue_after_entry || (options.ports[0] == 0 && !options.debug_pause),
        "Continuation requires the automatic APSS observer"
    );
    let root = prepared_root.canonicalize()?;
    let _source_guard = retention::Guard::acquire(&root)?;
    let mut prepared: Prepared =
        serde_json::from_reader(File::open(local_file(&root, Path::new("prepared.json"))?)?)?;
    ensure!(
        prepared.schema == 2 && prepared.inputs.schema == 1,
        "Unsupported prepared schema"
    );
    ensure!(
        prepared.inputs.auth == "retail",
        "The prepared authentication policy conflicts with the active repository rule"
    );
    prepared.inputs.check_target()?;
    ensure!(
        prepared.tme_runtime.is_some() == prepared.inputs.tme_runtime_donor.is_some(),
        "The prepared TME runtime artifacts are incomplete"
    );
    if let Some(runtime) = &prepared.tme_runtime {
        runtime.verify(&root, &prepared.inputs, &prepared.tme)?;
    }
    prepared.inputs.check_sequencer_trust()?;
    ensure!(
        prepared.sequencer_root.is_some() == prepared.inputs.sequencer_root.is_some(),
        "The prepared sequencer trust inputs are incomplete"
    );
    if let Some(sequencer_root) = &prepared.sequencer_root {
        ensure!(
            prepared
                .inputs
                .sequencer_root
                .as_ref()
                .map(|root| &root.sha256)
                == Some(&sequencer_root.sha256),
            "The prepared sequencer root identity does not match the input"
        );
        ensure!(
            sequencer_root.path.as_path() == Path::new("sequencer-root.bin"),
            "The prepared sequencer root filename is invalid"
        );
        firmware::check_sequencer_root(
            &local_file(&root, &sequencer_root.path)?,
            &sequencer_root.sha256,
        )?;
    }
    ensure!(
        prepared.oem_root.is_some() == prepared.inputs.oem_root.is_some(),
        "The prepared OEM root input is incomplete"
    );
    if let Some(oem_root) = &prepared.oem_root {
        ensure!(
            prepared
                .inputs
                .oem_root
                .as_ref()
                .map(|input| input.sha256.eq_ignore_ascii_case(&oem_root.sha256))
                == Some(true),
            "The prepared OEM root identity does not match the input"
        );
        ensure!(
            oem_root.path.as_path() == Path::new("oem-root.der"),
            "The prepared OEM root filename is invalid"
        );
        firmware::check_hash(&local_file(&root, &oem_root.path)?, &oem_root.sha256)?;
    }
    ensure!(
        prepared.calibration_fuses.is_some() == prepared.inputs.calibration_fuses.is_some(),
        "The prepared calibration fuse page is incomplete"
    );
    if let Some(page) = &prepared.calibration_fuses {
        ensure!(
            prepared.inputs.platform == "sm8850",
            "The private calibration page requires SM8850"
        );
        ensure!(
            prepared
                .inputs
                .calibration_fuses
                .as_ref()
                .map(|input| input.sha256.eq_ignore_ascii_case(&page.sha256))
                == Some(true),
            "The prepared calibration fuse page identity does not match the input"
        );
        ensure!(
            page.path.as_path() == Path::new("calibration-fuses.bin"),
            "The prepared calibration fuse page filename is invalid"
        );
        let path = local_file(&root, &page.path)?;
        ensure!(
            fs::metadata(&path)?.len() == firmware::CALIBRATION_PAGE_SIZE,
            "The calibration fuse page must contain 4096 bytes"
        );
        firmware::check_hash(&path, &page.sha256)?;
    }
    ensure!(
        prepared.disks.len() == prepared.storage.luns.len(),
        "The prepared LUN identities are incomplete"
    );
    for (disk, lun) in prepared.disks.iter().zip(&prepared.storage.luns) {
        ensure!(
            disk.filename == lun.filename,
            "The prepared LUN identities do not match"
        );
        let path = local_file(&root, Path::new(&disk.filename))?;
        ensure!(
            fs::metadata(&path)?.len()
                == lun
                    .sectors
                    .checked_mul(lun.sector_size)
                    .context("The LUN size overflows")?,
            "The prepared LUN size does not match"
        );
        ensure!(
            sparse::hash(&path)? == disk.sparse_sha256,
            "The prepared LUN hash does not match: {}",
            disk.filename
        );
    }
    for input in [&prepared.pbl, &prepared.tme, &prepared.board_file] {
        firmware::check_hash(&local_file(&root, &input.path)?, &input.sha256)?;
    }
    let executable = std::env::current_exe()?.canonicalize()?;
    let bin = executable
        .parent()
        .context("The executable has no parent directory")?;
    let system = bin.join("qcom-vp-system");
    ensure!(
        system.is_file(),
        "Cannot find the sibling executable {}",
        system.display()
    );
    let platform = bin
        .join("../share/qcom-vp/platforms/qcom/sm88xx")
        .join(&prepared.inputs.platform)
        .join("platform.lua")
        .canonicalize()
        .context("Cannot locate the installed platform data")?;
    let out = create_output(out)?;
    let _output_guard = retention::Guard::acquire(&out)?;
    retention::enforce(out.parent().context("The output parent is absent")?)?;
    describe(&prepared);
    save_json(&out.join("prepared.json"), &prepared)?;
    for input in [&prepared.pbl, &prepared.tme, &prepared.board_file] {
        firmware::copy_input(
            &Input {
                path: local_file(&root, &input.path)?,
                sha256: input.sha256.clone(),
            },
            &out.join(&input.path),
        )?;
    }
    let board = out.join(&prepared.board_file.path);
    if let Some(runtime) = &prepared.tme_runtime {
        for input in runtime.artifacts() {
            firmware::copy_input(
                &Input {
                    path: local_file(&root, &input.path)?,
                    sha256: input.sha256.clone(),
                },
                &out.join(&input.path),
            )?;
        }
    }
    if let Some(sequencer_root) = &prepared.sequencer_root {
        firmware::copy_input(
            &Input {
                path: local_file(&root, &sequencer_root.path)?,
                sha256: sequencer_root.sha256.clone(),
            },
            &out.join(&sequencer_root.path),
        )?;
    }
    if let Some(oem_root) = &prepared.oem_root {
        firmware::copy_input(
            &Input {
                path: local_file(&root, &oem_root.path)?,
                sha256: oem_root.sha256.clone(),
            },
            &out.join(&oem_root.path),
        )?;
    }
    if let Some(page) = &prepared.calibration_fuses {
        firmware::copy_input(
            &Input {
                path: local_file(&root, &page.path)?,
                sha256: page.sha256.clone(),
            },
            &out.join(&page.path),
        )?;
    }
    for disk in &prepared.disks {
        let target = out.join(&disk.filename);
        sparse::copy(&local_file(&root, Path::new(&disk.filename))?, &target)?;
        ensure!(
            sparse::hash(&target)? == disk.sparse_sha256,
            "The mutable LUN copy does not match: {}",
            disk.filename
        );
    }
    let automatic_observation = options.ports[0] == 0 && !options.debug_pause;
    let sites = if automatic_observation {
        Some(observer::Sites::load(
            &prepared.inputs.package.path,
            &prepared.kernel,
        )?)
    } else {
        None
    };
    let oem_uart_record = if options.enable_oem_uart {
        let mut candidates = prepared.storage.luns.iter().filter(|lun| {
            lun.partitions
                .iter()
                .any(|partition| partition.name == storage::OPLUS_UART_PARTITION)
        });
        let lun = candidates
            .next()
            .context("The OEM UART partition is absent")?;
        ensure!(
            candidates.next().is_none(),
            "The OEM UART partition occurs in multiple LUNs"
        );
        let mut file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(local_file(&out, Path::new(&lun.filename))?)?;
        let record = storage::enable_oplus_uart(&mut file, lun)?;
        println!(
            "oem_uart=enabled lun={} record_lba={}",
            record.lun, record.record_lba
        );
        save_json(&out.join("oem-uart.json"), &record)?;
        Some(record)
    } else {
        None
    };
    let observer_directory = if automatic_observation {
        Some(observer::SocketDirectory::new()?)
    } else {
        None
    };
    let mut config = format!("local board = dofile({})\nplatform = dofile({})({{\n pbl = {},\n tme = {},\n trace = {},\n apss_gdb = {},\n tme_gdb = {},\n cpucp_gdb = {},\n debug_pause = {},\n boot_enable = {},\n boot_lun_en = board.ufs_boot_lun,\n luns = {{\n", lua_string(&board)?, lua_string(&platform)?, lua_string(&out.join(&prepared.pbl.path))?, lua_string(&out.join(&prepared.tme.path))?, lua_string(&out.join("qemu"))?, options.ports[0], options.ports[1], options.ports[2], options.debug_pause, prepared.storage.boot_enable);
    for lun in &prepared.storage.luns {
        config.push_str(&format!(
            "  [{}] = {},\n",
            lun.lun,
            lua_string(&out.join(&lun.filename))?
        ));
    }
    config.push_str(&format!(
        " }},\n pbl_cpucp_offset = {},\n boot_lun_ids = {{\n",
        prepared.cpucp_offset
    ));
    for lun in &prepared.storage.luns {
        config.push_str(&format!("  [{}] = {},\n", lun.lun, lun.boot_lun_id));
    }
    config.push_str(" },\n");
    config.push_str(&format!(
        " oem_hw_key_hex = board.oem_hw_key_hex,\n virtual_root_hex = board.virtual_root_hex,\n rpmb_size_kib = board.rpmb_size_kib,\n rpmb_state_file = board.rpmb_size_kib == 0 and '' or {},\n",
        lua_string(&out.join("rpmb.state"))?
    ));
    if let Some(runtime) = &prepared.tme_runtime {
        config.push_str(&format!(
            " tme_runtime_api_map = {},\n",
            lua_string(&out.join(&runtime.api_map.path))?
        ));
    }
    if let Some(directory) = &observer_directory {
        config.push_str(&format!(
            " observer_socket = {},\n observer_clock_socket = {},\n",
            lua_string(directory.socket())?,
            lua_string(directory.clock_socket())?
        ));
    }
    if let Some(page) = &prepared.calibration_fuses {
        config.push_str(&format!(
            " calibration_fuses = {},\n",
            lua_string(&out.join(&page.path))?
        ));
    }
    config.push_str("})\n");
    if let Some(sequencer_root) = &prepared.sequencer_root {
        let digest = prepared
            .inputs
            .sequencer_entitlement_sha256
            .as_deref()
            .context("The prepared sequencer entitlement SHA-256 is absent")?;
        config.push_str(&format!(
            "platform.tme_boot_status.trusted_root = {}\n",
            lua_string(&out.join(&sequencer_root.path))?
        ));
        config.push_str(&format!(
            "platform.tme_boot_status.entitlement_sha256 = \"{digest}\"\n"
        ));
    }
    if let Some(oem_root) = &prepared.oem_root {
        let digest = firmware::hash384(&out.join(&oem_root.path))?;
        config.push_str(&format!(
            "platform.fuses.oem_pk_hash_sha384 = \"{digest}\"\n"
        ));
    }
    let config_path = out.join("run.lua");
    fs::write(&config_path, config)?;
    let stdout = File::create(out.join("console.log"))?;
    let stderr = File::create(out.join("system.log"))?;
    let deadline = Instant::now()
        .checked_add(Duration::from_secs(options.timeout_seconds))
        .context("The execution timeout overflows")?;
    let mut child = PlatformProcess {
        child: Command::new(&system)
            .arg("-l")
            .arg(&config_path)
            .stdin(Stdio::null())
            .stdout(stdout)
            .stderr(stderr)
            .spawn()
            .with_context(|| format!("Cannot execute {}", system.display()))?,
        status: None,
    };
    let mut observation = None;
    let mut paused_entry = None;
    let mut kernel_entry = false;
    let mut detach_confirmed = false;
    let mut termination = "observer-failure";
    let execution: Result<()> = (|| {
        observation = sites
            .zip(observer_directory.as_ref())
            .map(|(sites, directory)| {
                observer::start(
                    directory.socket().to_owned(),
                    directory.clock_socket().to_owned(),
                    out.clone(),
                    sites,
                    prepared.kernel.clone(),
                    deadline,
                )
            })
            .transpose()?;
        loop {
            if Instant::now() >= deadline {
                termination = "timeout";
                bail!("The platform exceeded the execution timeout");
            }
            if observation
                .as_ref()
                .is_some_and(observer::Observation::is_finished)
            {
                termination = "observer-failure";
                let entry = observation
                    .take()
                    .context("The APSS observer is absent")?
                    .finish()?;
                kernel_entry = true;
                paused_entry = Some(entry);
                if Instant::now() >= deadline {
                    termination = "timeout";
                    bail!("The platform exceeded the execution timeout");
                }
                if !options.continue_after_entry {
                    termination = "kernel-entry-stop";
                    return Ok(());
                }
                termination = "detach-failure";
                let detached = paused_entry
                    .take()
                    .context("The verified entry is absent")?
                    .detach();
                detach_confirmed = detached.is_ok();
                if Instant::now() >= deadline {
                    termination = "timeout";
                    detached?;
                    bail!("The platform exceeded the execution timeout");
                }
                detached?;
            }
            termination = "process-error";
            if let Some(status) = child.poll()? {
                termination = "platform-exit";
                ensure!(
                    kernel_entry && detach_confirmed,
                    "The platform exited with {status} before kernel observation completed"
                );
                return Ok(());
            }
            std::thread::sleep(Duration::from_millis(20));
        }
    })();
    let mut failures = Vec::new();
    if let Err(error) = execution {
        if Instant::now() >= deadline
            && matches!(termination, "observer-failure" | "detach-failure")
        {
            termination = "timeout";
        }
        failures.push(format!("{error:#}"));
    }
    let host_terminated = match child.stop() {
        Ok(stopped) => stopped,
        Err(error) => {
            failures.push(format!("{error:#}"));
            false
        }
    };
    let status = child.status;
    drop(child);
    // Join before removing the private socket or writing the final result.
    // A proof completed during process exit still remains entry evidence.
    if let Some(observer) = observation {
        observer.cancel();
        match observer.finish() {
            Ok(entry) => {
                kernel_entry = true;
                paused_entry = Some(entry);
            }
            Err(error) => failures.push(format!("{error:#}")),
        }
    }
    // In stop mode, retain the debugger connection until the child is reaped.
    drop(paused_entry);
    if let Some(status) = status {
        if !status.success() && !(host_terminated && status.signal() == Some(9)) {
            failures.push(format!("The platform exited with {status}"));
        }
    }
    let uart_bytes = match fs::metadata(out.join("qemu-uart.log")) {
        Ok(metadata) => Some(metadata.len()),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => None,
        Err(error) => {
            failures.push(format!("Cannot inspect the retained UART log: {error}"));
            None
        }
    };
    let succeeded = failures.is_empty();
    save_json(
        &out.join("result.json"),
        &serde_json::json!({
            "kernel_entry": kernel_entry,
            "run_succeeded": succeeded,
            "mode": if options.continue_after_entry { "continue-after-kernel-entry" } else { "stop-at-kernel-entry" },
            "detach_confirmed": detach_confirmed,
            "termination": termination,
            "host_terminated": host_terminated,
            "exit_code": status.and_then(|status| status.code()),
            "exit_signal": status.and_then(|status| status.signal()),
            "observer": automatic_observation,
            "observation_time_policy": if automatic_observation { "suspend-during-inspection" } else { "unchanged" },
            "uart_log": "qemu-uart.log",
            "uart_bytes": uart_bytes,
            "oem_uart_record": oem_uart_record,
            "reason": if succeeded { None } else { Some(failures.join("\n")) },
        }),
    )?;
    ensure!(
        succeeded,
        "{}. Kernel entry verified: {kernel_entry}. Logs: {}",
        failures.join("\n"),
        out.display()
    );
    println!(
        "Kernel entry verified. {} Logs: {}",
        if options.continue_after_entry {
            "The platform exited successfully after detach. Later kernel boot is not verified."
        } else {
            "The run stopped at verified entry without detach."
        },
        out.display()
    );
    Ok(())
}

fn main() -> std::process::ExitCode {
    let action = Cli::parse().command;
    let retention_root = match &action {
        Action::Retain { root, .. } => Some(root.clone()),
        Action::Prepare { out, .. } | Action::Run { out, .. } => {
            Some(
                out.parent()
                    .filter(|parent| !parent.as_os_str().is_empty())
                    .unwrap_or_else(|| Path::new("."))
                    .to_path_buf(),
            )
        }
        Action::Experiment { .. } | Action::UefiMap { .. } | Action::References { .. } => None,
    };
    let mut result = match action {
        Action::Retain { root, limit_gib } => limit_gib
            .checked_mul(1 << 30)
            .context("The artifact limit overflows")
            .and_then(|limit| retention::configure(&root, limit)),
        Action::Prepare { firmware, out } => prepare(&firmware, &out),
        Action::Run {
            prepared,
            out,
            stop_at: _,
            continue_after,
            timeout_seconds,
            apss_gdb,
            tme_gdb,
            cpucp_gdb,
            debug_pause,
            enable_oem_uart,
        } => run(
            &prepared,
            &out,
            RunOptions {
                timeout_seconds,
                ports: [apss_gdb, tme_gdb, cpucp_gdb],
                debug_pause,
                continue_after_entry: continue_after.is_some(),
                enable_oem_uart,
            },
        ),
        Action::UefiMap {
            image,
            member,
            out,
            log,
            sdk,
        } => uefi_map::run(&uefi_map::Options {
            image,
            member,
            out,
            logs: log,
            sdk,
        }),
        Action::Experiment {
            environment,
            out,
            system,
            timeout_seconds,
            resume_from,
            clone_from,
        } => experiment::run(&experiment::Options {
            environment,
            out,
            system,
            timeout_seconds,
            resume_from,
            clone_from,
        }),
        Action::References { repo, manifest } => {
            references::run(&references::Options { repo, manifest })
        }
    };
    if let Some(root) = &retention_root {
        if let Err(cleanup) = retention::enforce(root) {
            result = match result {
                Ok(()) => Err(cleanup),
                Err(error) => Err(error.context(format!("Artifact retention failed: {cleanup:#}"))),
            };
        }
    }
    match result {
        Ok(()) => std::process::ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("qcom-vp: {error:#}");
            std::process::ExitCode::FAILURE
        }
    }
}
