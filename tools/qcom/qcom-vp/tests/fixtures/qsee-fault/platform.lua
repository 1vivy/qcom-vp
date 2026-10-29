-- SPDX-License-Identifier: BSD-3-Clause-Clear
local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
local directory=assert(os.getenv('QCOM_VP_PLATFORM_DIR'))..'/'
local file=assert(io.open(root..'oem.txt','r'))
local soc=assert(file:read('*l'))
file:close()
local p={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'},
    apss_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','AARCH64'},accel='tcg',
        sync_policy='multithread-quantum',qemu_args='-d in_asm -D '..root..'producer.log'},
    apss_router={moduletype='router'},cpucp_router={moduletype='router'},
    cpucp_clock_router={moduletype='router'},
    clock_probe={moduletype='addrtr',mapped_base_addr=0,
        target_socket={address=0x10000000,size=0x200000,relative_addresses=false,
            bind='&apss_router.initiator_socket'},
        initiator_socket={bind='&cpucp_router.target_socket'}},
    apss_access={moduletype='AccessMonitor',initiator_socket={bind='&apss_router.target_socket'}},
    apss={moduletype='cpu_arm_cortexA53',args={'&apss_instance'},rvbar=0x300000,
        has_el3=true,has_el2=true,mem={bind='&apss_access.target_socket'}},
    rom={moduletype='gs_memory',read_only=true,
        target_socket={address=0x300000,size=0x4000,bind='&apss_router.initiator_socket'},
        load={{bin_file=root..'producer.bin',offset=0}}},
    dram={moduletype='gs_memory',
        target_socket={address=0x80000000,size=0x4000,bind='&apss_router.initiator_socket'}},
    publication={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x31048000,size=0x28000,bind='&apss_router.initiator_socket'}},
    virtual_ddr={moduletype='qcom,virtual-ddr-v1',shared_base=0x3104a800,
        dram_base=0x80000000,rank1_mib=0,dram_lower_size=0x4000,
        memory_socket={bind='&apss_router.target_socket'}},
    ddr_phy_llc={moduletype='qcom,ddr-phy-program-v1',configuration_offsets='0x80',
        command_offsets='0x5020,0x6020',
        target_socket={address=0x34a00000,size=0x10000,bind='&apss_router.initiator_socket'}},
}
dofile(directory..'qcom/sm88xx/ddr-command.lua')(p)
dofile(directory..'qcom/sm88xx/llcc.lua')(p,soc)
dofile(directory..'qcom/sm88xx/gem-noc.lua')(p)
platform=dofile(directory..'qcom/sm88xx/qsee-fault-monitors.lua')(p,soc)
