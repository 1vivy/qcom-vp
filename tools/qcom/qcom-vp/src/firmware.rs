use anyhow::{bail, ensure, Context, Result};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256, Sha384};
use std::fs::{self, File, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Input {
    pub path: PathBuf,
    pub sha256: String,
}

pub const PLATFORMS: [&str; 2] = ["sm8850", "sm8845"];

/// A ROM image at a file offset inside a possibly larger dump.
/// The SHA-256 covers the image only, not the whole file.
#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Rom {
    pub path: PathBuf,
    pub sha256: String,
    /// File offset of the ROM image.
    #[serde(default)]
    pub offset: u64,
    /// PBL only: offset of the CPUCP image inside the PBL image.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub cpucp_offset: Option<u64>,
}

pub const PBL_SIZE: u64 = 0x3c000;
pub const TME_SIZE: u64 = 0x20000;
pub const CPUCP_SIZE: u64 = 0x8000;
pub const DEFAULT_CPUCP_OFFSET: u64 = 0x34000;

impl Rom {
    /// Reads the image, checks its range, and checks its SHA-256.
    pub fn read(&self, size: u64) -> Result<Vec<u8>> {
        ensure!(
            self.sha256.len() == 64 && self.sha256.bytes().all(|b| b.is_ascii_hexdigit()),
            "Invalid SHA-256 for {}",
            self.path.display()
        );
        let mut file = File::open(&self.path)
            .with_context(|| format!("Cannot open {}", self.path.display()))?;
        let end = self
            .offset
            .checked_add(size)
            .context("The ROM range overflows")?;
        ensure!(
            file.metadata()?.len() >= end,
            "{} must contain at least {:#x} bytes to hold a {:#x}-byte ROM image at offset {:#x}",
            self.path.display(),
            end,
            size,
            self.offset
        );
        file.seek(SeekFrom::Start(self.offset))?;
        let mut bytes = vec![0_u8; size as usize];
        file.read_exact(&mut bytes)?;
        let actual = format!("{:x}", Sha256::digest(&bytes));
        ensure!(
            actual.eq_ignore_ascii_case(&self.sha256),
            "SHA-256 mismatch for the ROM image in {} at offset {:#x}: expected {}, received {}",
            self.path.display(),
            self.offset,
            self.sha256,
            actual
        );
        Ok(bytes)
    }
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Manifest {
    pub schema: u32,
    pub platform: String,
    /// Free label for the device. The label selects the installed board file unless `board_file` is set.
    pub board: String,
    pub auth: String,
    #[serde(default)]
    pub rom_compatibility: bool,
    #[serde(default)]
    pub omit_secdata: bool,
    pub pbl: Rom,
    pub tme: Rom,
    /// Private runtime donor for the common pinned TME reconstruction.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tme_runtime_donor: Option<Input>,
    pub package: Input,
    /// Optional board file. The file replaces the installed file that the label names.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub board_file: Option<Input>,
    /// Private raw QFPROM calibration page for SM8850.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub calibration_fuses: Option<Input>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub oem_root: Option<Input>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub sequencer_root: Option<Input>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub sequencer_entitlement_sha256: Option<String>,
    #[serde(default)]
    pub patches: Vec<PathBuf>,
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Patch {
    pub schema: u32,
    pub name: String,
    pub target: String,
    pub input_sha256: String,
    pub offset: u64,
    pub expected_span_sha256: String,
    pub replacement_hex: String,
    pub reason: String,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct AppliedPatch {
    pub name: String,
    pub target: String,
    pub reason: String,
    pub recipe_sha256: String,
    pub result_sha256: String,
}

fn digest_file<D: Digest>(path: &Path) -> Result<sha2::digest::Output<D>> {
    let mut file = File::open(path).with_context(|| format!("Cannot open {}", path.display()))?;
    let mut digest = D::new();
    let mut buffer = [0_u8; 131072];
    loop {
        let n = file.read(&mut buffer)?;
        if n == 0 {
            break;
        }
        digest.update(&buffer[..n]);
    }
    Ok(digest.finalize())
}

pub fn hash(path: &Path) -> Result<String> {
    Ok(format!("{:x}", digest_file::<Sha256>(path)?))
}

pub fn hash384(path: &Path) -> Result<String> {
    Ok(format!("{:x}", digest_file::<Sha384>(path)?))
}

pub fn check_hash(path: &Path, expected: &str) -> Result<()> {
    ensure!(
        expected.len() == 64 && expected.bytes().all(|b| b.is_ascii_hexdigit()),
        "Invalid SHA-256 for {}",
        path.display()
    );
    let actual = hash(path)?;
    ensure!(
        actual.eq_ignore_ascii_case(expected),
        "SHA-256 mismatch for {}: expected {}, received {}",
        path.display(),
        expected,
        actual
    );
    Ok(())
}

pub fn check_sequencer_root(path: &Path, expected: &str) -> Result<()> {
    check_hash(path, expected)?;
    ensure!(
        fs::metadata(path)?.len() == 136,
        "The sequencer root must contain 136 bytes (P-521 x68BE || y68BE)"
    );
    Ok(())
}

/// The private calibration page covers raw QFPROM `0x221c8000..0x221c8fff`.
pub const CALIBRATION_PAGE_SIZE: u64 = 4096;

fn absolute(base: &Path, path: &Path) -> Result<PathBuf> {
    let joined = if path.is_absolute() {
        path.to_owned()
    } else {
        base.join(path)
    };
    joined
        .canonicalize()
        .with_context(|| format!("Cannot resolve {}", joined.display()))
}

impl Manifest {
    /// Checks the platform, the board label, and the ROM compatibility label.
    pub fn check_target(&self) -> Result<()> {
        ensure!(
            PLATFORMS.contains(&self.platform.as_str()),
            "Unsupported platform '{}'. The supported platforms are {}",
            self.platform,
            PLATFORMS.join(" and ")
        );
        ensure!(
            !self.board.is_empty()
                && !self.board.starts_with('-')
                && self
                    .board
                    .bytes()
                    .all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'-'),
            "The board label must use lowercase letters, digits, and hyphens"
        );
        ensure!(
            self.rom_compatibility == (self.platform == "sm8845"),
            "The ROM compatibility label must be true for sm8845 and false for sm8850"
        );
        Ok(())
    }

    /// Offset of the CPUCP image inside the PBL image.
    pub fn cpucp_offset(&self) -> u64 {
        self.pbl.cpucp_offset.unwrap_or(DEFAULT_CPUCP_OFFSET)
    }

    pub fn check_sequencer_trust(&mut self) -> Result<()> {
        ensure!(
            self.sequencer_root.is_some() == self.sequencer_entitlement_sha256.is_some(),
            "The sequencer root and entitlement SHA-256 must both be present or both be absent"
        );
        if let Some(digest) = &mut self.sequencer_entitlement_sha256 {
            ensure!(
                digest.len() == 64 && digest.bytes().all(|b| b.is_ascii_hexdigit()),
                "The sequencer entitlement SHA-256 must contain 64 hexadecimal characters"
            );
            digest.make_ascii_lowercase();
        }
        Ok(())
    }

    pub fn load(path: &Path) -> Result<Self> {
        let path = path.canonicalize()?;
        let base = path
            .parent()
            .context("The manifest has no parent directory")?;
        let mut manifest: Self = toml::from_str(&fs::read_to_string(&path)?)?;
        ensure!(
            manifest.schema == 1,
            "Unsupported firmware schema {}",
            manifest.schema
        );
        manifest.check_target()?;
        ensure!(manifest.auth == "retail", "Authentication policy '{}' conflicts with the active repository rule. The rule requires retail authentication", manifest.auth);
        manifest.check_sequencer_trust()?;
        if let Some(root) = &mut manifest.sequencer_root {
            root.path = absolute(base, &root.path)?;
            check_sequencer_root(&root.path, &root.sha256)?;
        }
        if let Some(root) = &mut manifest.oem_root {
            root.path = absolute(base, &root.path)?;
            check_hash(&root.path, &root.sha256)?;
        }
        if let Some(page) = &mut manifest.calibration_fuses {
            ensure!(
                manifest.platform == "sm8850",
                "The private calibration page requires SM8850"
            );
            page.path = absolute(base, &page.path)?;
            check_hash(&page.path, &page.sha256)?;
            ensure!(
                fs::metadata(&page.path)?.len() == CALIBRATION_PAGE_SIZE,
                "The calibration fuse page must contain 4096 bytes"
            );
        }
        ensure!(
            manifest.tme.cpucp_offset.is_none(),
            "cpucp_offset applies to [pbl] only"
        );
        if let Some(offset) = manifest.pbl.cpucp_offset {
            ensure!(
                offset.checked_add(CPUCP_SIZE).is_some_and(|end| end <= PBL_SIZE),
                "The CPUCP image at cpucp_offset {:#x} must lie inside the PBL image",
                offset
            );
        }
        for rom in [&mut manifest.pbl, &mut manifest.tme] {
            rom.path = absolute(base, &rom.path)?;
        }
        manifest.pbl.read(PBL_SIZE)?;
        manifest.tme.read(TME_SIZE)?;
        let package = &mut manifest.package;
        package.path = absolute(base, &package.path)?;
        check_hash(&package.path, &package.sha256)?;
        if let Some(donor) = &mut manifest.tme_runtime_donor {
            donor.path = absolute(base, &donor.path)?;
            check_hash(&donor.path, &donor.sha256)?;
        }
        if let Some(file) = &mut manifest.board_file {
            file.path = absolute(base, &file.path)?;
            check_hash(&file.path, &file.sha256)?;
        }
        for recipe in &mut manifest.patches {
            *recipe = absolute(base, recipe)?;
        }
        Ok(manifest)
    }
}

pub fn copy_input(input: &Input, destination: &Path) -> Result<Input> {
    let mut source = File::open(&input.path)?;
    let mut target = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(destination)?;
    std::io::copy(&mut source, &mut target)?;
    target.sync_all()?;
    check_hash(destination, &input.sha256)?;
    Ok(Input {
        path: PathBuf::from(
            destination
                .file_name()
                .context("The input has no filename")?,
        ),
        sha256: input.sha256.clone(),
    })
}

/// Copies the ROM image out of its dump. The copy starts at offset zero.
pub fn copy_rom(rom: &Rom, size: u64, destination: &Path) -> Result<Input> {
    let bytes = rom.read(size)?;
    let mut target = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(destination)?;
    target.write_all(&bytes)?;
    target.sync_all()?;
    check_hash(destination, &rom.sha256)?;
    Ok(Input {
        path: PathBuf::from(
            destination
                .file_name()
                .context("The input has no filename")?,
        ),
        sha256: rom.sha256.clone(),
    })
}

pub fn apply_patch(recipe: &Path, root: &Path) -> Result<AppliedPatch> {
    let text = fs::read_to_string(recipe)?;
    let patch: Patch = toml::from_str(&text)?;
    ensure!(patch.schema == 1, "Unsupported patch schema");
    ensure!(
        !patch.name.trim().is_empty() && !patch.reason.trim().is_empty(),
        "The patch requires a name and reason"
    );
    let filename = match patch.target.as_str() {
        "pbl" => "pbl.bin",
        "tme" => "tme.bin",
        _ => bail!("Unsupported patch target {}", patch.target),
    };
    let path = root.join(filename);
    check_hash(&path, &patch.input_sha256)?;
    let hex = patch.replacement_hex.as_bytes();
    ensure!(
        !hex.is_empty() && hex.len() <= 2 * 1024 * 1024 && hex.len() % 2 == 0,
        "Invalid patch replacement length"
    );
    let bytes: Vec<u8> = hex
        .chunks_exact(2)
        .map(|pair| {
            let pair = std::str::from_utf8(pair)?;
            u8::from_str_radix(pair, 16).context("Invalid patch hexadecimal byte")
        })
        .collect::<Result<_>>()?;
    let mut file = OpenOptions::new().read(true).write(true).open(&path)?;
    let end = patch
        .offset
        .checked_add(bytes.len() as u64)
        .context("The patch range overflows")?;
    ensure!(
        end <= file.metadata()?.len(),
        "The patch exceeds the input range"
    );
    file.seek(SeekFrom::Start(patch.offset))?;
    let mut original = vec![0; bytes.len()];
    file.read_exact(&mut original)?;
    let span_hash = format!("{:x}", Sha256::digest(&original));
    ensure!(
        span_hash.eq_ignore_ascii_case(&patch.expected_span_sha256),
        "The patch preimage hash does not match"
    );
    file.seek(SeekFrom::Start(patch.offset))?;
    file.write_all(&bytes)?;
    file.sync_all()?;
    Ok(AppliedPatch {
        name: patch.name,
        target: patch.target,
        reason: patch.reason,
        recipe_sha256: format!("{:x}", Sha256::digest(text.as_bytes())),
        result_sha256: hash(&path)?,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn calibration_page_size_and_hash_are_checked() {
        let dir = tempfile::tempdir().unwrap();
        fs::write(dir.path().join("pbl.bin"), vec![0; 0x3c000]).unwrap();
        fs::write(dir.path().join("tme.bin"), vec![0; 0x20000]).unwrap();
        fs::write(dir.path().join("package.zip"), b"package").unwrap();
        let manifest = dir.path().join("firmware.toml");
        let write = |page: &[u8], digest: &str| {
            fs::write(dir.path().join("calibration.bin"), page).unwrap();
            fs::write(
                &manifest,
                format!(
                    "schema = 1\nplatform = \"sm8850\"\nboard = \"oplus-infiniti\"\nauth = \"retail\"\n\
                     [pbl]\npath = \"pbl.bin\"\nsha256 = \"{}\"\n\
                     [tme]\npath = \"tme.bin\"\nsha256 = \"{}\"\n\
                     [package]\npath = \"package.zip\"\nsha256 = \"{}\"\n\
                     [calibration_fuses]\npath = \"calibration.bin\"\nsha256 = \"{}\"\n",
                    hash(&dir.path().join("pbl.bin")).unwrap(),
                    hash(&dir.path().join("tme.bin")).unwrap(),
                    hash(&dir.path().join("package.zip")).unwrap(),
                    digest,
                ),
            )
            .unwrap();
        };
        let page = vec![0x5a; CALIBRATION_PAGE_SIZE as usize];
        let digest = format!("{:x}", Sha256::digest(&page));
        let truncated = &page[..page.len() - 1];
        write(truncated, &format!("{:x}", Sha256::digest(truncated)));
        let error = Manifest::load(&manifest).unwrap_err().to_string();
        assert!(error.contains("4096 bytes"), "{error}");
        write(&page, &format!("{:x}", Sha256::digest(b"other")));
        let error = Manifest::load(&manifest).unwrap_err().to_string();
        assert!(error.contains("SHA-256 mismatch"), "{error}");
        write(&page, &digest);
        let loaded = Manifest::load(&manifest).unwrap();
        let page = loaded.calibration_fuses.unwrap();
        assert_eq!(page.path, dir.path().canonicalize().unwrap().join("calibration.bin"));
        assert_eq!(page.sha256, digest);
    }

    #[test]
    fn hash_mismatch_rejects_input() {
        let dir = tempfile::tempdir().unwrap();
        let input = dir.path().join("input");
        fs::write(&input, b"actual").unwrap();
        assert!(check_hash(&input, &format!("{:x}", Sha256::digest(b"expected"))).is_err());
    }

    #[test]
    fn wrong_patch_preimage_preserves_input() {
        let dir = tempfile::tempdir().unwrap();
        let input = dir.path().join("pbl.bin");
        fs::write(&input, [1, 2, 3, 4]).unwrap();
        let recipe = dir.path().join("patch.toml");
        fs::write(&recipe, format!("schema = 1\nname = 'test'\ntarget = 'pbl'\ninput_sha256 = '{}'\noffset = 1\nexpected_span_sha256 = '{}'\nreplacement_hex = 'ff'\nreason = 'Exercise the preimage boundary.'\n", hash(&input).unwrap(), "0".repeat(64))).unwrap();
        assert!(apply_patch(&recipe, dir.path()).is_err());
        assert_eq!(fs::read(input).unwrap(), [1, 2, 3, 4]);
    }

    fn rom(path: PathBuf, sha256: String, offset: u64) -> Rom {
        Rom {
            path,
            sha256,
            offset,
            cpucp_offset: None,
        }
    }

    #[test]
    fn rom_hash_covers_the_image_at_its_offset() {
        let dir = tempfile::tempdir().unwrap();
        let image: Vec<u8> = (0..64_u8).collect();
        let mut dump = vec![0xee; 0x30];
        dump.extend_from_slice(&image);
        dump.extend_from_slice(&[0xdd; 0x20]);
        let path = dir.path().join("dump.bin");
        fs::write(&path, &dump).unwrap();
        let digest = format!("{:x}", Sha256::digest(&image));
        let read = rom(path.clone(), digest.clone(), 0x30).read(64).unwrap();
        assert_eq!(read, image);
        // The same hash at the wrong offset names different bytes.
        let error = rom(path.clone(), digest.clone(), 0x31).read(64).unwrap_err();
        assert!(error.to_string().contains("SHA-256 mismatch"), "{error}");
        // A dump that ends inside the image range is rejected before hashing.
        let error = rom(path, digest, 0x60).read(64).unwrap_err();
        assert!(error.to_string().contains("must contain at least"), "{error}");
    }

    #[test]
    fn copy_rom_writes_the_image_from_offset_zero() {
        let dir = tempfile::tempdir().unwrap();
        let image = vec![0x5a_u8; 16];
        let mut dump = vec![0_u8; 8];
        dump.extend_from_slice(&image);
        let path = dir.path().join("dump.bin");
        fs::write(&path, &dump).unwrap();
        let source = rom(path, format!("{:x}", Sha256::digest(&image)), 8);
        let copied = copy_rom(&source, 16, &dir.path().join("pbl.bin")).unwrap();
        assert_eq!(fs::read(dir.path().join("pbl.bin")).unwrap(), image);
        assert_eq!(copied.path, PathBuf::from("pbl.bin"));
        assert_eq!(copied.sha256, source.sha256);
    }

    #[test]
    fn cpucp_offset_must_lie_inside_the_pbl_image() {
        let dir = tempfile::tempdir().unwrap();
        fs::write(dir.path().join("pbl.bin"), vec![0; PBL_SIZE as usize]).unwrap();
        fs::write(dir.path().join("tme.bin"), vec![0; TME_SIZE as usize]).unwrap();
        fs::write(dir.path().join("package.zip"), b"package").unwrap();
        let manifest = dir.path().join("firmware.toml");
        let write = |pbl_extra: &str, tme_extra: &str| {
            fs::write(
                &manifest,
                format!(
                    "schema = 1\nplatform = \"sm8850\"\nboard = \"oplus-infiniti\"\nauth = \"retail\"\n\
                     [pbl]\npath = \"pbl.bin\"\nsha256 = \"{}\"\n{pbl_extra}\
                     [tme]\npath = \"tme.bin\"\nsha256 = \"{}\"\n{tme_extra}\
                     [package]\npath = \"package.zip\"\nsha256 = \"{}\"\n",
                    hash(&dir.path().join("pbl.bin")).unwrap(),
                    hash(&dir.path().join("tme.bin")).unwrap(),
                    hash(&dir.path().join("package.zip")).unwrap(),
                ),
            )
            .unwrap();
        };
        write("cpucp_offset = 0x34000\n", "");
        assert_eq!(Manifest::load(&manifest).unwrap().cpucp_offset(), 0x34000);
        write("", "");
        assert_eq!(Manifest::load(&manifest).unwrap().cpucp_offset(), DEFAULT_CPUCP_OFFSET);
        write("cpucp_offset = 0x34001\n", "");
        let error = Manifest::load(&manifest).unwrap_err().to_string();
        assert!(error.contains("inside the PBL image"), "{error}");
        write("", "cpucp_offset = 0\n");
        let error = Manifest::load(&manifest).unwrap_err().to_string();
        assert!(error.contains("[pbl] only"), "{error}");
    }

    fn target(platform: &str, board: &str, compatibility: bool) -> Manifest {
        let digest = "0".repeat(64);
        toml::from_str(&format!(
            "schema = 1\nplatform = \"{platform}\"\nboard = \"{board}\"\nauth = \"retail\"\n\
             rom_compatibility = {compatibility}\n\
             [pbl]\npath = \"p\"\nsha256 = \"{digest}\"\n\
             [tme]\npath = \"t\"\nsha256 = \"{digest}\"\n\
             [package]\npath = \"k\"\nsha256 = \"{digest}\"\n"
        ))
        .unwrap()
    }

    #[test]
    fn any_board_label_is_accepted_but_the_platform_stays_closed() {
        assert!(target("sm8850", "other-vendor-2", false).check_target().is_ok());
        assert!(target("sm8845", "other-vendor-2", true).check_target().is_ok());
        for (platform, board, compatibility) in [
            ("sm8750", "oplus-infiniti", false),
            ("sm8850", "Upper", false),
            ("sm8850", "-lead", false),
            ("sm8850", "", false),
            ("sm8850", "board", true),
            ("sm8845", "board", false),
        ] {
            assert!(
                target(platform, board, compatibility).check_target().is_err(),
                "{platform} {board:?} {compatibility}"
            );
        }
    }
}
