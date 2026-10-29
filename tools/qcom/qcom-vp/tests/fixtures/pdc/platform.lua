-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- The fixture declares one capture slot, so slot zero is advertised and
-- configurable. The declaration attaches no event producer.
local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
platform={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'},
    apss_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','AARCH64'},accel='tcg',
        sync_policy='multithread-quantum',qemu_args='-d in_asm -D '..root..'producer.log'},
    apss_router={moduletype='router'},
    apss_access={moduletype='AccessMonitor',initiator_socket={bind='&apss_router.target_socket'}},
    apss={moduletype='cpu_arm_cortexA53',args={'&apss_instance'},rvbar=0x300000,
        has_el3=true,has_el2=true,mem={bind='&apss_access.target_socket'}},
    rom={moduletype='gs_memory',read_only=true,
        target_socket={address=0x300000,size=0x1000,bind='&apss_router.initiator_socket'},
        load={{bin_file=root..'producer.bin',offset=0}}},
    pdc={moduletype='qcom,apss-pdc-v1',capture_slots=1,
        target_socket={address=0x0b200000,size=0x40000,bind='&apss_router.initiator_socket'}}}
