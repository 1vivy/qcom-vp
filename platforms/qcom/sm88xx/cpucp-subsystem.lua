-- Firmware-visible CPUCP initialization hardware. Geometry and cold state are
-- explicit virtual board choices, not captured silicon identification values.
return function(p,o)
    assert(o and o.router and o.cpu and o.instance,
        'CPUCP subsystem requires its CPU, native instance, and router.')
    assert(not p.cpucp_subsystem, 'CPUCP subsystem already exists.')
    local bus='&'..o.router..'.initiator_socket'
    p.cpucp_low_power_telemetry={moduletype='qcom,cpucp-low-power-telemetry-v1',
        target_socket={address=0x18871000,size=0x120,bind=bus}}
    -- APSS_CPUCP_CORE_HANG precedes the low-power banks at module +0x1000.
    -- CPUCP 0x1d803026..0x1d80304c arms each hart's detector: threshold 0x2ef2c, then EN.
    -- Executing harts retire instructions, so no stall reaches a threshold. STS stays zero.
    -- The existing owners keep +0x4c (APSS LPM security) and +0x50 (clock-gate override).
    for _,word in ipairs({
        {0x00,1},{0x04,1},{0x10,0xffffffff},{0x14,0xffffffff},{0x20,1},{0x24,1},
        {0x30,0},{0x34,0},{0x40,0},{0x48,1},{0x54,7},{0x58,1},
    }) do
        p['cpucp_core_hang_'..string.format('%02x',word[1])]={
            moduletype='qcom,retained-boot-control-v1',writable_mask=word[2],reset_value=0,
            target_socket={address=0x18870000+word[1],size=4,bind=bus}}
    end
    -- Each CPRH target shares its state between the PDP and the CPUCP.
    -- The level request block remains a separate owner at target offset 0x2600.
    local cprh_ownership_words={10,6,10,10}
    for index,address in ipairs({0x181c8000,0x181d0000,0x183c8000,0x183d0000}) do
        local target='cpucp_cprh_target_'..index
        local request='cpucp_level_request_'..index
        p[target]={moduletype='qcom,cpuss-cprh-target-v1',
            -- Ideal sensors retain a fixed nominal clock without aging drift.
            reference_clock_hz=19200000,oscillator_clock_hz=1920000000,
            ownership_word_count=cprh_ownership_words[index],
            target_socket={address=address,size=0x2600,bind=bus},
            table_socket={address=address+0x3000,size=0x1000,bind=bus}}
        p[request]={moduletype='qcom,cpucp-level-request-v1',
            target_socket={address=address+0x2600,size=0xe4,bind=bus}}
        if o.apss_router then
            for _,window in ipairs({
                {target,'target_socket',address,0x2600},
                {target,'table_socket',address+0x3000,0x1000},
                {request,'target_socket',address+0x2600,0xe4},
            }) do
                p['apss_'..window[1]..'_'..window[2]]={moduletype='addrtr',mapped_base_addr=0,
                    target_socket={address=window[3],size=window[4],relative_addresses=false,
                        bind='&'..o.apss_router..'.initiator_socket'},
                    initiator_socket={bind='&'..window[1]..'.'..window[2]}}
            end
        end
    end
    -- The PDP limit driver programs this block through system addresses.
    -- The CPUCP thermal module copies its sample words through ACEHR.
    -- The CPRH targets at 0x181c8000 and 0x183c8000 bound the aperture.
    for lane,geometry in ipairs({{5,6},{7,2}}) do
        local address=0x181c0000+(lane-1)*0x200000
        local name='cpucp_llm_'..lane
        p[name]={moduletype='qcom,cpuss-llm-v1',
            first_sensor=geometry[1],core_count=geometry[2],temperature_decic=250,sample_valid=true,
            target_socket={address=address,size=0x8000,bind=bus}}
        -- The CPUCP owner stays on its own bus. The APSS router reaches the
        -- same owner for PDP system-space accesses below 0x20000000.
        if o.apss_router then
            p['apss_'..name]={moduletype='addrtr',mapped_base_addr=0,
                target_socket={address=address,size=0x8000,relative_addresses=false,
                    bind='&'..o.apss_router..'.initiator_socket'},
                initiator_socket={bind='&'..name..'.target_socket'}}
        end
    end
    p.cpucp_subsystem={moduletype='qcom,cpucp-subsystem-v1',
        control_socket={address=0x18876000,size=0x200,bind=bus},
        interrupt_socket={address=0x18875000,size=0x204,bind=bus},
        irq_0={bind='&'..o.cpu..'.irq_in_16'}}
    if o.cpu1 then p.cpucp_subsystem.irq_1={bind='&'..o.cpu1..'.irq_in_16'} end
    -- Firmware descriptor1d82d840 selects19.2MHz. Native compares use
    --18b84000+8*mhartid, and native MTIME is18b8bff8. Hart1 is optional: the
    -- recovered release contract requires a real second ACLINT timer context.
    p.cpucp_machine_timer={moduletype='riscv_aclint_mtimer',args={'&'..o.instance},
        construction_priority=1,hartid_base=0,num_harts=o.cpu1 and 2 or 1,timebase_freq=19200000,
        timecmp_base=0,time_base=0x7ff8,aperture_size=0x8000,provide_rdtime=false,
        mem={address=0x18b84000,size=0x8000,bind=bus},
        timer_irq_0={bind='&'..o.cpu..'.irq_in_7'}}
    if o.cpu1 then p.cpucp_machine_timer.timer_irq_1={bind='&'..o.cpu1..'.irq_in_7'} end
    -- The scheduler writes one at 0x18b80000+4*hart to request a software interrupt.
    p.cpucp_software_interrupt={moduletype='riscv_aclint_swi',args={'&'..o.instance,'&'..o.cpu},
        construction_priority=1,num_harts=o.cpu1 and 2 or 1,
        mem={address=0x18b80000,size=0x4000,bind=bus}}
    -- Both ELF descriptors upload 0x30a bytes with aligned word stores.
    -- These banks retain instructions only. They do not execute power sequences.
    for lane=0,1 do
        p['cpucp_sequence_ram_'..lane]={moduletype='gs_memory',init_mem=true,
            target_socket={address=0x188c8000+lane*0x10000,size=0x30c,bind=bus}}
        for _,offset in ipairs({0,4,8,0x10,0x14,0x18,0x1c,0x20,0x24,0x28,0x2c,
            0x30,0x34,0x38,0x3c,0x40,0x44,0x48,0x4c,0x50,0x54,0x70,0x74,0x78,0x7c}) do
            p['cpucp_sequence_branch_'..lane..'_'..offset]={
                moduletype='qcom,retained-boot-control-v1',writable_mask=0xffffffff,reset_value=0,
                target_socket={address=0x188c8470+lane*0x10000+offset,size=4,bind=bus}}
        end
    end
    for _,range in ipairs({{0x18877000,0x130},{0x188b8000,0x200}}) do
        p['cpucp_sequence_store_'..range[1]]={moduletype='gs_memory',init_mem=true,
            target_socket={address=range[1],size=range[2],bind=bus}}
    end
    for _,offset in ipairs({0,4,8,0xc,0x10,0x14,0x18,0x1c,0x20,0x24,0x2c,0x30,0x38,0x3c,0x40,0x44}) do
        p['cpucp_shared_sequence_branch_'..offset]={
            moduletype='qcom,retained-boot-control-v1',writable_mask=0xffffffff,reset_value=0,
            target_socket={address=0x188b8470+offset,size=4,bind=bus}}
    end
    p.cpucp_aux_sequence_ram={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x188a0000,size=0xf8,bind=bus}}
    -- CPUCP uploads and PDP configuration share each CPRH lane owner.
    p.cpucp_cprh={moduletype='qcom,pdp-cprh-v1',
        lane0_target0_path='cpucp_cprh_target_1',lane0_target1_path='cpucp_cprh_target_2',
        lane1_target0_path='cpucp_cprh_target_3',lane1_target1_path='cpucp_cprh_target_4',
        cpucp_lane0={address=0x180c0000,size=0x1000,bind=bus},
        cpucp_lane1={address=0x182c0000,size=0x1000,bind=bus}}
    if o.apss_router then
        p.cpucp_cprh.pdp_lane0={address=0x180c0000,size=0x1000,
            bind='&'..o.apss_router..'.initiator_socket'}
        p.cpucp_cprh.pdp_lane1={address=0x182c0000,size=0x1000,
            bind='&'..o.apss_router..'.initiator_socket'}
    end
    for _,address in ipairs({0x188a1000,0x188a2000,0x188a5000,0x188a502c}) do
        p['cpucp_sequence_selector_'..address]={moduletype='qcom,retained-boot-control-v1',
            writable_mask=0xffffffff,reset_value=0,
            target_socket={address=address,size=4,bind=bus}}
    end
    local indices={}
    for index=0,94 do indices[#indices+1]=tostring(index) end
    for lane=0,1 do
        p['cpucp_sequence_configuration_'..lane]={moduletype='qcom,cpucp-sequence-configuration-v1',
            indices=table.concat(indices,' '),
            target_socket={address=0x188c0000+lane*0x10000,size=0xbe0,bind=bus}}
    end
    p.cpucp_shared_sequence_configuration={moduletype='qcom,cpucp-sequence-configuration-v1',
        indices='52 53 0 1 2 3 4 7 8 11 12 15 17 16 18 19 56 57 60 61 64 65 67 68 69 70 71 72 73 74 75 76 77 78 79 80',
        target_socket={address=0x188b0000,size=0xa20,bind=bus}}
    for _,address in ipairs({0x188c8808,0x188c8810,0x188d8808,0x188d8810}) do
        p['cpucp_sequence_policy_'..address]={moduletype='qcom,retained-boot-control-v1',
            writable_mask=0xffffffff,reset_value=0,
            target_socket={address=address,size=4,bind=bus}}
    end
    -- Local SRAM and DDR snapshot buffers must not share backing.
    assert(p.cpucp_ram and p.cpucp_ram.moduletype=='gs_memory'
        and p.cpucp_ram.target_socket.address==0x18b30000
        and p.cpucp_ram.target_socket.size==0x40000
        and p.cpucp_ram.target_socket.bind==bus, 'CPUCP requires its complete local SRAM owner.')
    -- Per-hart table1d82c000 selects these Rocket-compatible BEU registers.
    -- No cache-error producer or unproven local NMI route is synthesized.
    for hart=0,1 do
        p['cpucp_bus_error_'..hart]={moduletype='qcom,cpucp-bus-error-v1',
            target_socket={address=0x18b70000+hart*0x1000,size=0x30,bind=bus}}
    end
    -- SM8850 CPUCP 0x1d808e16..0x1d808ef6 configures the post-ROM controls.
    -- These words retain configuration without a completion or reset effect.
    -- The existing CPU-domain owner still handles 0x18878424.
    for _,word in ipairs({
        {0x18860200,1},{0x18860204,7},{0x1887d128,1},{0x1887d124,0x10117},
        {0x18875208,1},{0x1887e034,1},{0x18870050,1},{0x18877708,1},
        {0x1887770c,7},{0x1887c104,1},{0x12004320,1},{0x18878444,1},
        {0x188782f0,7},{0x188780c0,0xffffffff},{0x188a1028,0xffffffff},
        {0x188a2028,0xffffffff},{0x188a503c,0xffffffff},{0x188a6028,0xffffffff},
        {0x1887e030,0xff},
    }) do
        p['cpucp_handoff_config_'..string.format('%08x',word[1])]={
            moduletype='qcom,retained-boot-control-v1',writable_mask=word[2],reset_value=0,
            target_socket={address=word[1],size=4,bind=bus}}
    end
    -- Both software mutex pools share ten hardware words. Reuse the existing
    -- atomic owner model, including APSS's already mapped physical lock one.
    for lock=0,9 do
        local address=0x18860000+4*lock
        local name='cpucp_hwmutex_'..lock
        if lock==1 and p.qsee_ticket_guard then
            local guard=p.qsee_ticket_guard
            assert(guard.moduletype=='qcom,smem-mutex-v1'
                and guard.target_socket.address==address and guard.target_socket.size==4,
                'CPUCP hardware mutex one requires the existing physical QSEE owner.')
            p[name]={moduletype='addrtr',mapped_base_addr=0,
                target_socket={address=address,size=4,relative_addresses=false,bind=bus},
                initiator_socket={bind='&qsee_ticket_guard.target_socket'}}
        else
            p[name]={moduletype='qcom,smem-mutex-v1',
                target_socket={address=address,size=4,bind=bus}}
            if o.apss_router then
                p['apss_'..name]={moduletype='addrtr',mapped_base_addr=0,
                    target_socket={address=address,size=4,relative_addresses=false,
                        bind='&'..o.apss_router..'.initiator_socket'},
                    initiator_socket={bind='&'..name..'.target_socket'}}
            end
        end
    end
    for _,bank in ipairs({
        {'ns',0x17640000,0x18820000,28},
        {'secure',0x17648000,0x18828000,24},
        {'pdp',0x19960000,0x18808000,40}}) do
        -- These are CPUCP-side banks. No unsupported cross-aperture route is
        -- inferred from the different APSS MailboxDxe addresses.
        p['cpucp_mailbox_'..bank[1]]={moduletype='qcom,cpucp-mailbox-v1',
            tx_socket={address=bank[2],size=0x300,bind=bus},
            rx_socket={address=bank[3],size=0x4c08,bind=bus},
            irq={bind='&cpucp_subsystem.source_'..bank[4]}}
    end
    -- The registered PDP1 device has its own transmitter but the same RX
    -- physical base. Do not create a second owner for pending/MAP/enable state.
    p.cpucp_mailbox_pdp.auxiliary_tx_socket={address=0x1a960000,size=0x300,bind=bus}
end
