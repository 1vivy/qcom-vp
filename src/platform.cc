// SPDX-License-Identifier: BSD-3-Clause-Clear
#include <systemc>
#include <cci_configuration>
#include <argparser.h>
#include <cciutils.h>
#include <module_factory_container.h>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm-extensions/mem-tx-attrs.h>
#include <tlm-extensions/qemu-cpu-hint.h>
#include <async_event.h>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include "qcom/apss/cpu.h"
#include "qcom/apss/execution_group.h"
#include "qcom/apss/gic.h"
#include "qcom/apss/its.h"
#include "qcom/sm88xx/apss_boot/system_timer.h"
#include "qcom/clic/clic.h"
#include "qcom/clic/cpu.h"
#include "qcom/sm88xx/tme_runtime_abi/bridge.h"
#include "qcom/sm88xx/apss_boot/boot_identity.h"
#include "qcom/sm88xx/apss_boot/tcsr_identity.h"
#include "qcom/sm88xx/apss_boot/boot_info.h"
#include "qcom/sm88xx/apss_boot/sleep_counter.h"
#include "qcom/sm88xx/fuse_controller/fuse_controller.h"
#include "qcom/sm88xx/boot_control/apss_boot_scratch.h"
#include "qcom/sm88xx/boot_control/tme_low_power_control.h"
#include "qcom/sm88xx/boot_control/tme_crashdump_policy.h"
#include "qcom/sm88xx/boot_control/pdc.h"
#include "qcom/sm88xx/qsee_fault/fault_monitor.h"
#include "qcom/gpce/qce_v6.h"
#include "shared_memory/shared_executable_memory.h"
#include "qcom/sm88xx/tme_protection/tme_protection.h"
#include "qcom/sm88xx/tme_mailbox/tme_sequencer.h"
#include "qcom/sm88xx/tme_mailbox/watchdog.h"
#include "qcom/sm88xx/tme_mailbox/boot_status.h"
#include "qcom/sm88xx/tme_mailbox/image_handoff_observer.h"
#include "qcom/qcom_mailbox/mailbox.h"
#include "qcom/sm88xx/pdp_scmi/pdp_scmi.h"
#include "qcom/sm88xx/pdp/cpu.h"
#include "qcom/sm88xx/pdp/address_windows.h"
#include "qcom/sm88xx/pdp/registers.h"
#include "qcom/sm88xx/pdp/bsa_watchdog.h"
#include "qcom/sm88xx/pdp/power_controls.h"
#include "qcom/sm88xx/pdp/acd.h"
#include "qcom/sm88xx/pdp/cprh.h"
#include "qcom/sm88xx/pdp/cprh_target.h"
#include "qcom/gsi/gsi.h"
#include "qcom/tlmm/tlmm.h"
#include "qcom/sm88xx/tme_mailbox/software_state.h"
#include "qcom/vmidmt/vmid_mapper.h"
#include "qcom/sm88xx/cpucp/cpucp_boot_scratch.h"
#include "qcom/sm88xx/cpucp/cpucp_pad_control.h"
#include "qcom/sm88xx/cpucp/cpucp_boot_control.h"
#include "qcom/sm88xx/cpucp/subsystem.h"
#include "qcom/sm88xx/cpucp/sequence_configuration.h"
#include "qcom/sm88xx/cpucp/low_power_telemetry.h"
#include "qcom/sm88xx/cpucp/level_request.h"
#include "qcom/sm88xx/cpucp/llm.h"
#include "qcom/sm88xx/cpucp/mailbox.h"
#include "qcom/sm88xx/cpucp/bus_error.h"
#include "qcom/sm88xx/cpucp/acehr.h"
#include "qcom/sm88xx/cpucp/gcc_pll.h"
#include "qcom/sm88xx/cpucp/clock_roots.h"
#include "qcom/sm88xx/cpucp/runtime_clock_root.h"
#include "qcom/sm88xx/cpucp/zonda_pll.h"
#include "qcom/sm88xx/cpucp/pongo_pll.h"
#include "qcom/sm88xx/cpucp/refgen.h"
#include "qcom/sm88xx/cpucp/clock_votes.h"
#include "qcom/sm88xx/cpucp/clock_gating_overrides.h"
#include "qcom/sm88xx/cpucp/amu.h"
#include "qcom/sm88xx/cpucp/mpam_msc.h"
#include "qcom/spmi/spmi.h"
#include "qcom/spmi/pmic.h"
#include "qcom/sm88xx/ddr/virtual_dram.h"
#include "qcom/sm88xx/ddr/shrm.h"
#include "qcom/sm88xx/dcp/cpu.h"
#include "qcom/sm88xx/dcp/interrupt_conditioner.h"
#include "qcom/sm88xx/dcp/dsi_interrupts.h"
#include "qcom/bam/bam.h"
#include "qcom/ipa/ipa.h"
#include "qcom/sm88xx/dcp/bootstrap.h"
#include "qcom/rpmh/rsc.h"
#include "qcom/rpmh/display_rsc.h"
#include "qcom/rpmh/rscc.h"
#include "qcom/rpmh/pcie_rscc.h"
#include "qcom/rpmh/display_pdc.h"
#include "qcom/rpmh/cesta.h"
#include "qcom/rpmh/bcm_status.h"
#include "qcom/rpmh/aop_resource_service.h"
#include "qcom/rpmh/aop_control.h"
#include "qcom/aop/qmp.h"
#include "qcom/sm88xx/boot_control/aoss_reset_fsm.h"
#include "qcom/sm88xx/ddr/controls.h"
#include "qcom/cpr/controller.h"
#include "qcom/cpr/cprf.h"
#include "qcom/tsens/tsens.h"
#include "qcom/soccp/soccp.h"
#include "qcom/soccp/ipcc.h"
#include "qcom/soccp/mutex.h"
#include "qcom/smmu/smmu.h"
#include "qcom/sm88xx/ddr/phy.h"
#include "qcom/sm88xx/ddr/command_engine.h"
#include "qcom/llcc/llcc.h"
#include "qcom/llcc/dare_memory_port.h"
#include "qcom/eud/disconnected_usb_debug.h"
#include "qcom/rng/qrng.h"
#include "qcom/sm88xx/cpucp/clock_branch.h"
#include "qcom/sm88xx/cpucp/cpu_domain_power.h"
#include "qcom/sm88xx/cpucp/mpm_ps_hold.h"
#include "qcom/pmic/virtual_power.h"
#include "qcom/ufs/ufs_controller.h"
#include "qcom/ufs/gdsc.h"
#include "qcom/ufs/clock_reference.h"
#include "qcom/ufs/ufs_logical_unit.h"
#include "qcom/uart/geni_uart.h"
#include "diagnostics/memory_snapshot.h"
#include "diagnostics/observation_clock.h"
#include <arm_gicv3.h>

static gs::ModuleFactory::ModuleRegistrationWrapper memory_snapshot_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,memory-snapshot-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::MemorySnapshot>("qcom,memory-snapshot-v1"),
        "Memory snapshot constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper observation_clock_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "ObservationClock",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::ObservationClock>("ObservationClock"),
        "Observation clock constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper fuse_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,sm8850-fuse-controller",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::FuseController>("qcom,sm8850-fuse-controller"),
        "Fuse controller constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper cpr_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,cpr3-controller-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomCpr3Controller>("qcom,cpr3-controller-v1"),
        "CPR controller constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper cprf_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,rpmh-cprf-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomRpmhCprf>("qcom,rpmh-cprf-v1"),
        "RPMH CPRF constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper timer_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,sm8850-qtimer-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::SystemTimer>("qcom,sm8850-qtimer-v1"),
        "System timer constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper sleep_counter_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,mpm-sleep-counter-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::SleepCounter>("qcom,mpm-sleep-counter-v1"),
        "MPM sleep counter constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper watchdog_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,tme-watchdog-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::TmeWatchdog>("qcom,tme-watchdog-v1"),
        "TME watchdog constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper apss_watchdog_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,kpss-watchdog-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::ApssWatchdog>("qcom,kpss-watchdog-v1"),
        "APSS watchdog constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper clic_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,tme-clic-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::TmeClic>("qcom,tme-clic-v1"),
        "TME CLIC constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper clic_cpu_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,tme-clic-cpu-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::TmeClicCpu, sc_core::sc_object*, uint64_t>(
            "qcom,tme-clic-cpu-v1"),
        "TME CLIC CPU constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper apss_group_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,apss-execution-group-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::ApssExecutionGroup, sc_core::sc_object*>(
            "qcom,apss-execution-group-v1"),
        "APSS execution group constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper apss_gic_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,apss-gic-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::ApssGic, sc_core::sc_object*, sc_core::sc_object*>(
            "qcom,apss-gic-v1"),
        "APSS interrupt controller constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper apss_its_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,apss-its-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::ApssIts, sc_core::sc_object*, sc_core::sc_object*>(
            "qcom,apss-its-v1"),
        "APSS ITS constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper apss_cpu_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,apss-cpu-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::ApssCpu, sc_core::sc_object*, sc_core::sc_object*>(
            "qcom,apss-cpu-v1"),
        "APSS CPU constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper runtime_abi_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,tme-runtime-abi-bridge-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::TmeRuntimeAbiBridge>(
            "qcom,tme-runtime-abi-bridge-v1"),
        "TME runtime ABI bridge constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper scratch_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,apss-boot-scratch-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomApssBootScratch>("qcom,apss-boot-scratch-v1"),
        "APSS scratch constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper tme_low_power_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,tme-low-power-control-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::TmeLowPowerControl, sc_core::sc_object*>(
            "qcom,tme-low-power-control-v1"),
        "TME low-power control constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper identity_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,apss-boot-identity-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomApssBootIdentity>("qcom,apss-boot-identity-v1"),
        "APSS identity constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper boot_info_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,tcsr-boot-info-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomTcsrBootInfo>("qcom,tcsr-boot-info-v1"),
        "TCSR boot information constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper shared_memory_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,shared-executable-memory-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::SharedExecutableMemory, sc_core::sc_object*>(
            "qcom,shared-executable-memory-v1"),
        "Shared executable memory constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper dare_memory_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,llcc-dare-port-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::DareMemoryPort, sc_core::sc_object*, sc_core::sc_object*>(
            "qcom,llcc-dare-port-v1"),
        "DARE memory port constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper protection_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,sm8850-tme-protection",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::TmeProtection>("qcom,sm8850-tme-protection"),
        "TME protection constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper fixed_protection_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,xpu-fixed-resources-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::XpuFixedProtection>("qcom,xpu-fixed-resources-v1"),
        "Fixed-resource protection constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper protection_port_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,tme-protection-port-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::TmeProtectionPort, sc_core::sc_object*, sc_core::sc_object*>(
            "qcom,tme-protection-port-v1"),
        "Protection port constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper sequencer_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,tme-sequencer-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::TmeSequencer>("qcom,tme-sequencer-v1"),
        "TME sequencer constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper cpucp_scratch_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,cpucp-boot-scratch-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomCpucpBootScratch>(
            "qcom,cpucp-boot-scratch-v1"),
        "CPUCP scratch constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper cpucp_pad_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,cpucp-pad-control-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomCpucpPadControl>("qcom,cpucp-pad-control-v1"),
        "CPUCP pad constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper cpucp_boot_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,cpucp-boot-control-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomCpucpBootControl, sc_core::sc_object*>(
            "qcom,cpucp-boot-control-v1"),
        "CPUCP boot constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper cpucp_boot_v2_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,cpucp-boot-control-v2",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomCpucpBootControlV2, sc_core::sc_object*>(
            "qcom,cpucp-boot-control-v2"),
        "CPUCP V2 boot constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper cpucp_subsystem_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,cpucp-subsystem-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomCpucpSubsystem>("qcom,cpucp-subsystem-v1"),
        "CPUCP subsystem constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper cpucp_sequence_configuration_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,cpucp-sequence-configuration-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomSequenceConfiguration>("qcom,cpucp-sequence-configuration-v1"),
        "CPUCP sequencer configuration constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper cpucp_low_power_telemetry_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,cpucp-low-power-telemetry-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomLowPowerTelemetry>("qcom,cpucp-low-power-telemetry-v1"),
        "CPUCP low-power telemetry constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper cpucp_level_request_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,cpucp-level-request-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomLevelRequest>("qcom,cpucp-level-request-v1"),
        "CPUCP level request constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper cpucp_llm_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,cpuss-llm-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomCpussLlm>("qcom,cpuss-llm-v1"),
        "CPUSS limits manager constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper cpucp_mailbox_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,cpucp-mailbox-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomCpucpMailbox>("qcom,cpucp-mailbox-v1"),
        "CPUCP hardware mailbox constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper cpucp_bus_error_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,cpucp-bus-error-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomCpucpBusError>("qcom,cpucp-bus-error-v1"),
        "CPUCP bus-error unit constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper cpucp_acehr_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,cpucp-acehr-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomCpucpAcehr>("qcom,cpucp-acehr-v1"),
        "CPUCP ACEHR constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper gcc_pll_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,gcc-taycan-eko-t-pll-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomGccPll>("qcom,gcc-taycan-eko-t-pll-v1"),
        "GCC Taycan EKO-T PLL constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper zonda_pll_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,ncc-zonda-pll-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomZondaPll>("qcom,ncc-zonda-pll-v1"),
        "NCC Zonda PLL constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper pongo_pll_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,aoss-pongo-pll-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomPongoPll>("qcom,aoss-pongo-pll-v1"),
        "AOSS Pongo PLL constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper refgen_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,refgen-nxgen-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomRefgen>("qcom,refgen-nxgen-v1"),
        "REFGEN NXGEN constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper spmi_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,spmi-arbiter-packed-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomSpmiArbiter>("qcom,spmi-arbiter-packed-v1"),
        "SPMI arbiter constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper pmic_registers_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,virtual-pmic-registers-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomVirtualPmic>("qcom,virtual-pmic-registers-v1"),
        "Virtual PMIC constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper virtual_ddr_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,virtual-ddr-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomVirtualDdr>("qcom,virtual-ddr-v1"),
        "Virtual DDR constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper virtual_shrm_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,virtual-shrm-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomVirtualShrm>("qcom,virtual-shrm-v1"),
        "Virtual SHRM constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper rpmh_rsc_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,rpmh-rsc-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomRpmhRsc>("qcom,rpmh-rsc-v1"),
        "RPMh RSC constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper display_rsc_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,display-rsc-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomDisplayRsc>("qcom,display-rsc-v1"),
        "Display RSC constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper titan_rscc_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,titan-rscc-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomTitanRscc>("qcom,titan-rscc-v1"),
        "TITAN camera RSCC constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper pcie_rscc_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,pcie-rscc-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomPcieRscc>("qcom,pcie-rscc-v1"),
        "PCIe RSCC constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper display_pdc_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,display-pdc-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomDisplayPdc>("qcom,display-pdc-v1"),
        "Display PDC constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper cesta_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,cesta-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomCesta>("qcom,cesta-v1"),
        "CESTA constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper bcm_status_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,bcm-status-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomBcmStatus>("qcom,bcm-status-v1"),
        "BCM status constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper apss_pdc_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,apss-pdc-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomApssPdc>("qcom,apss-pdc-v1"),
        "APSS PDC constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper fault_monitor_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,fault-monitor-setup-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomFaultMonitor>("qcom,fault-monitor-setup-v1"),
        "Fault monitor configuration constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper qce_v6_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,qce-v6",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QceV6>("qcom,qce-v6"),
        "QCE version six constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper aop_resource_service_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,aop-resource-service-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomAopResourceService>("qcom,aop-resource-service-v1"),
        "AOP resource service constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper aop_control_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,aop-control-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomAopControl>("qcom,aop-control-v1"),
        "AOP transfer control constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper aop_qmp_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,aop-qmp-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomAopQmp>("qcom,aop-qmp-v1"),
        "AOP QMP endpoint constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper request_ack_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,request-ack-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomRequestAck>("qcom,request-ack-v1"),
        "Request acknowledgment constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper halt_channel_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,halt-channel-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomHaltChannel>("qcom,halt-channel-v1"),
        "Clock halt channel constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper aoss_reset_fsm_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,aoss-reset-fsm-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomAossResetFsm>("qcom,aoss-reset-fsm-v1"),
        "AOSS reset controller constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper ddr_frontend_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,ddr-frontend-program-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomDdrFrontendProgram>("qcom,ddr-frontend-program-v1"),
        "DDR frontend constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper tsens_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,tsens-v2",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomTsensV2>("qcom,tsens-v2"),
        "TSENS constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper tsens_calibration_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,tsens-calibration-profile-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomTsensCalibrationProfile>("qcom,tsens-calibration-profile-v1"),
        "TSENS calibration constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper dcp_cpu_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,dcp-rv32-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::DcpCpu, sc_core::sc_object*>("qcom,dcp-rv32-v1"),
        "DCP CPU constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper dcp_interrupt_conditioning_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,dcp-interrupt-conditioning-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::DcpInterruptConditioner>(
            "qcom,dcp-interrupt-conditioning-v1"),
        "DCP interrupt conditioning constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper dsi_interrupts_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,dsi-interrupts-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomDsiInterrupts>("qcom,dsi-interrupts-v1"),
        "Disconnected DSI interrupts constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper bam_v2_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,bam-v2.0.0",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomBamV2>("qcom,bam-v2.0.0"),
        "BAM v2.0 register file constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper ipa_v5_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,ipa-v5.5",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomIpaV5>("qcom,ipa-v5.5"),
        "IPA v5.5 secure register constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper dcp_bootstrap_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,dcp-bootstrap-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomDcpBootstrap, sc_core::sc_object*>("qcom,dcp-bootstrap-v1"),
        "DCP bootstrap constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper soccp_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,soccp-virtual-peer-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomSoccp>("qcom,soccp-virtual-peer-v1"),
        "SOCCP virtual peer constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper soccp_v2_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,soccp-virtual-peer-v2",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomSoccpV2>("qcom,soccp-virtual-peer-v2"),
        "SOCCP extended-register virtual peer constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper ipcc_local_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,ipcc-local-v3",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomIpccLocal>("qcom,ipcc-local-v3"),
        "IPCC local bank constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper smem_mutex_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,smem-mutex-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomSmemMutex>("qcom,smem-mutex-v1"),
        "SMEM mutex constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper smmu_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "arm,smmu-v2-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::ArmSmmuV2>("arm,smmu-v2-v1"),
        "SMMUv2 constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper smmu_stream_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "arm,smmu-stream-port-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::ArmSmmuStreamPort, sc_core::sc_object*>(
            "arm,smmu-stream-port-v1"),
        "SMMUv2 stream port constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper ddr_phy_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,ddr-phy-program-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomDdrPhyProgram>("qcom,ddr-phy-program-v1"),
        "DDR PHY program constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper ddr_command_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,ddr-command-engine-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomDdrCommandEngine>("qcom,ddr-command-engine-v1"),
        "DDR command engine constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper llcc_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,llcc-v6",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomLlcc>("qcom,llcc-v6"),
        "LLCC constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper eud_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,disconnected-usb-debug-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomDisconnectedUsbDebug>("qcom,disconnected-usb-debug-v1"),
        "Disconnected USB debug constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper clock_vote_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,clock-vote-bank-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomClockVoteBank>("qcom,clock-vote-bank-v1"),
        "Clock vote bank constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper retained_clock_control_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,retained-boot-control-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomRetainedBootControl>("qcom,retained-boot-control-v1"),
        "Retained boot control constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper clock_gating_overrides_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,clock-gating-overrides-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomClockGatingOverrides>(
            "qcom,clock-gating-overrides-v1"),
        "Clock-gating overrides constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper core_amu_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "arm,amu-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomCoreAmu>("arm,amu-v1"),
        "Activity monitor constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper ncc_mpam_msc_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,ncc-mpam-msc-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomNccMpamMsc>("qcom,ncc-mpam-msc-v1"),
        "NCC MPAM component constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper rcg_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,rcg-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomClockRoot>("qcom,rcg-v1"),
        "Clock root constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper runtime_rcg_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,cpucp-runtime-rcg-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomRuntimeClockRoot>("qcom,cpucp-runtime-rcg-v1"),
        "CPUCP runtime clock constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper dfs_rcg_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,rcg-dfs-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomDfsClockRoot>("qcom,rcg-dfs-v1"),
        "DFS clock root constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper clock_divider_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,clock-integer-divider-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomClockDivider>("qcom,clock-integer-divider-v1"),
        "Clock divider constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper clock_branches_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,clock-branch-enables-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomClockBranchEnables>("qcom,clock-branch-enables-v1"),
        "Clock branch constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper rng_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,qrng-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomRng>("qcom,qrng-v1"),
        "Random generator constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper clock_branch_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,clock-branch-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomClockBranch>("qcom,clock-branch-v1"),
        "Clock branch constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper reference_clock_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,virtual-always-on-clock-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::VirtualAlwaysOnClock>("qcom,virtual-always-on-clock-v1"),
        "Virtual reference clock constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper symbol_clock_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,symbol-clock-mux-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomSymbolClockMux>("qcom,symbol-clock-mux-v1"),
        "Symbol clock constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper geni_common_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,geni-common-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::GeniCommon>("qcom,geni-common-v1"),
        "GENI common constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper geni_se_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,geni-se-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::GeniSe>("qcom,geni-se-v1"),
        "GENI serial engine constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper gsi_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,gsi-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::GsiController>("qcom,gsi-v1"),
        "GSI constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper tlmm_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,tlmm-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomTlmm>("qcom,tlmm-v1"),
        "TLMM constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper uart_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,geni-uart-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::GeniUart>("qcom,geni-uart-v1"),
        "GENI UART constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper vmid_mapper_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,vmid-mapper-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::VmidMapper>("qcom,vmid-mapper-v1"),
        "VMID mapper constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper reference_gate_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,tcsr-clock-reference-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomTcsrClockReference>("qcom,tcsr-clock-reference-v1"),
        "TCSR reference clock constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper tcsr_identity_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,tcsr-soc-identity-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomTcsrIdentity>("qcom,tcsr-soc-identity-v1"),
        "TCSR identity constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper tme_crashdump_policy_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,tme-crashdump-policy-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::TmeCrashdumpPolicy>(
            "qcom,tme-crashdump-policy-v1"),
        "TME crash-dump policy constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper tme_status_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,tme-boot-status-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::TmeBootStatus>("qcom,tme-boot-status-v1"),
        "TME boot status constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper image_handoff_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,authenticated-image-handoff-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::AuthenticatedImageHandoff>(
            "qcom,authenticated-image-handoff-v1"),
        "Authenticated image handoff constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper mailbox_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,mailbox-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomMailbox>("qcom,mailbox-v1"),
        "Mailbox constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper mailbox_rx_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,mailbox-rx-bank-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomMailboxRxBank>("qcom,mailbox-rx-bank-v1"),
        "Mailbox RX constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper pdp_scmi_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,pdp-scmi-virtual-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomPdpScmi>("qcom,pdp-scmi-virtual-v1"),
        "PDP SCMI constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper pdp_cpu_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,pdp-cpu-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::PdpCpu, sc_core::sc_object*>("qcom,pdp-cpu-v1"),
        "PDP CPU constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper pdp_windows_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,pdp-address-windows-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::PdpAddressWindows>("qcom,pdp-address-windows-v1"),
        "PDP address window constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper ncc_timer_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,ncc-timer-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::NccTimer>("qcom,ncc-timer-v1"),
        "NCC timer constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper pdp_dpm_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,pdp-dpm-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::PdpDpm>("qcom,pdp-dpm-v1"),
        "PDP DPM constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper pdp_core_dpm_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,pdp-core-dpm-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::PdpCoreDpm>("qcom,pdp-core-dpm-v1"),
        "PDP per-core DPM constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper pdp_limiter_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,pdp-limiter-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::PdpLimiters>("qcom,pdp-limiter-v1"),
        "PDP limiter constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper pdp_acd_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,ncc-acd-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::PdpAcd>("qcom,ncc-acd-v1"),
        "NCC adaptive clock distribution constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper pdp_cprh_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,pdp-cprh-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::PdpCprh>("qcom,pdp-cprh-v1"),
        "PDP CPRH configuration constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper cprh_target_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,cpuss-cprh-target-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::CprhTarget>("qcom,cpuss-cprh-target-v1"),
        "CPRH target configuration constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper bsa_watchdog_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "arm,bsa-generic-watchdog-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::BsaWatchdog>("arm,bsa-generic-watchdog-v1"),
        "BSA watchdog constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper tme_software_state_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,sm8850-tme-software-state-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::TmeSoftwareState>(
            "qcom,sm8850-tme-software-state-v1"),
        "TME software state constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper cpu_domain_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,cpu-domain-power-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomCpuDomainPower>("qcom,cpu-domain-power-v1"),
        "CPU domain constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper gdsc_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,gdsc-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomGdsc>("qcom,gdsc-v1"),
        "Power domain constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper power_hold_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,mpm-ps-hold-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::MpmPsHold>("qcom,mpm-ps-hold-v1"),
        "Power hold constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper virtual_power_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,virtual-pmic-power-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::VirtualPower>("qcom,virtual-pmic-power-v1"),
        "Virtual power constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper ufs_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,ufs-hci-v1",
        gs::cci_constructor_vl::FactoryMaker<UfsController, sc_core::sc_object*>("qcom,ufs-hci-v1"),
        "UFS controller constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper ufs_lun_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,ufs-lu-v1",
        gs::cci_constructor_vl::FactoryMaker<UfsLogicalUnit, sc_core::sc_object*, sc_core::sc_object*>(
            "qcom,ufs-lu-v1"),
        "UFS logical unit constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper gic_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "arm_gicv3",
        gs::cci_constructor_vl::FactoryMaker<arm_gicv3, sc_core::sc_object*>("arm_gicv3"),
        "GIC constructor", cci::CCI_ABSOLUTE_NAME);
});

class AccessMonitor : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<AccessMonitor> target_socket{"target_socket"};
    tlm_utils::simple_initiator_socket<AccessMonitor> initiator_socket{"initiator_socket"};
    static bool faulted;
    cci::cci_param<bool> allow_unspecified{"allow_unspecified", false, "Accept unspecified attributes in one address space"};
    cci::cci_param<uint64_t> trace_base{"trace_base", 0, "First address of an optional diagnostic access trace"};
    cci::cci_param<uint64_t> trace_size{"trace_size", 0, "Size of the diagnostic access trace window; zero disables it"};
    cci::cci_param<uint64_t> trace_pc_base{"trace_pc_base", 0, "First PC of the optional trace filter"};
    cci::cci_param<uint64_t> trace_pc_size{"trace_pc_size", 0, "Size of the trace PC filter; zero traces every PC"};
    AccessMonitor(sc_core::sc_module_name n) : sc_module(n) {
        target_socket.register_b_transport(this, &AccessMonitor::transport);
        target_socket.register_transport_dbg(this, &AccessMonitor::debug);
        target_socket.register_get_direct_mem_ptr(this, &AccessMonitor::dmi);
        initiator_socket.register_invalidate_direct_mem_ptr(this, &AccessMonitor::invalidate);
    }
private:
    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        const auto* ext = tx.get_extension<QemuMemTxAttrsExtension>();
        if (!ext || (ext->attrs.unspecified && !allow_unspecified.get_value()) || ext->attrs.space > 1 || ext->attrs.address_type != 0 ||
            ext->attrs.pid != 0 || ext->attrs.requester_id != 0) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        } else {
            initiator_socket->b_transport(tx, delay);
        }
        const auto address = tx.get_address();
        if (trace_size.get_value() && address >= trace_base.get_value() &&
            address - trace_base.get_value() < trace_size.get_value() && !tx.is_response_error()) {
            const auto* cpu = tx.get_extension<QemuCpuHintTlmExtension>();
            const auto pc = cpu ? cpu->get_cpu().get_pc() : 0;
            if (!trace_pc_size.get_value() ||
                (cpu && pc >= trace_pc_base.get_value() && pc - trace_pc_base.get_value() < trace_pc_size.get_value())) {
                uint64_t value = 0;
                const auto* data = tx.get_data_ptr();
                for (unsigned i = 0; data && i < tx.get_data_length() && i < 8; ++i)
                    value |= uint64_t{data[i]} << (8 * i);
                std::cerr << "ACCESS_TRACE core=" << name() << std::hex;
                if (cpu) std::cerr << " pc=0x" << pc;
                std::cerr << " address=0x" << address << " operation=" << (tx.is_write() ? "write" : "read")
                          << " value=0x" << value << std::dec << " width=" << tx.get_data_length() << '\n';
            }
        }
        if (tx.is_response_error()) {
            faulted = true;
            const auto* cpu = tx.get_extension<QemuCpuHintTlmExtension>();
            std::cerr << "UNMODELED_ACCESS core=" << name();
            if (cpu) std::cerr << " pc=0x" << std::hex << cpu->get_cpu().get_pc();
            else std::cerr << " pc=unavailable";
            std::cerr << " address=0x" << std::hex << tx.get_address() << std::dec
                      << " width=" << tx.get_data_length()
                      << " operation=" << (tx.is_write() ? "write" : "read");
            // A rejected write keeps its value, so the next model change can name the field.
            if (tx.is_write() && tx.get_data_ptr() && tx.get_data_length() <= 8) {
                uint64_t value = 0;
                for (unsigned i = 0; i < tx.get_data_length(); ++i)
                    value |= uint64_t{tx.get_data_ptr()[i]} << (8 * i);
                std::cerr << " value=0x" << std::hex << value << std::dec;
            }
            std::cerr << " status=" << tx.get_response_string() << '\n';
            sc_core::sc_stop();
        }
    }
    unsigned debug(tlm::tlm_generic_payload& tx) { return initiator_socket->transport_dbg(tx); }
    bool dmi(tlm::tlm_generic_payload& tx, tlm::tlm_dmi& dmi) {
        return initiator_socket->get_direct_mem_ptr(tx, dmi);
    }
    void invalidate(sc_dt::uint64 begin, sc_dt::uint64 end) {
        target_socket->invalidate_direct_mem_ptr(begin, end);
    }
};
bool AccessMonitor::faulted = false;
GSC_MODULE_REGISTER(AccessMonitor);


int sc_main(int argc, char** argv) {
    try {
        scp::LoggingGuard logging_guard(scp::LogConfig().logAsync(false).logLevel(scp::log::WARNING));
        gs::ConfigurableBroker broker;
        cci::cci_originator origin("sc_main");
        auto handle = broker.create_broker_handle(origin);
        ArgParser args(handle, argc, argv);
        // CPUCP allows one millisecond for an already-completed DMA request.
        // Keep synchronization finer than that observable deadline.
        tlm_utils::tlm_quantumkeeper::set_global_quantum(sc_core::sc_time(100000, sc_core::SC_NS));
        class PlatformKeepAlive : public sc_core::sc_module
        {
        public:
            gs::async_event event;

            explicit PlatformKeepAlive(const sc_core::sc_module_name& name): sc_core::sc_module(name)
            {
                event.async_attach_suspending();
            }
        };

        PlatformKeepAlive keep_alive("keep_alive");
        gs::ModuleFactory::Container platform("platform");
        sc_core::sc_start();
        std::cerr << (AccessMonitor::faulted ? "RUN_FAILED reason=unmodeled-access\n" :
            "RUN_FAILED reason=missing-kernel-handoff\n");
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "RUN_FAILED reason=simulation-error detail=" << error.what() << '\n';
        return 2;
    } catch (...) {
        std::cerr << "RUN_FAILED reason=unknown-simulation-error\n";
        return 2;
    }
}
