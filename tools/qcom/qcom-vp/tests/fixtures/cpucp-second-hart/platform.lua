local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
local directory=assert(os.getenv('QCOM_VP_PLATFORM_DIR'))..'/'
local function make()
    local p={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'}}
    for _,name in ipairs({'producer','consumer'}) do
        p[name..'_instance']={moduletype='QemuInstance',
            args={'&qemu_inst_mgr',name=='producer' and 'AARCH64' or 'RISCV64'},accel='tcg',
            sync_policy='multithread-quantum',qemu_args='-d in_asm -D '..root..name..'.log'}
        p[name..'_router']={moduletype='router'}
        p[name..'_access']={moduletype='AccessMonitor',allow_unspecified=name~='producer',
            initiator_socket={bind='&'..name..'_router.target_socket'}}
    end
    -- Both harts share one native instance and memory map.
    p.consumer_cpu_router={moduletype='router'}
    p.consumer_access.target_socket={address=0,size=0x1000000000000,relative_addresses=false,
        bind='&consumer_cpu_router.initiator_socket'}
    p.producer={moduletype='cpu_arm_cortexA53',args={'&producer_instance'},rvbar=0x300000,
        has_el3=true,has_el2=true,mem={bind='&producer_access.target_socket'}}
    -- Boot control sets the secondary's native powered-off property before realization.
    p.consumer={moduletype='cpu_riscv64',args={'&consumer_instance',0},resetvec=0x334000,
        mem={bind='&consumer_cpu_router.target_socket'}}
    p.consumer_hart1={moduletype='cpu_riscv64',args={'&consumer_instance',1},resetvec=0x334000,
        mem={bind='&consumer_cpu_router.target_socket'}}
    for name,address in pairs({producer=0x300000,consumer=0x334000,hart1=0x400000}) do
        p[name..'_rom']={moduletype='gs_memory',read_only=true,
            target_socket={address=address,size=0x1000,
                bind='&'..(name=='producer' and 'producer' or 'consumer')..'_router.initiator_socket'},
            load={{bin_file=root..name..'.bin',offset=0}}}
    end
    p.hart1_rom.moduletype='qcom,shared-executable-memory-v1'
    p.hart1_rom.args={'&consumer'}
    p.hart1_rom.execution_sibling='consumer_hart1'
    p.shared={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x14800000,size=0x1000,bind='&consumer_router.initiator_socket'}}
    p.alias={moduletype='addrtr',mapped_base_addr=0x14800000,
        target_socket={address=0x14800000,size=0x1000,relative_addresses=false,
            bind='&producer_router.initiator_socket'},
        initiator_socket={bind='&consumer_router.target_socket'}}
    p.pad={moduletype='qcom,cpucp-pad-control-v1',reset_value=0,
        target_socket={address=0x0f1e0000,size=4,bind='&producer_router.initiator_socket'}}
    p.boot={moduletype='qcom,cpucp-boot-control-v1',args={'&consumer'},
        hart_index=0,reset={bind='&consumer.reset'},
        hart1_path='consumer_hart1',reset1={bind='&consumer_hart1.reset'},
        target_socket={address=0x17c40000,size=0x64,bind='&producer_router.initiator_socket'},
        reset_socket={address=0x17c42000,size=4,bind='&producer_router.initiator_socket'},
        setup_socket={address=0x18877600,size=0x90,bind='&producer_router.initiator_socket'}}
    p.runtime_reference={moduletype='qcom,virtual-always-on-clock-v1'}
    p.runtime_branch={moduletype='qcom,clock-branch-v1',source_path='runtime_reference',
        target_socket={address=0x17c07000,size=4,bind='&producer_router.initiator_socket'}}
    dofile(directory..'qcom/sm88xx/cpucp-runtime-controls.lua')(p,
        {boot='boot',clock='runtime_branch',lanes=3,routers={'producer_router'}})
    p.cpucp_ram={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x18b30000,size=0x40000,bind='&consumer_router.initiator_socket'}}
    dofile(directory..'qcom/sm88xx/cpucp-subsystem.lua')(p,
        {router='consumer_router',cpu='consumer',cpu1='consumer_hart1',instance='consumer_instance'})
    -- Virtual policy connects the subsystem commit to secondary release.
    p.cpucp_subsystem.hart1_release={bind='&boot.hart1_release'}
    return p
end
platform=make()
