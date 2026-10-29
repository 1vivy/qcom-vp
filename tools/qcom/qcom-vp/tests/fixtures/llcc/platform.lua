-- SPDX-License-Identifier: BSD-3-Clause-Clear
local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
local platforms=assert(os.getenv('QCOM_VP_PLATFORM_DIR'))..'/'
local p={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'},
    apss_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','AARCH64'},accel='tcg',
        sync_policy='multithread-quantum',qemu_args='-d in_asm -D '..root..'producer.log'},
    apss_router={moduletype='router'},
    cpu_router={moduletype='router'},
    apss_access={moduletype='AccessMonitor',
        target_socket={address=0,size=0x1000000000,relative_addresses=false,
            bind='&cpu_router.initiator_socket'},
        initiator_socket={bind='&apss_router.target_socket'}},
    -- Expected rejection probes reach the guest abort handler through this alias.
    -- All other accesses retain the monitor and its terminal-fault protocol.
    rejection_probe={moduletype='addrtr',mapped_base_addr=0,
        target_socket={address=0x1000000000,size=0x40000000,relative_addresses=false,
            bind='&cpu_router.initiator_socket'},
        initiator_socket={bind='&apss_router.target_socket'}},
    apss={moduletype='cpu_arm_cortexA53',args={'&apss_instance'},rvbar=0x300000,
        has_el3=true,has_el2=true,mem={bind='&cpu_router.target_socket'}},
    rom={moduletype='gs_memory',read_only=true,
        target_socket={address=0x300000,size=0x10000,bind='&apss_router.initiator_socket'},
        load={{bin_file=root..'producer.bin',offset=0}}},
    dram={moduletype='qcom,shared-executable-memory-v1',args={'&apss'},init_mem=true,
        target_socket={address=0x80000000,size=0x100000}},
    dare_port={moduletype='qcom,llcc-dare-port-v1',args={'&llcc','&apss'},
        physical_base=0x80000000,memory_size=0x100000,
        target_socket={address=0x80000000,size=0x100000,bind='&apss_router.initiator_socket'},
        initiator_socket={bind='&dram.target_socket'}},
}
platform=dofile(platforms..'qcom/sm88xx/llcc.lua')(p,'sm8850')
platform.llcc.memory_socket={bind='&apss_router.target_socket'}
