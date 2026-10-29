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
        target_socket={address=0x300000,size=0x10000,bind='&apss_router.initiator_socket'},
        load={{bin_file=root..'producer.bin',offset=0}}},
    -- DRV2 names requester 6 explicitly. Isolation must not depend on the bank index.
    rsc={moduletype='qcom,rpmh-rsc-v1',banks='0 4 2;2 6 3 6',
        resources='0x30000,0x30010',sequencer_words=64,
        target_socket={address=0x18900000,size=0x30000,bind='&apss_router.initiator_socket'}},
    tme_rsc={moduletype='qcom,rpmh-rsc-v1',banks='0 6 3',requester_base=3,
        resources='',resource_owner_path='rsc',sequencer_words=0,
        target_socket={address=0x02210000,size=0x10000,bind='&apss_router.initiator_socket'}}}
