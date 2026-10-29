-- SPDX-License-Identifier: BSD-3-Clause-Clear
local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
local directory=assert(os.getenv('QCOM_VP_PLATFORM_DIR'))..'/'
platform={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'},
    apss_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','AARCH64'},accel='tcg',
        sync_policy='multithread-quantum'},
    apss_router={moduletype='router'},
    apss_access={moduletype='AccessMonitor',initiator_socket={bind='&apss_router.target_socket'}},
    apss={moduletype='cpu_arm_cortexA53',args={'&apss_instance'},rvbar=0x300000,
        has_el3=true,has_el2=true,mem={bind='&apss_access.target_socket'}},
    rom={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x300000,size=0x20000,bind='&apss_router.initiator_socket'},
        load={{bin_file=root..'producer.bin',offset=0}}},
    stack={moduletype='gs_memory',
        target_socket={address=0x14000000,size=0x10000,bind='&apss_router.initiator_socket'}},
    payload={moduletype='gs_memory',dmi_allow=false,
        target_socket={address=0x400000,size=0x5c000,bind='&apss_router.initiator_socket'}},
    ddr={moduletype='gs_memory',dmi_allow=false,
        target_socket={address=0x81f08000,size=0x1000,bind='&apss_router.initiator_socket'}},
    cpucp_slot={moduletype='gs_memory',dmi_allow=false,
        target_socket={address=0x18b4e200,size=0x80,bind='&apss_router.initiator_socket'}},
    tme_mailbox={moduletype='gs_memory',dmi_allow=false,
        target_socket={address=0,size=0x3800}},
    tme_image_admission={moduletype='qcom,authenticated-image-handoff-v1',
        current_image_context_address=0x400000,
        target_socket={address=0x02000000,size=0x3800,bind='&apss_router.initiator_socket'},
        backing_socket={bind='&tme_mailbox.target_socket'},
        memory_socket={bind='&apss_router.target_socket'}}}
dofile(directory..'qcom/apss-gic.lua')(platform,
    {cpu='apss',instance='apss_instance',router='apss_router'})
dofile(directory..'qcom/sm88xx/pdp-scmi.lua')(platform,
    {router='apss_router',observer_path='tme_image_admission',gic='apss_gic'})
