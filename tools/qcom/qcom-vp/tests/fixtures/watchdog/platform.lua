-- SPDX-License-Identifier: BSD-3-Clause-Clear
local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))
local packed=dofile(root..'/policy.lua')
platform={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'},
    tme_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','RISCV32'},accel='tcg',
        sync_policy='multithread-quantum',qemu_args='-d in_asm,int -D '..root..'/producer.log'},
    tme_router={moduletype='router'},
    tme_access={moduletype='AccessMonitor',allow_unspecified=true,
        initiator_socket={bind='&tme_router.target_socket'}},
    tme={moduletype='qcom,tme-clic-cpu-v1',args={'&tme_instance',0},resetvec=0x200000,
        mem={bind='&tme_access.target_socket'}},
    rom={moduletype='gs_memory',read_only=true,
        target_socket={address=0x200000,size=0x1000,bind='&tme_router.initiator_socket'},
        load={{bin_file=root..'/producer.bin',offset=0}}},
    ram={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x400000,size=0x1000,bind='&tme_router.initiator_socket'}},
    watchdog={moduletype=packed and 'qcom,kpss-watchdog-v1' or 'qcom,tme-watchdog-v1',clock_hz=1000,
        target_socket={address=0xc24000,size=packed and 0x24 or 0x14,bind='&tme_router.initiator_socket'},
        bark={bind='&clic.source_27'},bite={bind='&clic.source_28'}},
    reference={moduletype='qcom,tme-watchdog-v1',clock_hz=1000,
        target_socket={address=0xc25000,size=0x14,bind='&tme_router.initiator_socket'}},
    clic={moduletype='qcom,tme-clic-v1',priority_reset=0,
        pending_socket={address=0x1800000,size=0x41,bind='&tme_router.initiator_socket'},
        enable_socket={address=0x1800400,size=0x41,bind='&tme_router.initiator_socket'},
        priority_socket={address=0x1800800,size=0x41,bind='&tme_router.initiator_socket'},
        threshold_socket={address=0x1800c00,size=1,bind='&tme_router.initiator_socket'},
        source_socket={address=0xc00100,size=0x20,bind='&tme_router.initiator_socket'}}}
if packed then
    platform.watchdog.nonsecure_reset=true
    platform.watchdog.security_socket={
        address=0xc23ffc,size=4,bind='&tme_router.initiator_socket'}
end
for id=0,64 do platform.clic['irq_'..id]={bind='&tme.clic_irq_'..id} end
