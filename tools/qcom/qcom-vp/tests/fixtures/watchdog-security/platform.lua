-- SPDX-License-Identifier: BSD-3-Clause-Clear
local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
platform={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'},
    apss_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','AARCH64'},accel='tcg',
        sync_policy='multithread-quantum'},
    apss_group={moduletype='qcom,apss-execution-group-v1',args={'&apss_instance'},cpu_count=1},
    apss_router={moduletype='router'},
    apss_access={moduletype='AccessMonitor',initiator_socket={bind='&apss_router.target_socket'}},
    apss={moduletype='qcom,apss-cpu-v1',args={'&apss_instance','&apss_group'},rvbar=0x300000,
        has_el3=true,has_el2=true,mem={bind='&apss_access.target_socket'}},
    rom={moduletype='gs_memory',read_only=true,
        target_socket={address=0x300000,size=0x10000,bind='&apss_router.initiator_socket'},
        load={{bin_file=root..'producer.bin',offset=0}}},
    watchdog={moduletype='qcom,kpss-watchdog-v1',clock_hz=32768,
        security_socket={address=0x17600000,size=4,bind='&apss_router.initiator_socket'},
        target_socket={address=0x17600004,size=0x24,bind='&apss_router.initiator_socket'}}}
