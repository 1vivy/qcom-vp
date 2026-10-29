local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
local function make()
    local p={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'}}
    for name,arch in pairs({producer='AARCH64',consumer='RISCV32'}) do
        p[name..'_instance']={moduletype='QemuInstance',args={'&qemu_inst_mgr',arch},accel='tcg',
            sync_policy='multithread-quantum',qemu_args='-d in_asm -D '..root..name..'.log'}
        p[name..'_router']={moduletype='router'}
        p[name..'_access']={moduletype='AccessMonitor',allow_unspecified=arch~='AARCH64',
            initiator_socket={bind='&'..name..'_router.target_socket'}}
    end
    p.producer={moduletype='cpu_arm_cortexA53',args={'&producer_instance'},rvbar=0x300000,
        has_el3=true,has_el2=true,mem={bind='&producer_access.target_socket'}}
    p.consumer={moduletype='cpu_riscv32',args={'&consumer_instance',0},resetvec=0x200000,
        mem={bind='&consumer_access.target_socket'}}
    for name,address in pairs({producer=0x300000,consumer=0x200000}) do
        p[name..'_rom']={moduletype='gs_memory',read_only=true,
            target_socket={address=address,size=0x1000,bind='&'..name..'_router.initiator_socket'},
            load={{bin_file=root..name..'.bin',offset=0}}}
    end
    p.shared={moduletype='qcom,shared-executable-memory-v1',args={'&consumer'},
        init_mem=true,target_socket={address=0x400000,size=0x3000,bind='&consumer_router.initiator_socket'}}
    p.alias={moduletype='addrtr',mapped_base_addr=0x400000,
        target_socket={address=0x20400000,size=0x3000,relative_addresses=false,bind='&producer_router.initiator_socket'},
        initiator_socket={bind='&consumer_router.target_socket'}}
    return p
end
platform=make()
