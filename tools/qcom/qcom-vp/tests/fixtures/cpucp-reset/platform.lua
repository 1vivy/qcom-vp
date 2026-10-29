local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
local function make()
    local p={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'}}
    for name,arch in pairs({producer='AARCH64',consumer='RISCV64'}) do
        p[name..'_instance']={moduletype='QemuInstance',args={'&qemu_inst_mgr',arch},accel='tcg',
            sync_policy='multithread-quantum',qemu_args='-d in_asm -D '..root..name..'.log'}
        p[name..'_router']={moduletype='router'}
        p[name..'_access']={moduletype='AccessMonitor',allow_unspecified=arch~='AARCH64',
            initiator_socket={bind='&'..name..'_router.target_socket'}}
    end
    p.producer={moduletype='cpu_arm_cortexA53',args={'&producer_instance'},rvbar=0x300000,
        has_el3=true,has_el2=true,mem={bind='&producer_access.target_socket'}}
    p.consumer={moduletype='cpu_riscv64',args={'&consumer_instance',0},resetvec=0x334000,
        mem={bind='&consumer_access.target_socket'}}
    for name,address in pairs({producer=0x300000,consumer=0x334000}) do
        p[name..'_rom']={moduletype='gs_memory',read_only=true,
            target_socket={address=address,size=0x1000,bind='&'..name..'_router.initiator_socket'},
            load={{bin_file=root..name..'.bin',offset=0}}}
    end
    p.shared={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x14800000,size=0x1000,bind='&consumer_router.initiator_socket'}}
    p.alias={moduletype='addrtr',mapped_base_addr=0x14800000,
        target_socket={address=0x14800000,size=0x1000,relative_addresses=false,
            bind='&producer_router.initiator_socket'},
        initiator_socket={bind='&consumer_router.target_socket'}}
    p.pad={moduletype='qcom,cpucp-pad-control-v1',reset_value=0,
        target_socket={address=0x0f1e0000,size=4,bind='&producer_router.initiator_socket'}}
    p.boot={moduletype='qcom,cpucp-boot-control-v1',args={'&consumer'},
        hart_index=0,sifive_cease=true,reset={bind='&consumer.reset'},
        target_socket={address=0x17c40000,size=0x64,bind='&producer_router.initiator_socket'},
        reset_socket={address=0x17c42000,size=4,bind='&producer_router.initiator_socket'},
        setup_socket={address=0x18877600,size=0x90,bind='&producer_router.initiator_socket'}}
    p.runtime_reference={moduletype='qcom,virtual-always-on-clock-v1'}
    p.runtime_branch={moduletype='qcom,clock-branch-v1',source_path='runtime_reference',
        target_socket={address=0x17c07000,size=4,bind='&producer_router.initiator_socket'}}
    dofile(assert(os.getenv('QCOM_VP_PLATFORM_DIR'))..'/qcom/sm88xx/cpucp-runtime-controls.lua')(p,
        {boot='boot',clock='runtime_branch',lanes=3,routers={'producer_router'}})
    return p
end
platform=make()
