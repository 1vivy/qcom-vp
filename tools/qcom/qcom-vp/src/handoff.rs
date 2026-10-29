use anyhow::{ensure, Context, Result};
use serde::{Deserialize, Serialize};
use std::fs::{self, File, OpenOptions};
use std::io::{Read, Write};
use std::path::Path;

#[derive(Clone, Serialize, Deserialize)]
pub struct KernelIdentity {
    pub member: String,
    pub sha256: String,
    pub bytes: u64,
}

pub fn extract_reference(package: &Path, member: &str, out: &Path) -> Result<KernelIdentity> {
    let mut archive = zip::ZipArchive::new(File::open(package)?)?;
    let mut boot = archive.by_name(member)?;
    let mut header = [0_u8; 4096];
    boot.read_exact(&mut header)?;
    ensure!(
        &header[..8] == b"ANDROID!",
        "The selected boot image has no Android header"
    );
    let version = u32::from_le_bytes(header[40..44].try_into()?);
    ensure!(
        version == 4,
        "Unsupported Android boot header version {version}"
    );
    let bytes = u32::from_le_bytes(header[8..12].try_into()?) as u64;
    ensure!(
        bytes >= 64 && bytes <= boot.size().saturating_sub(4096),
        "The kernel exceeds the boot image range"
    );
    let mut image_header = [0_u8; 64];
    boot.read_exact(&mut image_header)?;
    ensure!(
        &image_header[56..60] == b"ARMd",
        "The kernel is not an uncompressed AArch64 Image"
    );
    let reference = out.join("kernel-reference.bin");
    let mut file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&reference)?;
    file.write_all(&image_header)?;
    let copied = std::io::copy(&mut boot.take(bytes - 64), &mut file)?;
    ensure!(copied == bytes - 64, "The kernel payload is truncated");
    file.sync_all()?;
    Ok(KernelIdentity {
        member: member.to_owned(),
        sha256: crate::firmware::hash(&reference)?,
        bytes,
    })
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub(crate) struct Evidence {
    pub schema: u32,
    pub uefi_entry_pc: u64,
    pub abl_entry_pc: u64,
    pub boot_services_exit_observed: bool,
    pub abl_transfer_pc: u64,
    pub kernel_entry_pc: u64,
    pub loaded_kernel_address: u64,
    pub fdt_address: u64,
    pub executed_instruction_pc: u64,
    pub instruction_completed: bool,
    pub entry_registers: [u64; 4],
    pub exception_level: u8,
    pub non_secure: bool,
    pub sctlr: u64,
    pub daif: u64,
    pub kernel_member: String,
}

pub fn verify(out: &Path, kernel: &KernelIdentity) -> Result<()> {
    let file = crate::local_file(out, Path::new("kernel-entry.json"))
        .context("The platform did not record a kernel handoff")?;
    let evidence: Evidence = serde_json::from_reader(File::open(file)?)?;
    ensure!(evidence.schema == 1, "Unsupported kernel handoff schema");
    ensure!(
        evidence.uefi_entry_pc != 0 && evidence.abl_entry_pc != 0 && evidence.abl_transfer_pc != 0,
        "The firmware path evidence is incomplete"
    );
    ensure!(
        evidence.boot_services_exit_observed,
        "ABL did not complete its boot-services transition"
    );
    ensure!(
        evidence.instruction_completed
            && evidence.kernel_entry_pc != 0
            && evidence.executed_instruction_pc == evidence.kernel_entry_pc,
        "The first kernel instruction did not execute"
    );
    ensure!(
        evidence.kernel_entry_pc % 4 == 0,
        "The kernel entry is not instruction-aligned"
    );
    ensure!(
        evidence.entry_registers[0] != 0
            && evidence.entry_registers[0] % 8 == 0
            && evidence.entry_registers[1..] == [0, 0, 0],
        "The kernel entry registers violate the AArch64 boot contract"
    );
    ensure!(
        evidence.loaded_kernel_address == evidence.kernel_entry_pc
            && evidence.fdt_address == evidence.entry_registers[0],
        "The guest snapshots do not match the entry addresses"
    );
    ensure!(
        evidence.non_secure && matches!(evidence.exception_level, 1 | 2),
        "The kernel entry exception level violates the AArch64 boot contract"
    );
    ensure!(
        evidence.sctlr & ((1 << 25) | 5) == 0 && evidence.daif & 0x3c0 == 0x3c0,
        "The kernel entry MMU, byte order, or interrupt state is invalid"
    );
    ensure!(
        evidence.kernel_member == kernel.member,
        "The handoff selected a different boot image"
    );
    let loaded = crate::local_file(out, Path::new("loaded-kernel.bin"))?;
    ensure!(
        fs::metadata(&loaded)?.len() == kernel.bytes,
        "The loaded kernel size does not match"
    );
    crate::firmware::check_hash(&loaded, &kernel.sha256)?;
    let fdt = crate::local_file(out, Path::new("entry-fdt.bin"))?;
    ensure!(
        fs::metadata(&fdt)?.len() <= 2 * 1024 * 1024,
        "The entry device tree exceeds the boot contract limit"
    );
    check_fdt(&fs::read(fdt)?)?;
    Ok(())
}

pub(crate) fn check_fdt(bytes: &[u8]) -> Result<()> {
    ensure!(
        bytes.len() >= 40,
        "The entry device tree header is truncated"
    );
    let word = |at: usize| u32::from_be_bytes(bytes[at..at + 4].try_into().unwrap()) as usize;
    ensure!(
        word(0) == 0xd00dfeed && word(4) == bytes.len(),
        "The entry device tree header is invalid"
    );
    ensure!(
        word(20) >= 17 && word(24) <= 17,
        "The entry device tree version is unsupported"
    );
    let structure = word(8);
    let strings = word(12);
    let reservation = word(16);
    let string_end = strings
        .checked_add(word(32))
        .context("The device tree strings overflow")?;
    let structure_end = structure
        .checked_add(word(36))
        .context("The device tree structure overflows")?;
    ensure!(
        structure >= 40
            && structure % 4 == 0
            && structure_end <= bytes.len()
            && strings >= 40
            && string_end <= bytes.len(),
        "The device tree sections exceed the input range"
    );
    ensure!(
        structure_end <= strings || string_end <= structure,
        "The device tree sections overlap"
    );
    ensure!(
        reservation >= 40 && reservation % 8 == 0,
        "The device tree reservation offset is invalid"
    );
    let mut cursor = reservation;
    loop {
        let end = cursor
            .checked_add(16)
            .context("The device tree reservation range overflows")?;
        ensure!(
            end <= bytes.len()
                && (end <= structure || cursor >= structure_end)
                && (end <= strings || cursor >= string_end),
            "The device tree reservation range is invalid"
        );
        if bytes[cursor..end].iter().all(|b| *b == 0) {
            break;
        }
        cursor = end;
    }
    cursor = structure;
    let mut depth = 0usize;
    let mut root = false;
    loop {
        ensure!(
            cursor + 4 <= structure_end,
            "The device tree has no end token"
        );
        let token = word(cursor);
        cursor += 4;
        match token {
            1 => {
                ensure!(
                    depth > 0 || !root,
                    "The device tree contains multiple roots"
                );
                let length = bytes[cursor..structure_end]
                    .iter()
                    .position(|b| *b == 0)
                    .context("The device tree node name is truncated")?;
                if !root {
                    ensure!(length == 0, "The device tree root name is invalid");
                    root = true;
                }
                cursor = (cursor + length + 1 + 3) & !3;
                depth += 1;
            }
            2 => {
                ensure!(depth > 0, "The device tree node depth is invalid");
                depth -= 1;
            }
            3 => {
                ensure!(
                    depth > 0 && cursor + 8 <= structure_end,
                    "The device tree property is invalid"
                );
                let length = word(cursor);
                let name = word(cursor + 4);
                ensure!(
                    name < word(32) && bytes[strings + name..string_end].contains(&0),
                    "The device tree property name is invalid"
                );
                cursor = cursor
                    .checked_add(8)
                    .and_then(|v| v.checked_add(length))
                    .and_then(|v| v.checked_add(3))
                    .context("The device tree property range overflows")?
                    & !3;
            }
            4 => {}
            9 => {
                ensure!(root && depth == 0, "The device tree ends inside a node");
                break;
            }
            _ => anyhow::bail!("The device tree contains an invalid token"),
        }
        ensure!(
            cursor <= structure_end,
            "The device tree structure is truncated"
        );
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn early_exit_cannot_prove_kernel_entry() {
        let dir = tempfile::tempdir().unwrap();
        let identity = KernelIdentity {
            member: "IMAGES/boot.img".into(),
            sha256: "0".repeat(64),
            bytes: 64,
        };
        assert!(verify(dir.path(), &identity).is_err());
    }

    #[test]
    fn breakpoint_before_instruction_is_not_handoff() {
        let dir = tempfile::tempdir().unwrap();
        let mut fdt = vec![0_u8; 72];
        for (offset, value) in [
            (0, 0xd00dfeed_u32),
            (4, 72),
            (8, 56),
            (12, 72),
            (16, 40),
            (20, 17),
            (24, 16),
            (36, 16),
            (56, 1),
            (64, 2),
            (68, 9),
        ] {
            fdt[offset..offset + 4].copy_from_slice(&value.to_be_bytes());
        }
        fs::write(dir.path().join("entry-fdt.bin"), fdt).unwrap();
        let loaded = dir.path().join("loaded-kernel.bin");
        fs::write(&loaded, [0_u8; 64]).unwrap();
        let kernel = KernelIdentity {
            member: "IMAGES/boot.img".into(),
            sha256: crate::firmware::hash(&loaded).unwrap(),
            bytes: 64,
        };
        let mut evidence = serde_json::json!({
            "schema": 1, "uefi_entry_pc": 0xc6800000_u64,
            "abl_entry_pc": 0x9fa00000_u64, "boot_services_exit_observed": true,
            "abl_transfer_pc": 0x9fa00100_u64, "kernel_entry_pc": 0x80080000_u64,
            "loaded_kernel_address": 0x80080000_u64, "fdt_address": 0x90000000_u64,
            "executed_instruction_pc": 0x80080000_u64, "instruction_completed": false,
            "entry_registers": [0x90000000_u64, 0, 0, 0], "exception_level": 2,
            "non_secure": true, "sctlr": 0, "daif": 0x3c0,
            "kernel_member": "IMAGES/boot.img"
        });
        let record = dir.path().join("kernel-entry.json");
        fs::write(&record, serde_json::to_vec(&evidence).unwrap()).unwrap();
        assert!(verify(dir.path(), &kernel).is_err());
        evidence["instruction_completed"] = true.into();
        fs::write(&record, serde_json::to_vec(&evidence).unwrap()).unwrap();
        verify(dir.path(), &kernel).unwrap();
    }

    #[test]
    fn device_tree_rejects_property_outside_node() {
        let mut bytes = vec![0_u8; 68];
        for (offset, value) in [
            (0, 0xd00dfeed_u32),
            (4, 68),
            (8, 56),
            (12, 68),
            (16, 40),
            (20, 17),
            (24, 16),
            (36, 12),
            (56, 3),
            (60, 0),
            (64, 0),
        ] {
            bytes[offset..offset + 4].copy_from_slice(&value.to_be_bytes());
        }
        assert!(check_fdt(&bytes).is_err());
    }
}
