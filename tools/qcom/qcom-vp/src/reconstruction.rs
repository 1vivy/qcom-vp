//! Reconstruct the common pinned TME runtime from two private, hash-verified inputs.
//!
//! The recipe contains coordinates, not vendor instructions or hashes of short spans.
//! The whole-image SHA-256 of each input already pins every byte of that input.
//! The original first 0x16000 bytes remain unchanged. Runtime code comes from the
//! donor, except for the authored adapter and the documented contract repairs.
//! This operation does not authenticate images or alter the retail trust policy.
//! Callers must retain the original inputs and this provenance with the output.
//!
//! Assemble `firmware/qcom/sm88xx/tme-runtime/tme-runtime-adapter.S` for RV32IMAC with relaxation
//! disabled. Link `.text` at 0x234c00, `runtime_entry` at 0x234a00, and
//! `runtime_oem_resume` at 0x2309a8. Supply the extracted `.text` bytes, not ELF.
//! Both supported boards use the same pinned inputs, adapter, recipe, and API map.
//! This does not imply complete ABI compatibility. Unmapped API slots remain unmapped.

use anyhow::{ensure, Context, Result};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

const BASE: u32 = 0x200000;
const PREFIX: usize = 0x16000;
const ADAPTER_OFFSET: usize = 0x34c00;
const RECIPE: &str =
    include_str!("../../../../firmware/qcom/sm88xx/tme-runtime/tme-runtime-relocations.json");
const API_MAP: &str =
    include_str!("../../../../firmware/qcom/sm88xx/tme-runtime/tme-runtime-api.map");

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Recipe {
    schema: u32,
    original_sha256: String,
    donor_sha256: String,
    output_sha256: String,
    original_size: usize,
    donor_size: usize,
    adapter_sha256: String,
    adapter_size: usize,
    api_map_sha256: String,
    ram_delta: u32,
    trampoline_end: u32,
    recorded_patch_count: usize,
    reference_manifest_sha256: String,
    legacy_api_evidence_sha256: String,
    early_api_evidence_sha256: String,
    relocations: Vec<Relocation>,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Relocation {
    offset: usize,
    kind: RelocationKind,
}

#[derive(Deserialize)]
#[serde(rename_all = "snake_case")]
enum RelocationKind {
    Instruction,
    Data,
}

/// Complete reconstruction result. The caller chooses output paths.
pub struct ReconstructedTme {
    /// Reconstructed bytes, including the unchanged original prefix.
    pub image: Vec<u8>,
    /// Sorted hexadecimal legacy-slot mapping. `legacy` retains the old slot.
    pub api_map: &'static str,
    /// Input, recipe, adapter, mapping, and output identity.
    pub provenance: ReconstructionProvenance,
}

/// Hash identities and coordinates required to reproduce this private patch.
///
/// The recipe hash identifies every relocation and required preimage span.
/// The evidence hashes identify the private analysis inputs, not payload copies.
#[derive(Debug, Deserialize, Serialize, PartialEq, Eq)]
pub struct ReconstructionProvenance {
    pub original_sha256: String,
    pub donor_sha256: String,
    pub output_sha256: String,
    pub adapter_sha256: String,
    pub recipe_sha256: String,
    pub api_map_sha256: String,
    pub reference_manifest_sha256: String,
    pub legacy_api_evidence_sha256: String,
    pub early_api_evidence_sha256: String,
    pub preserved_prefix_length: usize,
    pub runtime_start: u32,
    pub runtime_end: u32,
    pub ram_delta: u32,
    pub trampoline_end: u32,
    pub relocation_count: usize,
    /// Matches the reference's accounting: relocations plus three repairs.
    /// The entry trampoline and OEM hook belong to the adapter record.
    pub recorded_patch_count: usize,
}

fn digest(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}

fn check_digest(bytes: &[u8], expected: &str, name: &str) -> Result<()> {
    ensure!(digest(bytes) == expected, "TME {name} SHA-256 mismatch");
    Ok(())
}

fn span(bytes: &[u8], offset: usize, length: usize) -> Result<&[u8]> {
    let end = offset.checked_add(length).context("TME span overflows")?;
    bytes.get(offset..end).context("TME span exceeds input")
}

fn word(bytes: &[u8], offset: usize) -> Result<u32> {
    let bytes: [u8; 4] = span(bytes, offset, 4)?.try_into()?;
    Ok(u32::from_le_bytes(bytes))
}

fn put_word(bytes: &mut [u8], offset: usize, value: u32) -> Result<()> {
    let end = offset.checked_add(4).context("TME word offset overflows")?;
    bytes
        .get_mut(offset..end)
        .context("TME word exceeds output")?
        .copy_from_slice(&value.to_le_bytes());
    Ok(())
}

fn jump(source: u32, destination: u32) -> Result<u32> {
    let offset = i64::from(destination) - i64::from(source);
    ensure!(
        offset % 2 == 0 && (-(1 << 20)..(1 << 20)).contains(&offset),
        "TME jump destination is out of range"
    );
    let bits = (offset as u32) & 0x1fffff;
    Ok(((bits >> 20) & 1) << 31
        | ((bits >> 1) & 0x3ff) << 21
        | ((bits >> 11) & 1) << 20
        | ((bits >> 12) & 0xff) << 12
        | 0x6f)
}

/// Reconstruct the exact pinned image and return its mapping and provenance.
///
/// All three byte inputs are hash-checked before any reconstruction. The recipe
/// independently checks every modified private span. No filesystem paths or
/// disassembler are required. The final hash must match the reference image.
///
/// # Errors
/// Returns an error for unsupported inputs, a different adapter, invalid recipe
/// coordinates, a preimage mismatch, or a different reconstructed output.
pub fn reconstruct(original: &[u8], donor: &[u8], adapter: &[u8]) -> Result<ReconstructedTme> {
    let recipe: Recipe = serde_json::from_str(RECIPE)?;
    ensure!(recipe.schema == 1, "Unsupported TME reconstruction schema");
    ensure!(
        original.len() == recipe.original_size && donor.len() == recipe.donor_size,
        "Unsupported TME input lengths"
    );
    check_digest(original, &recipe.original_sha256, "original")?;
    check_digest(donor, &recipe.donor_sha256, "donor")?;
    ensure!(
        adapter.len() == recipe.adapter_size,
        "Unsupported TME adapter length"
    );
    check_digest(adapter, &recipe.adapter_sha256, "adapter")?;
    check_digest(API_MAP.as_bytes(), &recipe.api_map_sha256, "API map")?;
    ensure!(
        recipe.ram_delta == 0x70000
            && recipe.trampoline_end == 0x234a08
            && recipe.recorded_patch_count == 2024
            && recipe.relocations.len() == 2021,
        "Unsupported TME reconstruction layout"
    );
    ensure!(
        span(donor, 0x34944, 0x38000 - 0x34944)?
            .iter()
            .all(|&b| b == 0xff)
            && donor
                .get(0x38000..)
                .context("Missing TME zero tail")?
                .iter()
                .all(|&b| b == 0),
        "TME adapter storage is not reserved"
    );
    let mut image = donor.to_vec();
    image[..PREFIX].copy_from_slice(span(original, 0, PREFIX)?);
    let mut previous = None;
    for relocation in &recipe.relocations {
        let offset = relocation.offset;
        ensure!(
            previous.is_none_or(|last| offset >= last + 4),
            "TME relocations overlap or are unordered"
        );
        previous = Some(offset);
        let before = word(donor, offset)?;
        match relocation.kind {
            RelocationKind::Instruction => {
                ensure!(
                    (PREFIX..0x31b48).contains(&offset) && offset % 2 == 0,
                    "TME instruction relocation exceeds runtime code"
                );
                let opcode = before & 0x7f;
                ensure!(
                    (opcode == 0x37 || opcode == 0x17) && (before >> 7) & 31 != 0,
                    "TME relocation is not LUI or AUIPC"
                );
                let value = (before & 0xfffff000).wrapping_add(if opcode == 0x17 {
                    BASE + offset as u32
                } else {
                    0
                });
                ensure!(
                    (0x404000..0x405000).contains(&value) || (0x410000..0x420000).contains(&value),
                    "TME instruction relocation has an unsupported address"
                );
            }
            RelocationKind::Data => {
                ensure!(
                    offset % 4 == 0
                        && ((0x32ad4..0x32ae4).contains(&offset)
                            || (0x33c84..0x34944).contains(&offset))
                        && (0x410000..0x420000).contains(&before),
                    "TME data relocation has an unsupported coordinate or address"
                );
            }
        }
        put_word(&mut image, offset, before.wrapping_add(recipe.ram_delta))?;
    }
    // Replace the relocated entry with the adapter. Its return trampoline uses
    // LUI because it runs at a different PC than the donor's original AUIPC.
    let entry = word(donor, PREFIX)?;
    ensure!(entry & 0x7f == 0x17, "TME entry is not AUIPC");
    let entry_value = (entry & 0xfffff000)
        .wrapping_add(BASE + PREFIX as u32)
        .wrapping_add(recipe.ram_delta);
    ensure!(
        entry_value & 0xfff == 0,
        "TME entry address is not page aligned"
    );
    put_word(&mut image, 0x34a00, entry_value | (entry & 0xf80) | 0x37)?;
    put_word(&mut image, 0x34a04, jump(0x234a04, 0x216004)?)?;
    ensure!(
        ADAPTER_OFFSET + adapter.len() <= 0x38000,
        "TME adapter exceeds reserved storage"
    );
    image[ADAPTER_OFFSET..ADAPTER_OFFSET + adapter.len()].copy_from_slice(adapter);
    put_word(&mut image, PREFIX, jump(0x216000, 0x234c00)?)?;
    put_word(&mut image, 0x309a4, jump(0x2309a4, 0x235000)?)?;
    // Preserve the original fuse-row lower bound, not the donor's wider bound.
    let old_bound = word(original, 0x19626)?;
    let new_bound = word(donor, 0x1961a)?;
    ensure!(
        old_bound >> 20 == 0x227
            && new_bound >> 20 == 0x237
            && old_bound & 0xfffff == new_bound & 0xfffff,
        "TME fuse bounds differ unexpectedly"
    );
    put_word(&mut image, 0x1961a, old_bound)?;
    // Publish pending state and results before the response identifier.
    let record = span(donor, 0x2d6d6, 8)?;
    image[0x2d6d6..0x2d6dc].copy_from_slice(&record[2..]);
    image[0x2d6dc..0x2d6de].copy_from_slice(&record[..2]);
    ensure!(
        image[..PREFIX] == original[..PREFIX],
        "TME original prefix changed"
    );
    check_digest(&image, &recipe.output_sha256, "reconstructed output")?;
    Ok(ReconstructedTme {
        image,
        api_map: API_MAP,
        provenance: ReconstructionProvenance {
            original_sha256: recipe.original_sha256,
            donor_sha256: recipe.donor_sha256,
            output_sha256: recipe.output_sha256,
            adapter_sha256: recipe.adapter_sha256,
            recipe_sha256: digest(RECIPE.as_bytes()),
            api_map_sha256: recipe.api_map_sha256,
            reference_manifest_sha256: recipe.reference_manifest_sha256,
            legacy_api_evidence_sha256: recipe.legacy_api_evidence_sha256,
            early_api_evidence_sha256: recipe.early_api_evidence_sha256,
            preserved_prefix_length: PREFIX,
            runtime_start: 0x216000,
            runtime_end: 0x234944,
            ram_delta: recipe.ram_delta,
            trampoline_end: recipe.trampoline_end,
            relocation_count: recipe.relocations.len(),
            recorded_patch_count: recipe.recorded_patch_count,
        },
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reconstruction_rejects_wrong_input_lengths() {
        assert!(reconstruct(&[], &[], &[]).is_err());
    }

    #[test]
    fn reconstruction_rejects_unpinned_original() {
        let recipe: Recipe = serde_json::from_str(RECIPE).unwrap();
        assert!(reconstruct(
            &vec![0; recipe.original_size],
            &vec![0; recipe.donor_size],
            &[],
        )
        .is_err());
    }
}
