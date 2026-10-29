use sha2::{Digest, Sha256, Sha512};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

fn install_prefix() -> PathBuf {
    match std::env::var_os("QCOM_VP_TEST_PREFIX") {
        Some(prefix) => PathBuf::from(prefix),
        None => PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../../build/install"),
    }
}

fn compile_guest(
    root: &Path,
    fixture: &Path,
    name: &str,
    target: &str,
    address: &str,
    extra: &[&str],
) {
    let elf = root.join(format!("{name}.elf"));
    let output = Command::new("clang")
        .arg(format!("--target={target}"))
        .args(["-fuse-ld=lld", "-nostdlib"])
        .arg(format!("-Wl,-Ttext={address}"))
        .arg(fixture.join(format!("{name}.S")))
        .args(extra)
        .arg("-o")
        .arg(&elf)
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let output = Command::new("llvm-objcopy")
        .args(["-O", "binary"])
        .arg(elf)
        .arg(root.join(format!("{name}.bin")))
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
}

fn run_platform(root: &Path, fixture: &Path) -> std::process::Output {
    let repository = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../..");
    let system = install_prefix()
        .join("bin/qcom-vp-system")
        .canonicalize()
        .unwrap();
    Command::new("timeout")
        .arg("10")
        .arg(system)
        .arg("-l")
        .arg(fixture.join("platform.lua"))
        .env("QCOM_VP_PROBE_ROOT", root)
        .env("QCOM_VP_PLATFORM_DIR", repository.join("platforms"))
        .output()
        .unwrap()
}

fn run_fixture(root: &Path, fixture: &Path, expected_fault: u64) -> std::process::Output {
    let output = run_platform(root, fixture);
    let errors = String::from_utf8_lossy(&output.stderr);
    assert_eq!(output.status.code(), Some(2), "{errors}");
    // Each guest defines the first fault that follows its behavioral checks.
    let mut faults = errors
        .lines()
        .filter(|line| line.starts_with("UNMODELED_ACCESS "));
    let fault = faults
        .next()
        .unwrap_or_else(|| panic!("The guest did not finish: {errors}"));
    assert!(
        fault.contains(&format!("address={expected_fault:#x} ")),
        "{errors}"
    );
    assert!(faults.next().is_none(), "{errors}");
    output
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn foreign_code_writes_invalidate_executed_riscv_code() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/coherence");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[],
    );
    compile_guest(
        directory.path(),
        &fixture,
        "consumer",
        "riscv32-none-elf",
        "0x200000",
        &["-march=rv32im", "-mabi=ilp32"],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn independent_execution_participant_has_writable_coherent_dmi() {
    let directory = tempfile::tempdir().unwrap();
    let fixtures = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures");
    let fixture = fixtures.join("participant-dmi");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[],
    );
    compile_guest(
        directory.path(),
        &fixture,
        "consumer",
        "riscv32-none-elf",
        "0x200000",
        &["-march=rv32im", "-mabi=ilp32"],
    );
    compile_guest(
        directory.path(),
        &fixture,
        "owner",
        "riscv32-none-elf",
        "0x200000",
        &["-march=rv32im", "-mabi=ilp32"],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
    let trace = std::fs::read_to_string(directory.path().join("consumer.log")).unwrap();
    assert!(
        trace.split("IN:").any(|block| {
            block.contains("0x80001000:") && block.contains("0x80001004:")
        }),
        "The participant did not translate a multi-instruction shared-RAM block: {trace}"
    );
    std::fs::write(
        directory.path().join("platform.lua"),
        format!(
            "dofile('{}')\nplatform.shared.execution_participant=''\nplatform.dare.execution_participant=''\n",
            fixture.join("platform.lua").display()
        ),
    )
    .unwrap();
    run_fixture(directory.path(), directory.path(), 0x600d0000);
    let trace = std::fs::read_to_string(directory.path().join("consumer.log")).unwrap();
    assert!(
        !trace.split("IN:").any(|block| {
            block.contains("0x80001000:") && block.contains("0x80001004:")
        }),
        "The transaction-only control unexpectedly received executable DMI: {trace}"
    );
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn runtime_abi_keeps_callback_updates_coherent_after_activation() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/runtime-abi");
    std::fs::write(directory.path().join("slots.map"), "4 4\n8 14\n10 legacy\n").unwrap();
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[],
    );
    run_fixture(directory.path(), &fixture, 0x41680c);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn runtime_activation_revokes_the_previous_requester_domain() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/runtime-abi");
    std::fs::write(directory.path().join("slots.map"), "4 4\n8 14\n10 legacy\n").unwrap();
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &["-DREVOKED_DOMAIN"],
    );
    run_fixture(directory.path(), &fixture, 0x400000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn runtime_adapter_wraps_request_and_response_rings_independently() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/runtime-ring");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "riscv32-none-elf",
        "0x200000",
        &["-march=rv32imac_zicsr", "-mabi=ilp32", "-mno-relax"],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn runtime_policy_queries_distinguish_absence_from_unavailable_state() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/runtime-policy");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "riscv32-none-elf",
        "0x200000",
        &["-march=rv32imac_zicsr", "-mabi=ilp32", "-mno-relax"],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn watchdog_petting_disable_and_deadlines_control_interrupts() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/watchdog");
    for packed in [false, true] {
        let directory = tempfile::tempdir().unwrap();
        std::fs::write(
            directory.path().join("policy.lua"),
            format!("return {packed}\n"),
        )
        .unwrap();
        let selection = format!("-DPACKED_STATUS={}", u8::from(packed));
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "riscv32-none-elf",
            "0x200000",
            &["-march=rv32imac_zicsr", "-mabi=ilp32", &selection],
        );
        run_fixture(directory.path(), &fixture, 0x600d0000);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn apss_watchdog_requires_secure_permission_changes() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/watchdog-security");
    for (options, expected_fault) in [
        (&[][..], 0x600d0000),
        (&["-DDENY_CONTROL"][..], 0x17600008),
        (&["-DDENY_MODE"][..], 0x17600000),
        (&["-DREVOKE_CONTROL"][..], 0x17600008),
        (&["-DREAD_CHICKEN"][..], 0x17600024),
        (&["-DINVALID_CHICKEN"][..], 0x17600024),
        (&["-DDENY_CONTROL", "-DDENY_CHICKEN"][..], 0x17600024),
    ] {
        let directory = tempfile::tempdir().unwrap();
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            options,
        );
        run_fixture(directory.path(), &fixture, expected_fault);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn apss_pdc_configuration_cannot_create_capture_events() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/pdc");
    for (case, terminal) in [
        (0, 0x600d0000),
        (1, 0x0b201204),
        (2, 0x0b201004),
        (3, 0x0b206000),
        (4, 0x0b204fd4),
    ] {
        let directory = tempfile::tempdir().unwrap();
        let selection = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[&selection],
        );
        run_fixture(directory.path(), &fixture, terminal);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn rpmh_completes_active_batches_and_isolates_requesters() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/rpmh-rsc");
    for (case, terminal) in [
        (0, 0x600d0000),
        (1, 0x600d0000),
        (2, 0x18901264),
        (3, 0x18900d24),
        (4, 0x02211504),
        (5, 0x18900d24),
    ] {
        let directory = tempfile::tempdir().unwrap();
        let selection = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[&selection],
        );
        run_fixture(directory.path(), &fixture, terminal);
    }
}

fn run_native_consumer(fixture: &str, marker: &str) {
    run_native_consumer_modes(fixture, &[(&[], marker)]);
}

fn run_native_consumer_modes(fixture: &str, modes: &[(&[&str], &str)]) {
    let repository = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../..");
    let install = install_prefix().canonicalize().unwrap();
    let directory = tempfile::tempdir().unwrap();
    let executable = directory.path().join("consumer");
    let flags = Command::new("pkg-config")
        .args(["--cflags", "glib-2.0"])
        .output()
        .unwrap();
    assert!(flags.status.success(), "Cannot resolve GLib compiler flags");
    let output = Command::new("c++")
        .args(["-std=c++17", "-pthread"])
        .args(std::str::from_utf8(&flags.stdout).unwrap().split_whitespace())
        .arg(format!("-I{}", repository.join("models").display()))
        .arg(format!("-I{}", install.join("include").display()))
        .arg(format!("-I{}", install.join("include/libqemu").display()))
        .arg(format!(
            "-I{}",
            install.join("systemc-components/common/include").display()
        ))
        .arg(format!(
            "-I{}",
            install.join("qemu-components/common/include").display()
        ))
        .arg(repository.join(format!("tools/qcom/qcom-vp/tests/fixtures/{fixture}/consumer.cpp")))
        .arg(format!("-L{}", install.join("lib").display()))
        .arg(format!("-Wl,-rpath,{}", install.join("lib").display()))
        .args(["-lcci-config", "-lsystemc", "-lcrypto", "-o"])
        .arg(&executable)
        .output()
        .unwrap();
    assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
    for (args, marker) in modes {
        let output = Command::new("timeout")
            .arg("10")
            .arg(&executable)
            .args(*args)
            .output()
            .unwrap();
        assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
        assert!(String::from_utf8_lossy(&output.stdout).contains(marker));
    }
}

#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn display_rsc_votes_and_timestamps_require_hardware_events() {
    run_native_consumer("display-rsc", "DISPLAY_RSC_CAUSAL_COMPLETION_PASS");
}

#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn camera_rscc_retains_the_sequence_without_a_start_request() {
    run_native_consumer("camera-rscc", "CAMERA_RSCC_SEQUENCE_UPLOAD_PASS");
}

#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn pcie_rscc_retains_the_sequence_and_hidden_tcs_data() {
    run_native_consumer("pcie-rscc", "PCIE_RSCC_CONFIGURATION_PASS");
}

#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn bcm_status_tracks_staged_votes_until_owner_commit() {
    run_native_consumer("bcm-status", "BCM_STAGED_VOTE_COMMIT_PASS");
}

#[test]
#[ignore = "Requires make build, a C++17 compiler, and OpenSSL"]
fn aop_resources_follow_discovered_rails_and_reject_failed_sources() {
    run_native_consumer("aop-resources", "AOP_DISCOVERED_VRM_PUBLICATION_PASS");
}

#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn aop_qmp_negotiates_the_descriptor_and_rejects_unknown_messages() {
    run_native_consumer("aop-qmp", "AOP_QMP_HANDSHAKE_PASS");
}

#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn dsi_interrupt_configuration_cannot_create_completion() {
    run_native_consumer("dsi-interrupts", "DSI_INTERRUPTS_NO_SYNTHETIC_EVENT_PASS");
}

#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn ipa_secure_configuration_retains_words_and_rejects_unknown_offsets() {
    run_native_consumer("ipa", "IPA_SECURE_CONFIGURATION_PASS");
}

#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn cpucp_acehr_preserves_configuration_without_false_completion() {
    run_native_consumer("cpucp-acehr", "CPUCP_ACEHR_REQUEST_BOUNDARY_PASS");
}

#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn cpuss_llm_retains_configuration_and_preserves_virtual_samples() {
    run_native_consumer("cpucp-llm", "CPUSS_LLM_CONFIGURATION_PASS");
}

#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn pdp_power_controls_preserve_configuration_and_reject_holes() {
    run_native_consumer("pdp-power-controls", "PDP_POWER_CONTROLS_BOUNDARY_PASS");
}

#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn pdp_cprh_shares_configuration_without_fabricating_status() {
    run_native_consumer("pdp-cprh", "PDP_CPRH_CONFIGURATION_PASS");
}

#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn cprh_target_preserves_table_boundaries_and_rejects_unknown_execution() {
    run_native_consumer("cprh-target", "CPRH_TARGET_BOUNDARY_PASS");
}

#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn cprf_retains_voltage_limits_without_creating_a_voltage() {
    run_native_consumer("cprf", "CPRF_CONFIGURATION_PASS");
}

#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn clock_gating_overrides_retain_fields_and_reject_reserved_bits() {
    run_native_consumer("clock-gating-overrides", "CLOCK_GATING_OVERRIDES_PASS");
}

#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn core_amu_counts_enabled_clocks_and_rejects_unsupplied_events() {
    run_native_consumer("core-amu", "CORE_AMU_COUNTING_PASS");
}

#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn ncc_mpam_msc_selects_partitions_and_rejects_counters() {
    run_native_consumer("ncc-mpam-msc", "NCC_MPAM_MSC_PARTITION_PASS");
}

#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn zonda_pll_hardware_ownership_preserves_effective_frequency() {
    run_native_consumer("ncc-pll", "ZONDA_PLL_HARDWARE_OWNERSHIP_PASS");
}

#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn observation_clock_freezes_real_time_until_the_debugger_resumes() {
    run_native_consumer("observation-clock", "OBSERVATION_CLOCK_PAUSE_RESUME_PASS");
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn qsee_clock_and_power_status_follow_requests() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/qsee-clock");
    for oem in ["sm8850", "sm8845"] {
        let directory = tempfile::tempdir().unwrap();
        std::fs::write(directory.path().join("oem.txt"), format!("{oem}\n")).unwrap();
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[],
        );
        run_fixture(directory.path(), &fixture, 0x600d0000);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn riscv_io_fetch_preserves_compressed_page_boundary() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/riscv-io-fetch");
    let directory = tempfile::tempdir().unwrap();
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "riscv32-none-elf",
        "0xa4100000",
        &["-march=rv32imac_zicsr", "-mabi=ilp32"],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn dcp_reset_gates_cache_operations_and_preserves_pmp() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/dcp-core");
    for case in 0..3 {
        let directory = tempfile::tempdir().unwrap();
        std::fs::write(directory.path().join("case.txt"), format!("{case}\n")).unwrap();
        compile_guest(
            directory.path(),
            &fixture,
            "controller",
            "aarch64-none-elf",
            "0x300000",
            &[],
        );
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "riscv32-none-elf",
            "0xa4100000",
            &[
                "-march=rv32imac_zicsr",
                "-mabi=ilp32",
                &format!("-DCASE={case}"),
            ],
        );
        run_fixture(directory.path(), &fixture, 0x600d0000);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn sequencer_completion_irq_preserves_masked_status() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/sequencer-irq");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "riscv32-none-elf",
        "0x200000",
        &["-march=rv32imac_zicsr", "-mabi=ilp32"],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, llvm-objcopy, and a C++17 compiler"]
fn clic_delivers_id64_and_preserves_masks_priority_and_level_sources() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/clic");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "riscv32-none-elf",
        "0x200000",
        &["-march=rv32im_zicsr", "-mabi=ilp32"],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
    // The peer outputs are observed directly. No peer firmware is involved.
    run_native_consumer("clic", "CLIC_PEER_NOTIFICATION_PASS");
}

#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn qce_bam_hashes_real_buffers_and_preserves_context_and_dma_security() {
    run_native_consumer("qce-bam", "QCE_BAM_HASH_PASS");
}

#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn explicit_gpce_roots_change_and_reproduce_cmac_and_reject_invalid_values() {
    run_native_consumer_modes(
        "qce-bam",
        &[
            (&["--root-profile"], "QCE_ROOT_PROFILE_PASS"),
            (&["--invalid-length"], "QCE_ROOT_INVALID_PASS"),
            (&["--invalid-hex"], "QCE_ROOT_INVALID_PASS"),
        ],
    );
}

#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn smmu_streams_preserve_secure_partition_and_physical_attributes() {
    run_native_consumer("smmu", "SMMU_STREAM_SECURITY_PASS");
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn tme_retained_power_requires_wfi_and_real_wake_and_preserves_protection() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/tme-power");
    for case in 0..=2 {
        let directory = tempfile::tempdir().unwrap();
        let selection = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "riscv32-none-elf",
            "0x200000",
            &["-march=rv32im_zicsr", "-mabi=ilp32", selection.as_str()],
        );
        run_fixture(directory.path(), &fixture, 0x600d0000);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn arm_security_attributes_reach_the_gic() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/security");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn fuse_reads_compose_existing_words_without_expanding_the_register_map() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/fuses");
    let cases: &[(&[&str], u64)] = &[
        (&[], 0x221c8548),
        (&["-DFUSE_PTE_WRITE"], 0x221c20ec),
        (&["-DFUSE_ROM_ERROR_WRITE"], 0x221c2090),
        (&["-DFUSE_PTE_NEIGHBOR"], 0x221c20f0),
        (&["-DFUSE_KEY_STATUS_WRITE"], 0x221c8010),
    ];
    for &(extra, expected_fault) in cases {
        let directory = tempfile::tempdir().unwrap();
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            extra,
        );
        let output = run_fixture(directory.path(), &fixture, expected_fault);
        if extra.is_empty() {
            let stdout = String::from_utf8_lossy(&output.stdout);
            assert!(
                stdout.contains(
                    "FUSE_CALIBRATION_SOURCE model=qcom,sm8850-fuse-controller \
                     source=declared-zero"
                ),
                "{stdout}"
            );
        }
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn private_calibration_page_supplies_listed_words_only() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/fuses-calibration");
    let directory = tempfile::tempdir().unwrap();
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[],
    );
    let mut page = vec![0x11; 4096];
    page[0x9e0..0x9e4].copy_from_slice(&[0x78, 0x56, 0x34, 0x12]);
    page[0xb0c..0xb10].copy_from_slice(&[0xef, 0xcd, 0xab, 0x90]);
    let path = directory.path().join("calibration.bin");
    fs::write(&path, &page).unwrap();
    let output = run_fixture(directory.path(), &fixture, 0x221c8a28);
    let digest = format!("{:x}", Sha256::digest(&page));
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains(&format!(
            "FUSE_CALIBRATION_SOURCE model=qcom,sm8850-fuse-controller \
             source=private-page sha256={digest}"
        )),
        "{stdout}"
    );
    // A page with the wrong size stops elaboration.
    fs::write(&path, &page[..4095]).unwrap();
    let output = run_platform(directory.path(), &fixture);
    let errors = String::from_utf8_lossy(&output.stderr);
    assert!(!output.status.success(), "{errors}");
    assert!(errors.contains("4096 bytes"), "{errors}");
    assert!(!errors.contains("UNMODELED_ACCESS"), "{errors}");
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn ibus_retains_guest_configuration_and_rejects_unsupported_accesses() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/ibus");
    let cases: &[(&[&str], u64)] = &[
        (&[], 0x600d0000),
        (&["-DIBUS_UNSUPPORTED_STATUS"], 0x0c469008),
        (&["-DIBUS_BAD_CONTROL"], 0x0c469200),
        (&["-DIBUS_WRITE_REVISION"], 0x0c469004),
    ];
    for &(extra, expected_fault) in cases {
        let directory = tempfile::tempdir().unwrap();
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            extra,
        );
        run_fixture(directory.path(), &fixture, expected_fault);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn smem_mutex_arbitrates_tme_and_apss_owners() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/smem-mutex");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn ipcc_views_share_source_aware_routing_and_preserve_local_state() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/ipcc");
    let driver = fixture.join("driver.c");
    for (case, expected) in [
        (0, 0x600d0000),
        (1, 0x0110600c),
        (2, 0x0120600c),
        (3, 0x0110600c),
        (4, 0x01101008),
    ] {
        let directory = tempfile::tempdir().unwrap();
        let selection = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "tme",
            "riscv32-none-elf",
            "0x200000",
            &["-march=rv32im_zicsr", "-mabi=ilp32"],
        );
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[
                "-ffreestanding",
                "-fno-builtin",
                "-mgeneral-regs-only",
                "-O2",
                driver.to_str().unwrap(),
                &selection,
            ],
        );
        run_fixture(directory.path(), &fixture, expected);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn tlmm_preserves_digital_boundaries_and_rejects_absent_registers() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/tlmm");
    let cases: &[(&[&str], u64)] = &[
        (&[], 0x600d0000),
        (&["-DTLMM_HOLE"], 0x0f1d9010),
        (&["-DTLMM_PAD_IO"], 0x0f1e2004),
        (&["-DTLMM_WRITE_PRESENCE"], 0x0f100010),
        (&["-DTLMM_RESERVED"], 0x0f10001c),
    ];
    for &(extra, expected_fault) in cases {
        let directory = tempfile::tempdir().unwrap();
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            extra,
        );
        run_fixture(directory.path(), &fixture, expected_fault);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn tsens_oem_auto_adjust_calibration_reset_and_conversion() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/tsens");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn tsens_rejects_a_watchdog_shorter_than_conversion() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/tsens");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &["-DTSENS_SHORT_WATCHDOG"],
    );
    run_fixture(directory.path(), &fixture, 0x0c222004);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn timer_frames_share_clock_and_preserve_signed_deadlines() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/timer");
    let cases: &[(&[&str], u64)] = &[
        (&[], 0x600d0000),
        (&["-DTIMER_WIDE_FREQUENCY"], 0x17811010),
        (&["-DTIMER_WIDE_COMPARE"], 0x17811020),
        (&["-DTIMER_WIDE_CONFIGURATION"], 0x17810000),
        (&["-DTIMER_WIDE_COUNTER_WRITE"], 0x17811000),
    ];
    for &(extra, expected_fault) in cases {
        let directory = tempfile::tempdir().unwrap();
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            extra,
        );
        run_fixture(directory.path(), &fixture, expected_fault);
    }
}
#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn uart_transmits_exact_guest_bytes_and_delivers_irqs() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/uart");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
    assert_eq!(
        std::fs::read(directory.path().join("uart-tx.bin")).unwrap(),
        b"UART TEST:\x00\xff\n"
    );
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn tme_readiness_requires_a_complete_live_response_ring() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/tme-status");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn pll_votes_and_reset_control_clock_availability() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/pll");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn ufs_boot_selection_reads_the_existing_lun_backends() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/ufs");
    let driver = fixture.join("driver.c");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[
            "-ffreestanding",
            "-fno-builtin",
            "-mgeneral-regs-only",
            "-O2",
            driver.to_str().unwrap(),
        ],
    );
    for lun in 0..6 {
        std::fs::write(
            directory.path().join(format!("lun{lun}.img")),
            vec![0x40 + lun; 4096 * usize::from(if lun < 3 { 1 } else { lun - 1 })],
        )
        .unwrap();
    }
    let output = run_fixture(directory.path(), &fixture, 0x600d0000);
    let log = String::from_utf8_lossy(&output.stderr);
    // Bulk access turns each 4 KiB DMA mapping into one transaction.
    // The three-block LUN4 read writes three distinct chunks into the data buffer.
    for address in [0x14803000u64, 0x14804000, 0x14805000] {
        let expected = format!(
            "address={address:#x} operation=write value=0x4444444444444444 width=4096"
        );
        assert!(
            log.lines().any(|line| {
                line.starts_with("ACCESS_TRACE core=platform.ufs_dma_access ")
                    && line.contains(&expected)
            }),
            "UFS did not dispatch one 4 KiB DMA transaction at {address:#x}: {log}"
        );
    }
    // The APSS initiator is not bulk-enabled. Its control cell remains one 8-byte access.
    let control: Vec<_> = log
        .lines()
        .filter(|line| line.starts_with("ACCESS_TRACE core=platform.apss_access "))
        .collect();
    assert!(
        control.iter().any(|line| line.contains(
            "address=0x14806000 operation=write value=0x1122334455667788 width=8"
        )),
        "The APSS initiator did not dispatch the 8-byte control write: {log}"
    );
    assert!(
        control.iter().any(|line| line.contains(
            "address=0x14806000 operation=read value=0x1122334455667788 width=8"
        )),
        "The APSS initiator did not dispatch the 8-byte control read: {log}"
    );
    assert!(
        control.iter().all(|line| line.ends_with("width=8")),
        "A non-enabled initiator exceeded its eight-byte access limit: {log}"
    );
    assert_eq!(
        std::fs::read(directory.path().join("lun1.img")).unwrap(),
        [0x5a; 4096]
    );
    assert_eq!(
        std::fs::read(directory.path().join("lun2.img")).unwrap(),
        [0x42; 4096]
    );
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn smmu_native_consumer_translates_and_retains_faults_on_dedicated_irqs() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/smmu");
    let driver = fixture.join("driver.c");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[
            "-ffreestanding",
            "-fno-builtin",
            "-mgeneral-regs-only",
            "-O2",
            driver.to_str().unwrap(),
        ],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn protection_revokes_existing_write_access() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/protection");
    for wide in [false, true] {
        let directory = tempfile::tempdir().unwrap();
        let flags: &[&str] = if wide {
            &["-DWIDE_ADDRESS", "-DRAM_BASE=0x114800000"]
        } else {
            &[]
        };
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            flags,
        );
        if wide {
            std::fs::write(
                directory.path().join("platform.lua"),
                format!(
                    "dofile('{}')\n\
                     platform.ram.target_socket.address=0x1147f0000\n\
                     platform.protection.address_bits=40\n\
                     platform.protection_port.protected_base=0\n\
                     platform.protection_port.protected_size=0\n",
                    fixture.join("platform.lua").display()
                ),
            )
            .unwrap();
            run_fixture(directory.path(), directory.path(), 0x114800000);
        } else {
            run_fixture(directory.path(), &fixture, 0x14800000);
        }
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn cpucp_cease_requires_reset_and_uses_the_published_vector() {
    let fixtures = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures");
    let source = fixtures.join("cpucp-reset");
    for (name, extra) in [
        ("cpucp-reset", &[][..]),
        ("cpucp-reset-v2", &["-DEXTENDED"][..]),
    ] {
        let directory = tempfile::tempdir().unwrap();
        compile_guest(
            directory.path(),
            &source,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            extra,
        );
        compile_guest(
            directory.path(),
            &source,
            "consumer",
            "riscv64-none-elf",
            "0x334000",
            &["-march=rv64im_zicsr", "-mabi=lp64"],
        );
        run_fixture(directory.path(), &fixtures.join(name), 0x600d0000);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn cpucp_legacy_rnmi_preserves_reset_traps_interrupts_and_disabled_boundary() {
    let fixtures = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures");
    let fixture = fixtures.join("cpucp-rnmi");
    for enabled in [true, false] {
        let directory = tempfile::tempdir().unwrap();
        compile_guest(
            directory.path(),
            &fixtures.join("cpucp-reset"),
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[],
        );
        let mut flags = vec!["-march=rv64im_zicsr", "-mabi=lp64"];
        if !enabled {
            flags.push("-DDISABLED");
        }
        compile_guest(
            directory.path(),
            &fixture,
            "consumer",
            "riscv64-none-elf",
            "0x334000",
            &flags,
        );
        std::fs::write(
            directory.path().join("platform.lua"),
            format!(
                "dofile('{}')\nplatform.boot.legacy_rnmi={enabled}\n",
                fixture.join("platform.lua").display()
            ),
        )
        .unwrap();
        run_fixture(directory.path(), directory.path(), 0x600d0000);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn cpucp_subsystem_routes_real_mailbox_interrupts_and_preserves_workspace() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/cpucp-subsystem");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[],
    );
    compile_guest(
        directory.path(),
        &fixture,
        "consumer",
        "riscv64-none-elf",
        "0x334000",
        &["-march=rv64im_zicsr", "-mabi=lp64"],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &["-DABSENT_PEER"],
    );
    std::fs::write(
        directory.path().join("platform.lua"),
        format!(
            "dofile('{}')\nplatform.peer_mailbox.peer_path=''\n",
            fixture.join("platform.lua").display()
        ),
    )
    .unwrap();
    let output = run_fixture(directory.path(), directory.path(), 0x18830104);
    let message = "CPUCP mailbox TX has no declared remote peer";
    assert!(
        String::from_utf8_lossy(&output.stderr).contains(message)
            || String::from_utf8_lossy(&output.stdout).contains(message)
    );
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn cpucp_secure_notification_preserves_pending_through_service() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/cpucp-secure-notify");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[],
    );
    compile_guest(
        directory.path(),
        &fixture,
        "consumer",
        "riscv64-none-elf",
        "0x334000",
        &["-march=rv64im_zicsr", "-mabi=lp64"],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn cpucp_second_hart_requires_publication_and_release() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/cpucp-second-hart");
    compile_guest(directory.path(), &fixture, "producer", "aarch64-none-elf", "0x300000", &[]);
    for (name, address) in [("consumer", "0x334000"), ("hart1", "0x400000")] {
        compile_guest(directory.path(), &fixture, name, "riscv64-none-elf", address,
            &["-march=rv64im_zicsr", "-mabi=lp64"]);
    }
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn cpu_domain_acknowledgements_require_live_hardware_and_deliver_reset() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/cpu-domain");
    let driver = fixture.join("driver.c");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[
            "-ffreestanding",
            "-fno-builtin",
            "-mgeneral-regs-only",
            "-O2",
            driver.to_str().unwrap(),
        ],
    );
    compile_guest(
        directory.path(),
        &fixture,
        "consumer",
        "aarch64-none-elf",
        "0x334000",
        &[],
    );
    // The PDP guest is raw Thumb code with a fixed cold and warm table layout.
    let pdp_script = format!("-Wl,--script={}", fixture.join("pdp.ld").display());
    compile_guest(
        directory.path(),
        &fixture,
        "pdp",
        "thumbv7em-none-eabi",
        "0x20000000",
        &[pdp_script.as_str()],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn pdp_windows_remap_and_revoke_cached_aliases() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/pdp-windows");
    let driver = fixture.join("driver.c");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[
            "-ffreestanding",
            "-fno-builtin",
            "-mgeneral-regs-only",
            "-O2",
            driver.to_str().unwrap(),
        ],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

struct SignedImage {
    bytes: Vec<u8>,
    entitlement: Vec<u8>,
    outer_signature: usize,
    certificate_signature: usize,
}

fn generate_key(directory: &Path, name: &str) -> PathBuf {
    let path = directory.join(name);
    let output = Command::new("openssl")
        .args(["ecparam", "-name", "secp521r1", "-genkey", "-noout", "-out"])
        .arg(&path)
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    path
}

fn public_point(key: &Path, directory: &Path, name: &str) -> [u8; 136] {
    let der = directory.join(format!("{name}.public.der"));
    let output = Command::new("openssl")
        .args(["ec", "-in"])
        .arg(key)
        .args(["-pubout", "-outform", "DER", "-out"])
        .arg(&der)
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let encoded = std::fs::read(der).unwrap();
    let point = &encoded[encoded.len() - 133..];
    assert_eq!(point[0], 4);
    let mut result = [0_u8; 136];
    result[2..68].copy_from_slice(&point[1..67]);
    result[70..136].copy_from_slice(&point[67..133]);
    result
}

fn der_length(bytes: &[u8], offset: &mut usize) -> usize {
    let first = bytes[*offset];
    *offset += 1;
    if first & 0x80 == 0 {
        return usize::from(first);
    }
    let count = usize::from(first & 0x7f);
    let mut length = 0;
    for _ in 0..count {
        length = (length << 8) | usize::from(bytes[*offset]);
        *offset += 1;
    }
    length
}

fn parse_signature<const N: usize>(bytes: &[u8]) -> [u8; N] {
    let mut offset = 0;
    assert_eq!(bytes[offset], 0x30);
    offset += 1;
    let length = der_length(bytes, &mut offset);
    assert_eq!(offset + length, bytes.len());
    assert_eq!(N % 2, 0);
    let mut signature = [0_u8; N];
    for part in 0..2 {
        assert_eq!(bytes[offset], 0x02);
        offset += 1;
        let length = der_length(bytes, &mut offset);
        let mut value = &bytes[offset..offset + length];
        offset += length;
        while value.len() > 1 && value[0] == 0 {
            value = &value[1..];
        }
        assert!(value.len() <= N / 2);
        let end = (part + 1) * (N / 2);
        signature[end - value.len()..end].copy_from_slice(value);
    }
    assert_eq!(offset, bytes.len());
    signature
}

fn sign(key: &Path, message: &[u8], directory: &Path, name: &str) -> [u8; 136] {
    let message_path = directory.join(format!("{name}.message"));
    let signature_path = directory.join(format!("{name}.signature"));
    std::fs::write(&message_path, message).unwrap();
    let output = Command::new("openssl")
        .args(["dgst", "-sha512", "-sign"])
        .arg(key)
        .arg("-out")
        .arg(&signature_path)
        .arg(&message_path)
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    parse_signature(&std::fs::read(signature_path).unwrap())
}

fn tlv(tag: u16, value: &[u8]) -> Vec<u8> {
    let mut encoded = Vec::with_capacity(4 + value.len() + 3);
    encoded.extend_from_slice(&tag.to_le_bytes());
    encoded.extend_from_slice(&u16::try_from(value.len()).unwrap().to_le_bytes());
    encoded.extend_from_slice(value);
    while encoded.len() % 4 != 0 {
        encoded.push(0);
    }
    encoded
}

fn scalar(tag: u16, value: u32) -> Vec<u8> {
    tlv(tag, &value.to_le_bytes())
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|byte| format!("{byte:02x}")).collect()
}

fn signed_image(
    root: &Path,
    subject: &Path,
    subject_point: &[u8; 136],
    program: &[u8],
    chip_version: u32,
    directory: &Path,
    name: &str,
) -> SignedImage {
    // The chip constraint is synthetic. The test pins the complete entitlement digest.
    let entitlement = tlv(
        0x8281,
        &[
            scalar(0x8300, 1),
            tlv(0x8380, &tlv(0x8420, &chip_version.to_le_bytes())),
            tlv(0x8381, &[scalar(0x8422, 0x37), scalar(0x8423, 4)].concat()),
            tlv(
                0x8390,
                &[scalar(0x8410, 3), tlv(0x8411, subject_point)].concat(),
            ),
        ]
        .concat(),
    );
    let certificate_signature = sign(
        root,
        &entitlement,
        directory,
        &format!("{name}-certificate"),
    );
    let certificate = tlv(
        0x8080,
        &[
            scalar(0x8101, 1),
            scalar(0x8102, 1),
            scalar(0x8103, 2),
            tlv(0x8180, &entitlement),
            tlv(0x81ff, &certificate_signature),
        ]
        .concat(),
    );
    let program_hash = Sha512::digest(program);
    let command = tlv(
        0x8291,
        &[
            scalar(0x8301, 1),
            scalar(0x8351, 0x37),
            scalar(0x8352, 1),
            tlv(0x8380, &scalar(0x8401, 0xa01bffff)),
            tlv(
                0x8384,
                &[scalar(0x8440, 5), tlv(0x8441, program_hash.as_slice())].concat(),
            ),
        ]
        .concat(),
    );
    let prefix = [tlv(0x0001, &[2]), command].concat();
    let body_length = prefix.len() + 4 + 136 + certificate.len();
    let mut bytes = Vec::new();
    bytes.extend_from_slice(&0x0182_u16.to_le_bytes());
    bytes.extend_from_slice(&u16::try_from(body_length).unwrap().to_le_bytes());
    bytes.extend_from_slice(&prefix);
    let outer_signature = sign(subject, &bytes, directory, &format!("{name}-command"));
    let outer_signature_offset = bytes.len() + 4;
    bytes.extend_from_slice(&tlv(0x81ff, &outer_signature));
    let certificate_start = bytes.len();
    bytes.extend_from_slice(&certificate);
    let certificate_signature_offset = certificate_start + 4 + 3 * 8 + 4 + entitlement.len() + 4;
    assert!(bytes.len() <= 4096);
    bytes.resize(4096, 0);
    SignedImage {
        bytes,
        entitlement,
        outer_signature: outer_signature_offset,
        certificate_signature: certificate_signature_offset,
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn sequencer_sha384_uses_guest_dma_and_serialized_split_state() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/tme-hash");
    let original: Vec<u8> = (0..647).map(|i| ((i * 37 + 11) & 0xff) as u8).collect();
    let mut changed = original.clone();
    changed[191] ^= 0x80;
    let expected = sha2::Sha384::digest(&original);
    let changed_expected = sha2::Sha384::digest(&changed);
    assert_ne!(expected, changed_expected);
    let mut source = vec![0; 0x1000];
    source[..647].copy_from_slice(&original);
    source[0x400..0x400 + 647].copy_from_slice(&changed);
    source[0x800..0x830].copy_from_slice(&expected);
    source[0x830..0x860].copy_from_slice(&changed_expected);
    // Positive cases compare split and one-shot hashes with independent digests.
    // Other cases reject missing descriptors, unmapped sources, unknown phases, and one-shot continuation.
    for case in 0..6 {
        let directory = tempfile::tempdir().unwrap();
        std::fs::write(directory.path().join("source.bin"), &source).unwrap();
        let define = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[&define],
        );
        run_fixture(
            directory.path(),
            &fixture,
            if case == 0 || case == 4 { 0x600d0000 } else { 0x02230060 },
        );
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, llvm-objcopy, and OpenSSL"]
fn sequencer_authentication_checks_the_complete_p521_chain() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/tme-auth");
    let root = generate_key(directory.path(), "root-key.pem");
    let subject = generate_key(directory.path(), "subject-key.pem");
    let wrong_root = generate_key(directory.path(), "wrong-root-key.pem");
    let root_point = public_point(&root, directory.path(), "root");
    let subject_point = public_point(&subject, directory.path(), "subject");
    let wrong_root_point = public_point(&wrong_root, directory.path(), "wrong-root");
    std::fs::write(directory.path().join("root.bin"), root_point).unwrap();
    std::fs::write(directory.path().join("wrong-root.bin"), wrong_root_point).unwrap();
    let program: Vec<u8> = (0..4096).map(|i| ((i * 37 + 11) & 0xff) as u8).collect();
    let approved = signed_image(
        &root,
        &subject,
        &subject_point,
        &program,
        0xa01b0200,
        directory.path(),
        "approved",
    );
    let unapproved = signed_image(
        &root,
        &subject,
        &subject_point,
        &program,
        0xa01b0201,
        directory.path(),
        "unapproved",
    );
    assert_ne!(approved.entitlement, unapproved.entitlement);
    let pin = hex(Sha256::digest(&approved.entitlement).as_slice());
    std::fs::write(
        directory.path().join("trust.lua"),
        format!("return {{approved='{pin}'}}\n"),
    )
    .unwrap();
    let mut tampered_program = program.clone();
    tampered_program[0] ^= 0xff;
    // Change a middle byte of r, not the two leading zero bytes.
    let mut tampered_command = approved.bytes.clone();
    tampered_command[approved.outer_signature + 20] ^= 1;
    let mut tampered_certificate = approved.bytes.clone();
    tampered_certificate[approved.certificate_signature + 20] ^= 1;
    // The nonzero leading byte must fail before the DER conversion.
    let mut malformed_command = approved.bytes.clone();
    malformed_command[approved.outer_signature] = 0xff;
    // Platform instance i receives program{i}.bin and input{i}.bin.
    let programs = [
        program.clone(),
        tampered_program,
        program.clone(),
        program.clone(),
        program.clone(),
        program.clone(),
        program.clone(),
        program.clone(),
    ];
    let inputs = [
        approved.bytes.clone(),
        approved.bytes.clone(),
        tampered_command,
        tampered_certificate,
        approved.bytes.clone(),
        unapproved.bytes,
        malformed_command,
        approved.bytes.clone(),
    ];
    for (index, (program, input)) in programs.iter().zip(inputs.iter()).enumerate() {
        std::fs::write(
            directory.path().join(format!("program{index}.bin")),
            program,
        )
        .unwrap();
        std::fs::write(directory.path().join(format!("input{index}.bin")), input).unwrap();
    }
    let mut mailbox = [0_u8; 16];
    mailbox[0..4].copy_from_slice(&0x4c425054_u32.to_le_bytes());
    mailbox[4..8].copy_from_slice(&2_u32.to_le_bytes());
    std::fs::write(directory.path().join("mailbox.bin"), mailbox).unwrap();
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn native_clock_epochs_preserve_mailboxes_across_debugger_pauses() {
    use std::io::{Read, Write};
    use std::os::unix::net::UnixStream;
    use std::time::{Duration, Instant};

    // Kill and reap the owned process if a debugger assertion fails.
    struct Guest(std::process::Child);
    impl Drop for Guest {
        fn drop(&mut self) {
            let _ = self.0.kill();
            let _ = self.0.wait();
        }
    }

    struct Remote {
        stream: UnixStream,
        deadline: Instant,
    }
    impl Remote {
        fn byte(&mut self) -> u8 {
            loop {
                assert!(Instant::now() < self.deadline, "Clock debugger timed out");
                let mut byte = [0];
                match self.stream.read_exact(&mut byte) {
                    Ok(()) => return byte[0],
                    Err(error)
                        if matches!(
                            error.kind(),
                            std::io::ErrorKind::WouldBlock
                                | std::io::ErrorKind::TimedOut
                                | std::io::ErrorKind::Interrupted
                        ) => {}
                    Err(error) => panic!("Clock debugger disconnected: {error}"),
                }
            }
        }

        fn send(&mut self, command: &str) {
            let checksum = command.bytes().fold(0_u8, u8::wrapping_add);
            write!(self.stream, "${command}#{checksum:02x}").unwrap();
        }

        fn command(&mut self, command: &str) -> Vec<u8> {
            self.send(command);
            loop {
                match self.byte() {
                    b'+' => {}
                    b'$' => break,
                    other => panic!("Unexpected clock debugger prefix: {other:#x}"),
                }
            }
            let mut response = Vec::new();
            loop {
                let byte = self.byte();
                if byte == b'#' {
                    break;
                }
                assert!(response.len() < 256, "Clock debugger packet exceeds limit");
                response.push(byte);
            }
            let digits = [self.byte(), self.byte()];
            let checksum = u8::from_str_radix(std::str::from_utf8(&digits).unwrap(), 16).unwrap();
            assert_eq!(
                response
                    .iter()
                    .fold(0_u8, |sum, byte| sum.wrapping_add(*byte)),
                checksum
            );
            self.stream.write_all(b"+").unwrap();
            response
        }

        fn stop(&mut self, command: &str) {
            let response = self.command(command);
            assert!(
                response.starts_with(b"T05") || response.starts_with(b"S05"),
                "Expected a genuine debugger stop: {response:?}"
            );
        }
    }

    let directory = tempfile::tempdir().unwrap();
    // Keep the Unix endpoint short and private, independent of TMPDIR length.
    let sockets = tempfile::tempdir_in("/tmp").unwrap();
    let socket = sockets.path().join("apss.sock");
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/clock-epoch");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[],
    );
    compile_guest(
        directory.path(),
        &fixture,
        "consumer",
        "riscv32-none-elf",
        "0x200000",
        &["-march=rv32im", "-mabi=ilp32"],
    );
    let repository = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../..");
    let errors_path = directory.path().join("stderr.log");
    let mut guest = Guest(
        Command::new(install_prefix().join("bin/qcom-vp-system"))
            .arg("-l")
            .arg(fixture.join("platform.lua"))
            .env("QCOM_VP_PROBE_ROOT", directory.path())
            .env("QCOM_VP_PLATFORM_DIR", repository.join("platforms"))
            .env("QCOM_VP_CLOCK_SOCKET", &socket)
            .stdin(std::process::Stdio::null())
            .stdout(std::process::Stdio::null())
            .stderr(std::fs::File::create(&errors_path).unwrap())
            .spawn()
            .unwrap(),
    );
    let deadline = Instant::now() + Duration::from_secs(20);
    let stream = loop {
        assert!(
            Instant::now() < deadline,
            "Clock debugger socket did not appear: {}",
            std::fs::read_to_string(&errors_path).unwrap()
        );
        assert!(
            guest.0.try_wait().unwrap().is_none(),
            "Clock guest exited before debugger connection: {}",
            std::fs::read_to_string(&errors_path).unwrap()
        );
        match UnixStream::connect(&socket) {
            Ok(stream) => break stream,
            Err(error)
                if matches!(
                    error.kind(),
                    std::io::ErrorKind::NotFound | std::io::ErrorKind::ConnectionRefused
                ) =>
            {
                std::thread::sleep(Duration::from_millis(10))
            }
            Err(error) => panic!("Cannot connect clock debugger: {error}"),
        }
    };
    stream
        .set_read_timeout(Some(Duration::from_millis(100)))
        .unwrap();
    stream
        .set_write_timeout(Some(Duration::from_millis(100)))
        .unwrap();
    let mut remote = Remote { stream, deadline };
    // The RV instance runs while APSS starts with -S. Their native epochs differ.
    std::thread::sleep(Duration::from_millis(500));
    remote.stop("s");
    remote.stop("s");
    for address in [0x300100, 0x300200] {
        assert_eq!(remote.command(&format!("Z1,{address:x},4")), b"OK");
        remote.stop("c");
        // Let the other instance advance while APSS retains its stopped epoch.
        std::thread::sleep(Duration::from_secs(1));
        assert_eq!(remote.command(&format!("z1,{address:x},4")), b"OK");
        remote.stop("s");
    }
    remote.send("c");
    let status = loop {
        if let Some(status) = guest.0.try_wait().unwrap() {
            break status;
        }
        assert!(
            Instant::now() < deadline,
            "Clock guest did not consume both publications and advance its timer: {}",
            std::fs::read_to_string(&errors_path).unwrap()
        );
        std::thread::sleep(Duration::from_millis(10));
    };
    let errors = std::fs::read_to_string(&errors_path).unwrap();
    assert_eq!(status.code(), Some(2), "{errors}");
    let faults: Vec<_> = errors
        .lines()
        .filter(|line| line.starts_with("UNMODELED_ACCESS "))
        .collect();
    assert_eq!(faults.len(), 1, "{errors}");
    assert!(faults[0].contains("address=0x600d0000 "), "{errors}");
}

#[test]
#[ignore = "Requires make build, Clang, LLD, llvm-objcopy, and OpenSSL"]
fn sequencer_rejects_invalid_p384_signatures_and_resets_status() {
    use sha2::{Digest, Sha384};

    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/tme-signature");
    let message = b"A synthetic sequencer signature must authenticate this exact message.";
    std::fs::write(root.join("message"), message).unwrap();
    for arguments in [
        &[
            "ecparam",
            "-name",
            "secp384r1",
            "-genkey",
            "-noout",
            "-out",
            "key.pem",
        ][..],
        &[
            "pkey",
            "-in",
            "key.pem",
            "-pubout",
            "-outform",
            "DER",
            "-out",
            "public.der",
        ][..],
        &[
            "dgst",
            "-sha384",
            "-sign",
            "key.pem",
            "-out",
            "signature.der",
            "message",
        ][..],
    ] {
        let output = Command::new("openssl")
            .args(arguments)
            .current_dir(root)
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        );
    }
    let public = std::fs::read(root.join("public.der")).unwrap();
    let point = &public[public.len() - 97..];
    assert_eq!(point[0], 4);
    let signature = parse_signature::<96>(&std::fs::read(root.join("signature.der")).unwrap());
    let mut input = [0; 244];
    input[4..100].copy_from_slice(&point[1..]);
    input[100..196].copy_from_slice(&signature);
    input[196..].copy_from_slice(&Sha384::digest(message));
    std::fs::write(root.join("input.bin"), input).unwrap();
    let driver = fixture.join("driver.c");
    for unsupported_curve in [false, true] {
        let mut arguments = vec![
            "-ffreestanding",
            "-fno-builtin",
            "-mgeneral-regs-only",
            "-O2",
            driver.to_str().unwrap(),
        ];
        if unsupported_curve {
            arguments.push("-DUNSUPPORTED_CURVE");
        }
        compile_guest(
            root,
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &arguments,
        );
        run_fixture(
            root,
            &fixture,
            if unsupported_curve { 0x02230060 } else { 0x600d0000 },
        );
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn sequencer_programs_debug_policy_and_rejects_stale_address_lists() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/tme-debug-access");
    let driver = fixture.join("driver.c");
    for incomplete in [false, true] {
        let directory = tempfile::tempdir().unwrap();
        let mut arguments = vec![
            "-ffreestanding",
            "-fno-builtin",
            "-mgeneral-regs-only",
            "-O2",
            driver.to_str().unwrap(),
        ];
        if incomplete {
            arguments.push("-DINCOMPLETE_PAYLOAD");
        }
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &arguments,
        );
        run_fixture(
            directory.path(),
            &fixture,
            if incomplete { 0x02230060 } else { 0x600d0000 },
        );
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn vmid_mapper_isolates_security_banks_and_bounds_contexts() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/vmid-mapper");
    for (case, terminal) in [(0, 0x600d0000), (1, 0x008c6c10), (2, 0x008c6008)] {
        let directory = tempfile::tempdir().unwrap();
        let definition = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[&definition],
        );
        run_fixture(directory.path(), &fixture, terminal);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn xpu_namespace_keeps_foreign_locks_and_requires_trusted_secure_authority() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/xpu-namespace");
    for (case, terminal) in [(0, 0x600d0000), (1, 0x06f00100), (2, 0x01f01030)] {
        let directory = tempfile::tempdir().unwrap();
        let definition = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[&definition],
        );
        run_fixture(directory.path(), &fixture, terminal);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn xpu_namespace_revokes_logical_principals_and_cached_data_access() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/xpu-namespace");
    for (case, terminal) in [
        (7, 0x600d0000),
        (8, 0x14800000),
        (9, 0x14800000),
        (10, 0x14800000),
        (11, 0x01f01030),
        (12, 0x600d0000),
        (13, 0x15800000),
        (14, 0x14800000),
        (15, 0x14800000),
        (16, 0x14800000),
        (17, 0x14800000),
    ] {
        let directory = tempfile::tempdir().unwrap();
        let definition = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[&definition],
        );
        run_fixture(directory.path(), &fixture, terminal);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn xpu_aperture_bounds_preserve_the_masked_exclusive_limit() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/xpu-namespace");
    for (case, terminal) in [
        (3, 0x14803ffc),
        (4, 0x14800000),
        (5, 0x01f01004),
        (6, 0x01f01004),
    ] {
        let directory = tempfile::tempdir().unwrap();
        let definition = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[&definition],
        );
        run_fixture(directory.path(), &fixture, terminal);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn sequencer_clears_backing_bytes_and_rejects_invalid_clear_frames() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/tme-memory-clear");
    let driver = fixture.join("driver.c");
    for case in 0..=6 {
        let directory = tempfile::tempdir().unwrap();
        let definition = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[
                "-ffreestanding",
                "-fno-builtin",
                "-mgeneral-regs-only",
                "-O2",
                driver.to_str().unwrap(),
                &definition,
            ],
        );
        run_fixture(
            directory.path(),
            &fixture,
            if case == 0 { 0x600d0000 } else { 0x02230060 },
        );
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn managed_keys_bind_inputs_enforce_ownership_and_erase_on_clear() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/tme-keys");
    let directory = tempfile::tempdir().unwrap();
    let mut input: Vec<u8> = (0..32).chain(0..32).collect();
    input[37] ^= 1;
    std::fs::write(directory.path().join("input.bin"), input).unwrap();
    let driver = fixture.join("driver.c");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[
            "-ffreestanding",
            "-fno-builtin",
            "-mgeneral-regs-only",
            "-O2",
            driver.to_str().unwrap(),
        ],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn declared_root_profile_reaches_tme_managed_key_output() {
    let guest_fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/tme-keys");
    let platform_fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/key-roots");
    let directory = tempfile::tempdir().unwrap();
    let mut input: Vec<u8> = (0..32).chain(0..32).collect();
    input[37] ^= 1;
    std::fs::write(directory.path().join("input.bin"), input).unwrap();
    let driver = guest_fixture.join("driver.c");
    compile_guest(
        directory.path(),
        &guest_fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[
            "-ffreestanding",
            "-fno-builtin",
            "-mgeneral-regs-only",
            "-O2",
            "-DROOT_PROFILE",
            driver.to_str().unwrap(),
        ],
    );
    run_fixture(directory.path(), &platform_fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, a C++17 compiler, and OpenSSL"]
fn explicit_tme_roots_change_and_reproduce_managed_hash_and_reject_invalid_values() {
    run_native_consumer_modes(
        "key-roots",
        &[
            (&["--root-profile"], "TME_ROOT_PROFILE_PASS"),
            (&["--invalid-length"], "TME_ROOT_INVALID_PASS"),
            (&["--invalid-hex"], "TME_ROOT_INVALID_PASS"),
            (&["--disabled"], "TME_ROOT_INVALID_PASS"),
        ],
    );
}

#[test]
#[ignore = "Requires make build, a C++17 compiler, and OpenSSL"]
fn wrapped_key_export_authenticates_and_installs_only_after_verification() {
    run_native_consumer("tme-wrap", "TME_KEY_WRAP_PASS");
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn transport_key_broadcast_requires_the_command_and_the_receive_window() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/tme-warmup");
    let driver = fixture.join("driver.c");
    for case in 0..=3 {
        // Cases two and three fault on the refused execute write.
        let terminal = if case < 2 { 0x600d0000 } else { 0x02230060 };
        let directory = tempfile::tempdir().unwrap();
        std::fs::write(directory.path().join("case.txt"), format!("{case}\n")).unwrap();
        let definition = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[
                "-ffreestanding",
                "-fno-builtin",
                "-mgeneral-regs-only",
                "-O2",
                driver.to_str().unwrap(),
                &definition,
            ],
        );
        run_fixture(directory.path(), &fixture, terminal);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn hwkm_shared_identity_and_key_status_follow_the_engine() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/tme-warmup");
    let driver = fixture.join("driver.c");
    for (case, terminal) in [(4, 0x600d0000), (5, 0x01df0100)] {
        let directory = tempfile::tempdir().unwrap();
        std::fs::write(directory.path().join("case.txt"), format!("{case}\n")).unwrap();
        let definition = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[
                "-ffreestanding",
                "-fno-builtin",
                "-mgeneral-regs-only",
                "-O2",
                driver.to_str().unwrap(),
                &definition,
            ],
        );
        run_fixture(directory.path(), &fixture, terminal);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn authorization_denials_retain_command_errors_and_reset_before_entropy() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/tme-debug-denial");
    let driver = fixture.join("driver.c");
    for (case, rom_patch) in (0..=10).flat_map(|case| [false, true].map(|patch| (case, patch))) {
        let terminal = if case == 0 { 0x600d0000 } else { 0x02230060 };
        let directory = tempfile::tempdir().unwrap();
        let definition = format!("-DCASE={case}");
        let command = format!("-DROM_PATCH={}", u8::from(rom_patch));
        // An omitted parameter uses the model's false default. Keep the other denial enabled.
        let policy = match (case == 6, rom_patch) {
            (true, false) => "return {rom_patch=true}\n",
            (true, true) => "return {debug=true}\n",
            (false, _) => "return {debug=true,rom_patch=true}\n",
        };
        std::fs::write(directory.path().join("policy.lua"), policy).unwrap();
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[
                "-ffreestanding",
                "-fno-builtin",
                "-mgeneral-regs-only",
                "-O2",
                driver.to_str().unwrap(),
                &definition,
                &command,
            ],
        );
        run_fixture(directory.path(), &fixture, terminal);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn pdp_services_require_admission_and_preserve_mailbox_ownership() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/pdp-scmi");
    let driver = fixture.join("driver.c");
    for case in 0..=8 {
        let directory = tempfile::tempdir().unwrap();
        let selection = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[
                "-ffreestanding",
                "-fno-builtin",
                "-mgeneral-regs-only",
                "-O2",
                driver.to_str().unwrap(),
                &selection,
            ],
        );
        let expected = if case == 0 || case >= 7 {
            0x600d0000
        } else {
            0x19980104
        };
        run_fixture(directory.path(), &fixture, expected);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn qsee_fault_configuration_preserves_ownership() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/qsee-fault");
    for soc in ["sm8850", "sm8845"] {
        for (case, terminal) in [(0, 0x600d0000), (1, 0x01500020), (2, 0x01500008)] {
            let directory = tempfile::tempdir().unwrap();
            std::fs::write(directory.path().join("oem.txt"), format!("{soc}\n")).unwrap();
            let selection = format!("-DCASE={case}");
            compile_guest(
                directory.path(),
                &fixture,
                "producer",
                "aarch64-none-elf",
                "0x300000",
                &[&selection],
            );
            run_fixture(directory.path(), &fixture, terminal);
        }
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn pmic_configuration_preserves_aliases_keys_and_uploaded_bytes() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/pmic-psi");
    for case in 0..=12 {
        let directory = tempfile::tempdir().unwrap();
        let selection = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[&selection],
        );
        run_fixture(
            directory.path(),
            &fixture,
            match case {
                1..=3 | 10..=11 => 0x0c900000,
                6 => 0x0c4c0040,
                7 => 0x0c4c0020,
                8 => 0x0c4c00a0,
                12 => 0x0c900200,
                _ => 0x600d0000,
            },
        );
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn pmic_dump_reads_unowned_bytes_without_widening_rejection() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/pmic-dump");
    for (case, terminal, logged) in [
        (0, 0x600d0000, &["890", "891", "892", "893", "894", "895", "896", "897"][..]),
        (1, 0x600d0000, &["84c"][..]),
        (2, 0x600d0000, &[][..]),
        (3, 0x0c4c0020, &[][..]),
        (4, 0x0c900000, &[][..]),
        (5, 0x0c4c0060, &[][..]),
        (6, 0x600d0000, &["850"][..]),
        (7, 0x600d0000, &["8d9"][..]),
        (8, 0x600d0000, &["16b0"][..]),
        (9, 0x600d0000, &["1621", "1622", "1623", "1624", "1625", "1626"][..]),
    ] {
        let directory = tempfile::tempdir().unwrap();
        let selection = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[&selection],
        );
        let output = run_fixture(directory.path(), &fixture, terminal);
        let errors = String::from_utf8_lossy(&output.stderr);
        let observed: Vec<&str> = errors
            .lines()
            .filter(|line| line.starts_with("PMIC_DUMP_READ_UNDECLARED "))
            .collect();
        let expected: Vec<String> = logged
            .iter()
            .map(|address| format!("PMIC_DUMP_READ_UNDECLARED bus=0 sid=0 address=0x{address}"))
            .collect();
        assert_eq!(observed, expected, "{errors}");
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn pmic_dump_present_profile_covers_present_groups_only() {
    let fixture =
        Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/pmic-dump-present");
    for (case, terminal, logged) in [
        (0, 0x600d0000, &["890", "891", "892", "893", "894", "895", "896", "897"][..]),
        (1, 0x600d0000, &["c020"][..]),
        (2, 0x0c4c0060, &[][..]),
        (3, 0x0c900000, &[][..]),
    ] {
        let directory = tempfile::tempdir().unwrap();
        let selection = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[&selection],
        );
        let output = run_fixture(directory.path(), &fixture, terminal);
        let errors = String::from_utf8_lossy(&output.stderr);
        let observed: Vec<&str> = errors
            .lines()
            .filter(|line| line.starts_with("PMIC_DUMP_READ_UNDECLARED "))
            .collect();
        let expected: Vec<String> = logged
            .iter()
            .map(|address| format!("PMIC_DUMP_READ_UNDECLARED bus=0 sid=0 address=0x{address}"))
            .collect();
        assert_eq!(observed, expected, "{errors}");
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn pmic_scan_reads_pbs_bytes_without_moving_the_transfer() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/pmic-dump");
    for (case, terminal, logged) in [
        (10, 0x600d0000, &["1608", "1609", "160a", "160b", "160c", "160d", "160e", "160f"][..]),
        (11, 0x600d0000, &["16a2", "16a5", "16a6", "16a7"][..]),
        (12, 0x0c900400, &[][..]),
        (13, 0x600d0000, &["164c", "164d", "164e", "164f", "1651", "1652", "1653"][..]),
    ] {
        let directory = tempfile::tempdir().unwrap();
        let selection = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[&selection],
        );
        let output = run_fixture(directory.path(), &fixture, terminal);
        let errors = String::from_utf8_lossy(&output.stderr);
        let observed: Vec<&str> = errors
            .lines()
            .filter(|line| line.starts_with("PMIC_SCAN_READ_UNDECLARED "))
            .collect();
        let expected: Vec<String> = logged
            .iter()
            .map(|address| {
                format!(
                    "PMIC_SCAN_READ_UNDECLARED bus=0 sid=0 address=0x{address} policy=declared-zero"
                )
            })
            .collect();
        assert_eq!(observed, expected, "{errors}");
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn spmi_dcp_shares_routes_and_pmic_without_apss_authority() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/dcp-spmi");
    for (case, terminal) in [
        (0, 0x600d0000),
        (1, 0x4c42a004),
        (2, 0x4c4c0008),
        (3, 0x0c780008),
        (4, 0x4ca40008),
        (5, 0x4c780000),
        (6, 0x0c400004),
    ] {
        let directory = tempfile::tempdir().unwrap();
        let selection = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[&selection],
        );
        run_fixture(directory.path(), &fixture, terminal);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn pmic_discovery_preserves_seven_byte_revisions_and_unknown_accesses() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/dcp-spmi");
    for (case, terminal) in [
        (7, 0x600d0000),
        (8, 0x4c780000),
        (9, 0x4c780000),
        (10, 0x4c900200),
    ] {
        let directory = tempfile::tempdir().unwrap();
        let selection = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[&selection],
        );
        run_fixture(directory.path(), &fixture, terminal);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn pmic_virtual_absence_is_opt_in_bounded_and_logged_once() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/pmic-discovery");
    for (case, terminal) in [
        (11, 0x600d0000),
        (12, 0x4c780000),
        (13, 0x4c900200),
        (14, 0x4c780040),
        (15, 0x600d0000),
        (16, 0x600d0000),
        (17, 0x4c780000),
        (18, 0x4c780000),
        (19, 0x4c780040),
        (20, 0x600d0000),
    ] {
        let directory = tempfile::tempdir().unwrap();
        let selection = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[&selection],
        );
        let output = run_fixture(directory.path(), &fixture, terminal);
        let errors = String::from_utf8_lossy(&output.stderr);
        let mut absences = errors
            .lines()
            .filter(|line| line.starts_with("UNMODELED_PMIC_ABSENT "));
        assert_eq!(
            absences.next(),
            Some("UNMODELED_PMIC_ABSENT bus=0 sid=0 peripheral=0x2 policy=virtual-absent"),
            "{errors}"
        );
        assert!(absences.next().is_none(), "{errors}");
    }
}

#[test]
#[ignore = "Requires a C++17 compiler"]
fn pbs_memory_preserves_data_and_rejects_invalid_lifecycles() {
    let repository = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../..");
    let directory = tempfile::tempdir().unwrap();
    let executable = directory.path().join("pbs-native");
    let compile = Command::new("c++")
        .args(["-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-I"])
        .arg(repository.join("models"))
        .arg(repository.join("tools/qcom/qcom-vp/tests/fixtures/pbs-native/producer.cc"))
        .arg("-o")
        .arg(&executable)
        .output()
        .unwrap();
    assert!(
        compile.status.success(),
        "{}",
        String::from_utf8_lossy(&compile.stderr)
    );
    let run = Command::new(executable).output().unwrap();
    assert!(
        run.status.success(),
        "{}",
        String::from_utf8_lossy(&run.stderr)
    );
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn eud_enable_preserves_disconnected_status() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/eud");
    for (case, address) in [0x600d0000, 0x088e2000, 0x088e1020].into_iter().enumerate() {
        let directory = tempfile::tempdir().unwrap();
        let selection = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[&selection],
        );
        run_fixture(directory.path(), &fixture, address);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn apss_executes_mpam_pauth_and_rng() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/apss");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn apss_sibling_executes_external_code_updates() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/apss-group");
    for (name, address) in [("producer", "0x300000"), ("writer", "0x400000")] {
        compile_guest(
            directory.path(),
            &fixture,
            name,
            "aarch64-none-elf",
            address,
            &[],
        );
    }
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn cpucp_preserves_local_sram_across_ddr_backup_and_reset() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/cpucp-ddr");
    for soc in [8850, 8845] {
        let directory = tempfile::tempdir().unwrap();
        std::fs::write(
            directory.path().join("policy.lua"),
            format!("return 'sm{soc}'\n"),
        )
        .unwrap();
        let selection = format!("-DSOC={soc}");
        compile_guest(
            directory.path(),
            &fixture,
            "apss",
            "aarch64-none-elf",
            "0x300000",
            &[&selection],
        );
        compile_guest(
            directory.path(),
            &fixture,
            "cpucp",
            "riscv64-none-elf",
            "0x334000",
            &["-march=rv64im", "-mabi=lp64", &selection],
        );
        run_fixture(directory.path(), &fixture, 0x600d0000);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn apss_virtual_timer_delivers_ppi27() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/apss-timer");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn shrm_processor_reset_controls_startup_and_pending_commands() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/shrm-reset");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn cpr_measurements_require_inputs_and_follow_targets() {
    let directory = tempfile::tempdir().unwrap();
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/cpr");
    compile_guest(
        directory.path(),
        &fixture,
        "producer",
        "aarch64-none-elf",
        "0x300000",
        &[],
    );
    run_fixture(directory.path(), &fixture, 0x600d0000);
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn ddr_commands_preserve_pair_rank_state_and_report_declared_geometry() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/ddr-command");
    for (rank1_mib, unsupported, corrupt) in [
        (0, false, false),
        (512, false, false),
        (0, true, false),
        (0, false, true),
    ] {
        let directory = tempfile::tempdir().unwrap();
        std::fs::write(
            directory.path().join("policy.lua"),
            format!("return {{rank1_mib={rank1_mib}}}\n"),
        )
        .unwrap();
        let mut definitions = vec![if rank1_mib == 0 {
            "-DRANK1_PRESENT=0"
        } else {
            "-DRANK1_PRESENT=1"
        }];
        if unsupported {
            definitions.push("-DUNSUPPORTED_OPCODE");
        }
        if corrupt {
            definitions.push("-DCORRUPT_CALLBACK");
        }
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &definitions,
        );
        run_fixture(
            directory.path(),
            &fixture,
            if unsupported {
                0x31a05804
            } else if corrupt {
                0x31076064
            } else {
                0x600d0000
            },
        );
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn llcc_requests_preserve_bank_state_memory_and_security_boundaries() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/llcc");
    for (definition, expected_fault) in [
        (None, 0x600d0000),
        (Some("-DREJECTED_OFFSET=0x60000"), 0x34860000),
        (Some("-DREJECTED_OFFSET=0xb0000"), 0x348b0000),
        (Some("-DREVOKE_METADATA"), 0x80000000),
        (Some("-DREVOKE_KEY"), 0x80000000),
        (Some("-DREVOKE_CONFIG"), 0x80000000),
        (Some("-DREVOKE_BACKGROUND"), 0x80010000),
    ] {
        let directory = tempfile::tempdir().unwrap();
        let definitions: Vec<_> = definition.into_iter().collect();
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &definitions,
        );
        run_fixture(directory.path(), &fixture, expected_fault);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn gsi_initialization_requires_download_start_and_ordered_mailbox_requests() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/gsi");
    for size in [0x5248, 0x5230] {
        for case in 0..=11 {
            let directory = tempfile::tempdir().unwrap();
            let selection = format!("-DCASE={case}");
            let extent = format!("-DFW_SIZE={size}");
            compile_guest(
                directory.path(),
                &fixture,
                "producer",
                "aarch64-none-elf",
                "0x300000",
                &[&selection, &extent],
            );
            std::fs::write(
                directory.path().join("platform.lua"),
                format!(
                    "dofile('{}')\nplatform.gsi.firmware_size={size}\n",
                    fixture.join("platform.lua").display()
                ),
            )
            .unwrap();
            let expected_fault = match case {
                0 | 2 => 0x600d0000,
                1 | 5 | 6 | 7 => 0x00a04000,
                3 | 4 | 8 => 0x00a13018,
                9 => 0x00a50000,
                10 => 0x00a13400,
                11 => 0x00a13020,
                _ => unreachable!(),
            };
            run_fixture(directory.path(), directory.path(), expected_fault);
        }
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn aop_requests_halt_clocks_and_transfer_phases_preserve_hardware_state() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/aop-control");
    for case in 0..=9 {
        let directory = tempfile::tempdir().unwrap();
        let selection = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[&selection],
        );
        let expected_fault = match case {
            0 => 0x600d0000,
            1 => 0x100408,
            2 => 0x120004,
            3 => 0x140000,
            4 => 0x140004,
            5 => 0x140008,
            6 => 0x100020,
            7 => 0x100010,
            8 => 0x120000,
            9 => 0x14000c,
            _ => unreachable!(),
        };
        run_fixture(directory.path(), &fixture, expected_fault);
    }
}

#[test]
#[ignore = "Requires make build, Clang, LLD, and llvm-objcopy"]
fn aoss_cold_start_reaches_idle_and_control_writes_preserve_readonly_status() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/aoss-reset");
    for case in 0..=1 {
        let directory = tempfile::tempdir().unwrap();
        let selection = format!("-DCASE={case}");
        compile_guest(
            directory.path(),
            &fixture,
            "producer",
            "aarch64-none-elf",
            "0x300000",
            &[&selection],
        );
        run_fixture(
            directory.path(),
            &fixture,
            if case == 0 { 0x600d0000 } else { 0x101000 },
        );
    }
}
