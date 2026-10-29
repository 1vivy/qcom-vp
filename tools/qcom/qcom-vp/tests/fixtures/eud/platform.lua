-- SPDX-License-Identifier: BSD-3-Clause-Clear
local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
platform={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'},
    apss_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','AARCH64'},accel='tcg',
        sync_policy='multithread-quantum',qemu_args='-d in_asm -D '..root..'producer.log'},
    apss_router={moduletype='router'},
    apss_access={moduletype='AccessMonitor',initiator_socket={bind='&apss_router.target_socket'}},
    apss={moduletype='cpu_arm_cortexA53',args={'&apss_instance'},rvbar=0x300000,
        has_el3=true,has_el2=true,mem={bind='&apss_access.target_socket'}},
    rom={moduletype='gs_memory',read_only=true,
        target_socket={address=0x300000,size=0x2000,bind='&apss_router.initiator_socket'},
        load={{bin_file=root..'producer.bin',offset=0}}},
    eud={moduletype='qcom,disconnected-usb-debug-v1',disconnected=true,
        enable_socket={address=0x88e2000,size=4,bind='&apss_router.initiator_socket'},
        enumeration_socket={address=0x88e0240,size=4,bind='&apss_router.initiator_socket'},
        attach_socket={address=0x88e1020,size=4,bind='&apss_router.initiator_socket'},
        controller_socket={address=0xa60c704,size=4,bind='&apss_router.initiator_socket'},
        detect_socket={address=0xc278000,size=4,bind='&apss_router.initiator_socket'},
        power_socket={address=0x15000000,size=4,bind='&apss_router.initiator_socket'}}}
