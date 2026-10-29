local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
local oem_file=assert(io.open(root..'oem.txt','r'))
local oem=assert(oem_file:read('*l'))
oem_file:close()
local directory=assert(os.getenv('QCOM_VP_PLATFORM_DIR'))..'/'
local p={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'},
    apss_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','AARCH64'},accel='tcg',
        sync_policy='multithread-quantum',qemu_args='-d in_asm -D '..root..'producer.log'},
    dcp_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','RISCV32'},accel='tcg',
        sync_policy='multithread-quantum'},
    dcp={moduletype='qcom,dcp-rv32-v1',args={'&dcp_instance'},pmp=true,
        mem={bind='&cpucp_router.target_socket'}},
    apss_router={moduletype='router'},cpucp_router={moduletype='router'},
    cpucp_clock_router={moduletype='router'},
    apss_access={moduletype='AccessMonitor',initiator_socket={bind='&apss_router.target_socket'}},
    apss={moduletype='cpu_arm_cortexA53',args={'&apss_instance'},rvbar=0x300000,
        has_el3=true,has_el2=true,mem={bind='&apss_access.target_socket'}},
    rom={moduletype='gs_memory',read_only=true,
        target_socket={address=0x300000,size=0x1000,bind='&apss_router.initiator_socket'},
        load={{bin_file=root..'producer.bin',offset=0}}},
    xbl_boot_clock_reference={moduletype='qcom,virtual-always-on-clock-v1'},
    gcc_pll_router={moduletype='router'},
    gcc_plls={moduletype='qcom,gcc-taycan-eko-t-pll-v1',reference_hz=19200000,pll_count=12,vote_count=2,
        target_socket={address=0x100000,size=0xb038,bind='&gcc_pll_router.initiator_socket'},
        vote_socket={address=0x152020,size=0x5004,bind='&gcc_pll_router.initiator_socket'}},
}
dofile(directory..'qcom/sm88xx/xbl-power-domains.lua')(p,
    oem=='sm8845' and {0x0958e334,0x03d99080,0x03d68024} or nil)
dofile(directory..'qcom/sm88xx/dcp-bootstrap.lua')(p)
dofile(directory..'qcom/sm88xx/qsee-clock-dependencies.lua')(p,oem)
p.qsee_clock_probe_alias={moduletype='addrtr',mapped_base_addr=0x00185000,
    target_socket={address=0x40000000,size=4,relative_addresses=false,
        bind='&apss_router.initiator_socket'},
    initiator_socket={bind='&cpucp_router.target_socket'}}
platform=p
