local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
local function make()
    local p={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'}}
    for _,name in ipairs({'producer','consumer'}) do
        p[name..'_instance']={moduletype='QemuInstance',args={'&qemu_inst_mgr','AARCH64'},accel='tcg',
            sync_policy='multithread-quantum',qemu_args='-d in_asm -D '..root..name..'.log'}
        p[name..'_router']={moduletype='router'}
        p[name..'_access']={moduletype='AccessMonitor',
            initiator_socket={bind='&'..name..'_router.target_socket'}}
    end
    p.consumer_cpu_router={moduletype='router'}
    p.consumer_access.target_socket={address=0,size=0x1000000000000,relative_addresses=false,
        bind='&consumer_cpu_router.initiator_socket'}
    p.producer={moduletype='cpu_arm_cortexA53',args={'&producer_instance'},rvbar=0x300000,
        has_el3=true,has_el2=true,mem={bind='&producer_access.target_socket'}}
    p.consumer_group={moduletype='qcom,apss-execution-group-v1',args={'&consumer_instance'}}
    for index=0,7 do
        local name=index==0 and 'consumer' or 'consumer'..index
        p[name]={moduletype='qcom,apss-cpu-v1',args={'&consumer_instance','&consumer_group'},
            rvbar=0x334000,has_el3=true,has_el2=true,group_index=index,
            mp_affinity=index<6 and index*0x100 or 0x10000+(index-6)*0x100,
            initial_reset=index~=0,mem={bind='&consumer_cpu_router.target_socket'}}
    end
    for name,address in pairs({producer=0x300000,consumer=0x334000}) do
        p[name..'_rom']={moduletype='gs_memory',read_only=true,
            target_socket={address=address,size=name=='producer' and 0x20000 or 0x4000,
                bind='&'..name..'_router.initiator_socket'},
            load={{bin_file=root..name..'.bin',offset=0}}}
    end
    p.pdp_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','AARCH64'},
        accel='tcg',tcg_mode='MULTI',sync_policy='multithread-quantum',
        qemu_args='-d in_asm,guest_errors -D '..root..'pdp.log'}
    p.pdp_router={moduletype='router'}
    p.pdp_access={moduletype='AccessMonitor',allow_unspecified=false,
        initiator_socket={bind='&pdp_router.target_socket'}}
    p.pdp={moduletype='qcom,pdp-cpu-v1',args={'&pdp_instance'},
        init_nsvtor=0x20000000,num_irq=90,pmsav7_dregion=16,
        clock_hz=19200000,reference_clock_hz=19200000,
        mem={bind='&pdp_access.target_socket'}}
    p.pdp_ram={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x20000000,size=0x20000,bind='&pdp_router.initiator_socket'},
        load={{bin_file=root..'pdp.bin',offset=0}}}
    p.pdp_shared={moduletype='addrtr',mapped_base_addr=0x14800000,
        target_socket={address=0x14800000,size=0x1000,relative_addresses=false,
            bind='&pdp_router.initiator_socket'},
        initiator_socket={bind='&consumer_router.target_socket'}}
    p.stack={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x14000000,size=0x1000,bind='&producer_router.initiator_socket'}}
    p.shared={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x14800000,size=0x1000,bind='&consumer_router.initiator_socket'}}
    p.alias={moduletype='addrtr',mapped_base_addr=0x14800000,
        target_socket={address=0x14800000,size=0x1000,relative_addresses=false,
            bind='&producer_router.initiator_socket'},
        initiator_socket={bind='&consumer_router.target_socket'}}
    p.gcc_plls={moduletype='qcom,gcc-taycan-eko-t-pll-v1',pll_count=5,
        target_socket={address=0x100000,size=0x4038,bind='&producer_router.initiator_socket'},
        vote_socket={address=0x152020,size=4,bind='&producer_router.initiator_socket'}}
    p.domain_root={moduletype='qcom,rcg-v1',pll_path='gcc_plls',
        crystal_selector=0,primary_selector=1,secondary_selector=8,require_root_enable=true,
        target_socket={address=0x17c02004,size=0x14,bind='&producer_router.initiator_socket'}}
    p.domain_branches={moduletype='qcom,clock-branch-enables-v1',
        target_socket={address=0x17c0402c,size=0x14,bind='&producer_router.initiator_socket'}}
    p.ncc_pll0={moduletype='qcom,ncc-zonda-pll-v1',
        target_socket={address=0x19a30000,size=0x34,bind='&producer_router.initiator_socket'}}
    for index,address in ipairs({0x181c8000,0x181d0000}) do
        p['cprh_target'..index]={moduletype='qcom,cpuss-cprh-target-v1',
            target_socket={address=address,size=0x2600,bind='&producer_router.initiator_socket'},
            table_socket={address=address+0x3000,size=0x1000,bind='&producer_router.initiator_socket'}}
    end
    p.cprh={moduletype='qcom,pdp-cprh-v1',
        lane0_target0_path='cprh_target1',lane0_target1_path='cprh_target2',
        pdp_lane0={address=0x180c0000,size=0x1000,bind='&producer_router.initiator_socket'}}
    p.domains={moduletype='qcom,cpu-domain-power-v1',root_path='domain_root',
        branches_path='domain_branches',lane0_attached_mask=0x3f,lane1_attached_mask=3,
        initially_powered_mask=1,
        lane0_core_mask=0x3f,lane1_core_mask=3,
        ncc_pll0_path='ncc_pll0',
        cprh_path='cprh',
        reset_l0_c0={bind='&consumer.reset'},
        vector_l0_c0_target='consumer.reset_vector',
        pdp_reset0={bind='&pdp.reset'},
        pdp_vector0={bind='&pdp.reset_vector'},
        init_socket={address=0x17960020,size=0x14,bind='&producer_router.initiator_socket'},
        clock_socket={address=0x17c020a4,size=0x14,bind='&producer_router.initiator_socket'},
        control_socket={address=0x18878000,size=0x4188,bind='&producer_router.initiator_socket'},
        startup_socket={address=0x18871200,size=0x108,bind='&producer_router.initiator_socket'},
        tags0_socket={address=0x18000000,size=0x638,bind='&producer_router.initiator_socket'},
        tags1_socket={address=0x18200000,size=0x638,bind='&producer_router.initiator_socket'},
        aux0_socket={address=0x18040000,size=0x420,bind='&producer_router.initiator_socket'},
        aux1_socket={address=0x18240000,size=0x420,bind='&producer_router.initiator_socket'},
        downstream0_socket={address=0x18060000,size=0x10,bind='&producer_router.initiator_socket'},
        downstream1_socket={address=0x18260000,size=0x10,bind='&producer_router.initiator_socket'},
        route0_socket={address=0x198f0000,size=0x98,bind='&producer_router.initiator_socket'},
        route1_socket={address=0x1a8f0000,size=0x98,bind='&producer_router.initiator_socket'},
        runtime0_socket={address=0x19850000,size=0xa20,bind='&producer_router.initiator_socket'},
        runtime1_socket={address=0x1a850000,size=0x190,bind='&producer_router.initiator_socket'},
        window0_socket={address=0x18080000,size=0x48,bind='&producer_router.initiator_socket'},
        window1_socket={address=0x18280000,size=0x28,bind='&producer_router.initiator_socket'},
        power0_socket={address=0x19880000,size=0x610,bind='&producer_router.initiator_socket'},
        power1_socket={address=0x1a880000,size=0x610,bind='&producer_router.initiator_socket'},
        bridge0_socket={address=0x199a2000,size=0x38,bind='&producer_router.initiator_socket'},
        bridge1_socket={address=0x1a9a2000,size=0x38,bind='&producer_router.initiator_socket'},
        sequence0_socket={address=0x198b0000,size=0x2300,bind='&producer_router.initiator_socket'},
        sequence1_socket={address=0x1a8b0000,size=0x2300,bind='&producer_router.initiator_socket'},
        fabric0_socket={address=0x19830000,size=0x328,bind='&producer_router.initiator_socket'},
        fabric1_socket={address=0x1a830000,size=0x328,bind='&producer_router.initiator_socket'},
        cluster0_socket={address=0x19898000,size=0x688,bind='&producer_router.initiator_socket'},
        cluster1_socket={address=0x1a898000,size=0x688,bind='&producer_router.initiator_socket'},
        table0_socket={address=0x199e0000,size=0x1418,bind='&producer_router.initiator_socket'},
        table1_socket={address=0x1a9e0000,size=0x1418,bind='&producer_router.initiator_socket'},
        cluster_aux0_socket={address=0x19aa0000,size=0xc98,bind='&producer_router.initiator_socket'},
        cluster_aux1_socket={address=0x1aaa0000,size=0xc98,bind='&producer_router.initiator_socket'},
    }
    for core=0,5 do
        local offset=core*0x100000
        p['core0_policy_'..core]={moduletype='addrtr',mapped_base_addr=offset+0x150,
            target_socket={address=0x19032150+offset,size=8,relative_addresses=false,
                bind='&producer_router.initiator_socket'},
            initiator_socket={bind='&domains.core0_socket'}}
        p['core0_runtime_policy_'..core]={moduletype='addrtr',mapped_base_addr=offset+0xb8,
            target_socket={address=0x190320b8+offset,size=0x80,relative_addresses=false,
                bind='&producer_router.initiator_socket'},
            initiator_socket={bind='&domains.core0_socket'}}
        for row=0,2 do
            p['core0_runtime_config_'..core..'_'..row]={moduletype='addrtr',
                mapped_base_addr=offset+0xe008+row*0x40,
                target_socket={address=0x19040008+offset+row*0x40,size=0x20,
                    relative_addresses=false,bind='&producer_router.initiator_socket'},
                initiator_socket={bind='&domains.core0_socket'}}
        end
        p['core0_feature_'..core]={moduletype='addrtr',mapped_base_addr=offset+0xd00,
            target_socket={address=0x19030d00+offset,size=8,relative_addresses=false,
                bind='&producer_router.initiator_socket'},
            initiator_socket={bind='&domains.feature0_socket'}}
        p['release0_'..core]={moduletype='addrtr',mapped_base_addr=offset,
            target_socket={address=0x19050000+offset,size=0x10,relative_addresses=false,
                bind='&producer_router.initiator_socket'},
            initiator_socket={bind='&domains.release0_socket'}}
    end
    for core=0,1 do
        local offset=core*0x100000
        p['core1_policy_'..core]={moduletype='addrtr',mapped_base_addr=offset+0x150,
            target_socket={address=0x1a032150+offset,size=8,relative_addresses=false,
                bind='&producer_router.initiator_socket'},
            initiator_socket={bind='&domains.core1_socket'}}
        p['core1_runtime_policy_'..core]={moduletype='addrtr',mapped_base_addr=offset+0xb8,
            target_socket={address=0x1a0320b8+offset,size=0x80,relative_addresses=false,
                bind='&producer_router.initiator_socket'},
            initiator_socket={bind='&domains.core1_socket'}}
        for row=0,2 do
            p['core1_runtime_config_'..core..'_'..row]={moduletype='addrtr',
                mapped_base_addr=offset+0xe008+row*0x40,
                target_socket={address=0x1a040008+offset+row*0x40,size=0x20,
                    relative_addresses=false,bind='&producer_router.initiator_socket'},
                initiator_socket={bind='&domains.core1_socket'}}
        end
        p['core1_feature_'..core]={moduletype='addrtr',mapped_base_addr=offset+0xd00,
            target_socket={address=0x1a030d00+offset,size=8,relative_addresses=false,
                bind='&producer_router.initiator_socket'},
            initiator_socket={bind='&domains.feature1_socket'}}
        p['release1_'..core]={moduletype='addrtr',mapped_base_addr=offset,
            target_socket={address=0x1a050000+offset,size=0x10,relative_addresses=false,
                bind='&producer_router.initiator_socket'},
            initiator_socket={bind='&domains.release1_socket'}}
    end
    for index=1,7 do
        local lane=index<6 and 0 or 1
        local core=index<6 and index or index-6
        p.domains['reset_l'..lane..'_c'..core]={bind='&consumer'..index..'.reset'}
        p.domains['vector_l'..lane..'_c'..core..'_target']='consumer'..index..'.reset_vector'
    end
    return p
end
platform=make()