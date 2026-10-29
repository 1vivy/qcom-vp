local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
local trust=dofile(root..'trust.lua')
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
    handoff={moduletype='gs_memory',init_mem=true,dmi_allow=false,
        target_socket={address=0x24030000,size=4,bind='&apss_router.initiator_socket'}}}
for i=0,7 do
    platform['program'..i]={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x24000000+i*0x1000,size=0x1000},
        load={{bin_file=root..'program'..i..'.bin',offset=0}}}
    platform['input'..i]={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x24010000+i*0x1000,size=0x1000},
        load={{bin_file=root..'input'..i..'.bin',offset=0}}}
    platform['mailbox'..i]={moduletype='gs_memory',init_mem=true,dmi_allow=false,
        target_socket={address=0x24020000+i*0x1000,size=0x1000,bind='&apss_router.initiator_socket'},
        load={{bin_file=root..'mailbox.bin',offset=0}}}
end
for i=0,7 do
    -- The guest starts each instance with a four-byte write at offset zero.
    platform['status'..i]={moduletype='qcom,tme-boot-status-v1',interface_version=0,
        hardware_version=0xa01b0200,
        target_socket={address=0x22236000+i*0x1000,size=0x50,
            bind='&apss_router.initiator_socket'},
        program_socket={bind='&program'..i..'.target_socket'},
        input_socket={bind='&input'..i..'.target_socket'},
        mailbox_socket={bind='&mailbox'..i..'.target_socket'},
        handoff_socket={bind='&handoff.target_socket'}}
    if i~=7 then
        platform['status'..i].trusted_root=root..(i==4 and 'wrong-root.bin' or 'root.bin')
        platform['status'..i].entitlement_sha256=trust.approved
    end
end
