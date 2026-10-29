-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- RPMB persistence example. Inputs, scratch logs, and mutable device state
-- use the experiment launcher's separate roots.
local env_root=assert(os.getenv('QCOM_VP_ENV_ROOT'))..'/'
local state_root=assert(os.getenv('QCOM_VP_STATE_ROOT'))..'/'
local run_root=assert(os.getenv('QCOM_VP_RUN_ROOT'))..'/'
local directory=env_root..'platforms/'
local profile=dofile(env_root..'profile.lua')
platform={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'},
    apss_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','AARCH64'},accel='tcg',
        sync_policy='multithread-quantum',qemu_args='-d in_asm -D '..run_root..'producer.log'},
    apss_router={moduletype='router'},dma_router={moduletype='router'},
    apss_access={moduletype='AccessMonitor',initiator_socket={bind='&apss_router.target_socket'}},
    apss={moduletype='cpu_arm_cortexA53',args={'&apss_instance'},rvbar=0x300000,
        has_el3=true,has_el2=true,mem={bind='&apss_access.target_socket'}},
    -- LLD places C constants in the next 64 KiB segment after executable code.
    -- The raw image must fit in full or gs_memory rejects the complete load.
    rom={moduletype='gs_memory',read_only=true,
        target_socket={address=0x300000,size=0x20000,bind='&apss_router.initiator_socket'},
        load={{bin_file=env_root..'producer.bin',offset=0}}},
    ram={moduletype='gs_memory',dmi_allow=false,
        target_socket={address=0x14800000,size=0x10000,bind='&apss_router.initiator_socket'}},
    dma_ram={moduletype='addrtr',mapped_base_addr=0,
        target_socket={address=0x14800000,size=0x10000,relative_addresses=false,
            bind='&dma_router.initiator_socket'},
        initiator_socket={bind='&ram.target_socket'}}}
local irq=dofile(directory..'qcom/apss-gic.lua')(platform,
    {cpu='apss',instance='apss_instance',router='apss_router'})
local luns={}
for i=0,5 do luns[i]=state_root..'lun'..i..'.img' end
dofile(directory..'qcom/sm88xx/ufs.lua')(platform,{router='apss_router',ram_router='dma_router',
    irq=irq,virtual_phy=true,luns=luns,boot_enable=true,boot_lun_en=1,
    boot_lun_ids={[0]=0,1,2,0,0,0},rpmb_size_kib=profile.rpmb_size_kib,
    rpmb_state_file=state_root..'rpmb.bin'})
-- Trace the 12 KiB UFS data buffer and the separate eight-byte CPU control cell.
-- The trace reports the length of each transaction that reaches each ingress.
platform.ufs_dma_access.trace_base=0x14803000
platform.ufs_dma_access.trace_size=0x3000
platform.apss_access.trace_base=0x14806000
platform.apss_access.trace_size=8
