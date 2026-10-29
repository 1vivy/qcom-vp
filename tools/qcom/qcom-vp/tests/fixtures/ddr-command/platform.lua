-- SPDX-License-Identifier: BSD-3-Clause-Clear
local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
local platforms=assert(os.getenv('QCOM_VP_PLATFORM_DIR'))..'/'
local policy=dofile(root..'policy.lua')
local p={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'},
    apss_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','AARCH64'},accel='tcg',
        sync_policy='multithread-quantum',qemu_args='-d in_asm -D '..root..'producer.log'},
    apss_router={moduletype='router'},
    apss_access={moduletype='AccessMonitor',initiator_socket={bind='&apss_router.target_socket'}},
    apss={moduletype='cpu_arm_cortexA53',args={'&apss_instance'},rvbar=0x300000,
        has_el3=true,has_el2=true,mem={bind='&apss_access.target_socket'}},
    rom={moduletype='gs_memory',read_only=true,
        target_socket={address=0x300000,size=0x4000,bind='&apss_router.initiator_socket'},
        load={{bin_file=root..'producer.bin',offset=0}}},
    dram_low={moduletype='gs_memory',
        target_socket={address=0x80000000,size=0x200,bind='&apss_router.initiator_socket'}},
    dram_high={moduletype='gs_memory',
        target_socket={address=0x90000000,size=0x4000,bind='&apss_router.initiator_socket'}},
    shrm_sram={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x31048000,size=0x28000,bind='&apss_router.initiator_socket'}},
    publication_fence={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x310a8000,size=0x1000,bind='&apss_router.initiator_socket'}},
    byte_lane={moduletype='qcom,ddr-phy-program-v1',configuration_offsets='0x10',
        retained_extent=0x10000,
        target_socket={address=0x31bc0000,size=0x10000,bind='&apss_router.initiator_socket'}},
    shrm={moduletype='qcom,virtual-shrm-v1',backend_path='virtual_ddr',
        target_socket={address=0x31076000,size=0x70,bind='&apss_router.initiator_socket'},
        processor_control_socket={address=0x31079000,size=4,bind='&apss_router.initiator_socket'}},
}
p.virtual_ddr={moduletype='qcom,virtual-ddr-v1',shared_base=0x3104a800,
    dram_base=0x80000000,rank1_mib=policy.rank1_mib,
    dram_lower_size=0x200,dram_upper_base=0x90000000,
    memory_socket={bind='&apss_router.target_socket'}}
-- A real frontend owner receives accesses outside the command registers.
p.ddr_phy_llc={moduletype='qcom,ddr-phy-program-v1',configuration_offsets='0x80',
    pxi_frontend=true,pxi_backend_path='virtual_ddr',
    command_offsets='0x5020,0x6020',
    target_socket={address=0x34a00000,size=0x10000,bind='&apss_router.initiator_socket'}}
p.ddr_phy_mc={moduletype='qcom,ddr-phy-program-v1',configuration_offsets='0x80',
    target_socket={address=0x34b80000,size=0x10000,bind='&apss_router.initiator_socket'}}
dofile(platforms..'qcom/sm88xx/ddr-mc-banks.lua')(p)
platform=dofile(platforms..'qcom/sm88xx/ddr-command.lua')(p)
