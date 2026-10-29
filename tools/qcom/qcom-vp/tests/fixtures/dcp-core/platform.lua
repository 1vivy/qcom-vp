-- SPDX-License-Identifier: BSD-3-Clause-Clear
local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
local selection=assert(io.open(root..'case.txt','r'))
local case=assert(tonumber(selection:read('*l')))
selection:close()
local p={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'},
    apss_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','AARCH64'},accel='tcg',
        sync_policy='multithread-quantum'},
    dcp_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','RISCV32'},accel='tcg',
        sync_policy='multithread-quantum',qemu_args='-d in_asm -D '..root..'producer.log'},
    apss_router={moduletype='router'},
    apss_access={moduletype='AccessMonitor',initiator_socket={bind='&apss_router.target_socket'}},
    dcp_access={moduletype='AccessMonitor',allow_unspecified=true,
        initiator_socket={bind='&apss_router.target_socket'}},
    apss={moduletype='cpu_arm_cortexA53',args={'&apss_instance'},rvbar=0x300000,
        has_el3=true,mem={bind='&apss_access.target_socket'}},
    dcp={moduletype='qcom,dcp-rv32-v1',args={'&dcp_instance'},pmp=true,mmu=false,
        cache_operations=case~=1,mem={bind='&dcp_access.target_socket'}},
    controller_rom={moduletype='gs_memory',read_only=true,
        target_socket={address=0x300000,size=0x1000,bind='&apss_router.initiator_socket'},
        load={{bin_file=root..'controller.bin',offset=0}}},
    firmware_rom={moduletype='gs_memory',read_only=true,
        target_socket={address=0xa4100000,size=0x2000,bind='&apss_router.initiator_socket'},
        load={{bin_file=root..'producer.bin',offset=0}}},
    data={moduletype='gs_memory',init_mem=true,
        target_socket={address=0xa4200000,size=0x1000,bind='&apss_router.initiator_socket'}},
    xbl_boot_clock_reference={moduletype='qcom,virtual-always-on-clock-v1'}}
local directory=assert(os.getenv('QCOM_VP_PLATFORM_DIR'))..'/'
dofile(directory..'qcom/sm88xx/dcp-bootstrap.lua')(p)
platform=p
