-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Evidence: pbl8850.bin reset at 0x300000 and RV64 slice at image offset 0x34000.
-- Evidence: tme8850.bin absolute pointers and reset at 0x200000.
local directory = assert(debug.getinfo(1, 'S').source:match('^@(.*/)'))
return function(o)
    assert(type(o.pbl)=='string' and type(o.tme)=='string', 'ROM paths are required.')
    assert(type(o.trace)=='string' and not o.trace:find('%s'), 'The trace prefix must not contain whitespace.')
    assert(not o.debug_pause or ((o.apss_gdb or 0)>0 and (o.tme_gdb or 0)>0 and (o.cpucp_gdb or 0)>0),
        'Reset pause requires a debugger port for each core.')
    assert(not o.observer_socket or (type(o.observer_socket)=='string' and
        not o.observer_socket:find('[%s,%z]') and not o.debug_pause and (o.apss_gdb or 0)==0),
        'The observer requires a separate APSS Unix socket.')
    assert((o.observer_socket~=nil)==(o.observer_clock_socket~=nil),
        'The observer requires its simulation clock endpoint.')
    local hardware_version=0xa01b0200
    local p={moduletype='Container',
        qemu_inst_mgr={moduletype='QemuInstanceManager'}}
    if o.observer_clock_socket then
        p.observation_clock={moduletype='ObservationClock',socket_path=o.observer_clock_socket}
    end
    local function core(name,arch,cpu,reset,port)
        local inst=name..'_instance'
        local router=name..'_router'
        local monitor=name..'_access'
        p[router]={moduletype='router'}
        -- The libqemu RISC-V core uses MEMTXATTRS_UNSPECIFIED for its single address space.
        p[monitor]={moduletype='AccessMonitor',allow_unspecified=arch~='AARCH64',
            initiator_socket={bind='&'..router..'.target_socket'}}
        local observer=(name=='apss') and o.observer_socket
        local thread_trace=name=='cpucp'
        p[inst]={moduletype='QemuInstance',args={'&qemu_inst_mgr',arch},accel='tcg',
            sync_policy='multithread-quantum',
            qemu_args=((o.debug_pause or observer) and '-S ' or '')..
                (observer and ('-gdb unix:'..observer..',server=on,wait=off ') or '')..
                '-d in_asm,int,guest_errors,unimp'..(thread_trace and ',tid' or '')..
                ' -D '..o.trace..'-'..name..(thread_trace and '-%d' or '')..'.log'}
        p[name]={moduletype=cpu,args={'&'..inst,0},mem={bind='&'..monitor..'.target_socket'}}
        if arch=='AARCH64' then
            p[name].args={'&'..inst}
            p[name].rvbar=reset
            p[name].has_el3=true
            p[name].has_el2=true
        else
            p[name].resetvec=reset
        end
        if port and port>0 then p[name].gdb_port=port end
        return router
    end
    local a=core('apss','AARCH64','qcom,apss-cpu-v1',0x300000,o.apss_gdb)
    p.apss.initial_reset=true
    p.apss_group={moduletype='qcom,apss-execution-group-v1',
        args={'&apss_instance'},cpu_count=8}
    p.apss_cpu_router={moduletype='router'}
    p.apss_access.target_socket={address=0,size=0x1000000000000,relative_addresses=false,
        bind='&apss_cpu_router.initiator_socket'}
    local apss_cpus={'apss','apss_l0_c1','apss_l0_c2','apss_l0_c3',
        'apss_l0_c4','apss_l0_c5','apss_l1_c0','apss_l1_c1'}
    -- The selected SEC tables declare six cores in lane zero and two in lane one.
    for index,name in ipairs(apss_cpus) do
        if index>1 then
            p[name]={moduletype='qcom,apss-cpu-v1',rvbar=0x300000,
                has_el3=true,has_el2=true,initial_reset=true,
                mem={bind='&apss_cpu_router.target_socket'}}
        end
        p[name].args={'&apss_instance','&apss_group'}
        p[name].mem.bind='&apss_cpu_router.target_socket'
        p[name].group_index=index-1
        p[name].mp_affinity=index<=6 and (index-1)*0x100 or 0x10000+(index-7)*0x100
        p[name].start_powered_off=false
    end
    local t=core('tme','RISCV32','cpu_riscv32',0x200000,o.tme_gdb)
    p.tme.pmp=true
    local c=core('cpucp','RISCV64','cpu_riscv64',0x334000,o.cpucp_gdb)
    p.cpucp_cpu_router={moduletype='router'}
    p.cpucp_access.target_socket={address=0,size=0x1000000000000,relative_addresses=false,
        bind='&cpucp_cpu_router.initiator_socket'}
    p.cpucp.mem.bind='&cpucp_cpu_router.target_socket'
    p.cpucp_hart1={moduletype='cpu_riscv64',args={'&cpucp_instance',1},resetvec=0x334000,
        mem={bind='&cpucp_cpu_router.target_socket'}}
    local function memory(name,router,address,size,path,offset)
        p[name]={moduletype='gs_memory',read_only=path~=nil,
            target_socket={address=address,size=size,bind='&'..router..'.initiator_socket'}}
        if path then
            p[name].load={{bin_file=path,offset=0,bin_file_offset=offset,bin_file_size=size}}
        end
    end
    memory('apss_rom',a,0x300000,0x3c000,o.pbl,0)
    -- The CPUCP image offset is a layout input. The default matches the PBL image that the evidence names.
    memory('cpucp_rom',c,0x334000,0x8000,o.pbl,o.pbl_cpucp_offset or 0x34000)
    memory('tme_rom',t,0x200000,0x20000,o.tme,0)
    -- PBL 0x31ed98..0x31ede0 describes SRAM data and a heap ending at 0x14a7fc00.
    -- CPUCP 0x335518..0x335526 clears the final 0x400-byte log region.
    memory('apss_sram',a,0x14800000,0x280000)
    p.apss_sram.moduletype='qcom,shared-executable-memory-v1'
    p.apss_sram.args={'&apss_group'}
    -- Candidate table 0x213dac maps APSS SRAM at 0x14800000 to TME address 0x18800000.
    -- OEM table 0x447368 bounds this shared view to 0x280000 bytes.
    p.tme_apss_sram={moduletype='addrtr',mapped_base_addr=0,
        target_socket={address=0x18800000,size=0x280000,relative_addresses=false,
            bind='&'..t..'.initiator_socket'},
        initiator_socket={bind='&apss_sram.target_socket'}}
    -- SYSTEM_IMEM spans 0x14680000..0x146ac000.
    -- Selected XBL table 0x148e6360 stores the handoff pointer at 0x14698fcc.
    -- Cold-zero is board policy. Firmware owns every handoff record and cookie.
    memory('apss_imem',a,0x14680000,0x2c000)
    p.apss_imem.init_mem=true
    -- Both selected SOCCP DEBUG ELFs use the first two 16 KiB pages.
    -- DCD uses the third page (maximum selected extent 0x211c). No ready data is seeded.
    memory('soccp_debug_sram',a,0x00d20000,0xc000)
    p.soccp_debug_sram.init_mem=true
    -- Genuine CPUCP PT_LOAD bytes and loader zero-fill survive architectural reset.
    memory('cpucp_image',a,0x1d800000,0x40000)
    p.cpucp_image.moduletype='qcom,shared-executable-memory-v1'
    p.cpucp_image.args={'&cpucp'}
    p.cpucp_image.execution_sibling='cpucp_hart1'
    p.cpucp_image.init_mem=true
    -- Selected XBL 0x148ebf88..0x148ebfa0 gives IRAM/DMEM base-size pairs.
    -- SHRM ELF p_paddr values confirm both load destinations.
    memory('shrm_iram',a,0x31020000,0x28000)
    memory('shrm_dmem',a,0x31048000,0x18000)
    p.shrm_iram.init_mem=true
    p.shrm_dmem.init_mem=true
    -- Evidence: TME ROM 0x213000..0x213008 bounds the local executable RAM.
    p.tme_ram={moduletype='qcom,shared-executable-memory-v1',args={'&tme'},init_mem=true,
        target_socket={address=0x400000,size=0x5c000,bind='&'..t..'.initiator_socket'}}
    -- The selected TME ELF uses APSS addresses from 0x2041c000 to 0x2044a000.
    p.apss_tme_ram={moduletype='addrtr',mapped_base_addr=0x400000,
        target_socket={address=0x20400000,size=0x5c000,relative_addresses=false,
            bind='&'..a..'.initiator_socket'},
        initiator_socket={bind='&'..t..'.target_socket'}}
    -- Runtime snapshots copy 0x40000 bytes from local SRAM into separate DDR buffers.
    -- Keep this owner across ROM startup, loader zero-fill, and architectural reset.
    memory('cpucp_ram',c,0x18b30000,0x40000)
    p.cpucp_ram.init_mem=true
    -- CPUCP 0x1d80b7ec..0x1d80b946 copies a 0x20000-byte DDR range through this local snapshot store.
    memory('cpucp_snapshot_ram',c,0x1d900000,0x20000)
    p.cpucp_snapshot_ram.init_mem=true
    -- CPUCP 0x1d80b79e loads the PDP image into this exact destination.
    memory('pdp0_ram',c,0x19930000,0x20000)
    p.pdp0_ram.init_mem=true
    -- Evidence: CPUCP log destination.
    p.cpucp_log={moduletype='addrtr',mapped_base_addr=0x27fc00,
        target_socket={address=0x14a7fc00,size=0x400,relative_addresses=false,
            bind='&'..c..'.initiator_socket'},
        initiator_socket={bind='&apss_sram.target_socket'}}
    -- Evidence: APBL 0x30d660 reads the canary and 0x305720 writes it.
    p.apss_scratch={moduletype='qcom,apss-boot-scratch-v1',canary_reset=0,
        target_socket={address=0x01fd4000,size=0x1000,bind='&'..a..'.initiator_socket'}}
    -- PBL 0x302e48 accepts selectors 0..3. Zero is a virtual board policy.
    -- APSS_HW_VERSION uses the declared reset value 0x10010000. No SM8850 value is measured.
    p.apss_identity={moduletype='qcom,apss-boot-identity-v1',hardware_version=0x10010000,
        profile_selector=0,variant_selector=0,cpu_present_mask=0x033f,llc_group_disable_mask=0,
        target_socket={address=0x17651000,size=0x28,bind='&'..a..'.initiator_socket'}}
    -- CPUCP 0x335812 reads the same selector through its own address view.
    p.cpucp_identity={moduletype='addrtr',mapped_base_addr=0x17651000,
        target_socket={address=0x17651000,size=0x28,relative_addresses=false,
            bind='&'..c..'.initiator_socket'},
        initiator_socket={bind='&'..a..'.target_socket'}}
    -- Evidence: CPUCP 0x3359aa reads this retained boot word.
    p.cpucp_scratch={moduletype='qcom,cpucp-boot-scratch-v1',reset_value=0,
        target_socket={address=0x01fd9000,size=4,bind='&'..c..'.initiator_socket'}}
    -- APSS 0x30d80c reads the same boot word.
    p.apss_cpucp_scratch={moduletype='addrtr',mapped_base_addr=0,
        target_socket={address=0x01fd9000,size=4,relative_addresses=false,bind='&'..a..'.initiator_socket'},
        initiator_socket={bind='&cpucp_scratch.target_socket'}}
    -- The block defines reset-debug configuration, FSM control, and status.
    -- One controller owns the functional cold POR-to-IDLE transition.
    p.aoss_reset_fsm={moduletype='qcom,aoss-reset-fsm-v1',
        debug_enable_socket={address=0x0c2f0000,size=4,bind='&'..a..'.initiator_socket'},
        control_socket={address=0x0c2f0004,size=4,bind='&'..a..'.initiator_socket'},
        status_socket={address=0x0c2f1004,size=4,bind='&'..a..'.initiator_socket'}}
    -- XBL 0x14827fbc reads this counter. XBLLoaderLib/boot_target.h specifies 32768 Hz.
    p.sleep_counter={moduletype='qcom,mpm-sleep-counter-v1',clock_hz=32768,
        target_socket={address=0x0c221000,size=4,bind='&'..a..'.initiator_socket'}}
    p.timer_router={moduletype='router'}
    for _, router in ipairs({a,c}) do
        p[router..'_timers']={moduletype='addrtr',mapped_base_addr=0x17810000,
            target_socket={address=0x17810000,size=0xe000,relative_addresses=false,
                bind='&'..router..'.initiator_socket'},
            initiator_socket={bind='&timer_router.target_socket'}}
        p[router..'_counter_control']={moduletype='addrtr',mapped_base_addr=0x0c210000,
            target_socket={address=0x0c210000,size=4,relative_addresses=false,
                bind='&'..router..'.initiator_socket'},
            initiator_socket={bind='&timer_router.target_socket'}}
    end
    -- CPUCP 0x33552a..0x335566 enables one counter and configures both frames.
    -- APBL 0x30cd74 and 0x30ccbc read the same counter through these frames.
    p.system_timer={moduletype='qcom,sm8850-qtimer-v1',clock_hz=19200000,
        enabled_at_reset=true,frame_access_reset=0x3f,
        -- Explicit virtual reset policy for XBL's early EL0 counter consumer.
        frame0_el0_access_reset=1,
        control_socket={address=0x0c210000,size=4,bind='&timer_router.initiator_socket'},
        config_socket={address=0x17810000,size=0x1000,bind='&timer_router.initiator_socket'},
        frame0_socket={address=0x17811000,size=0x1000,bind='&timer_router.initiator_socket'},
        frame0_el0_socket={address=0x17812000,size=0x1000,bind='&timer_router.initiator_socket'},
        frame1_socket={address=0x17813000,size=0x1000,bind='&timer_router.initiator_socket'},
        -- QSEE counter reads: SM8850 0xd8212404, SM8845 0xd820f534.
        frame2_socket={address=0x17815000,size=0x1000,bind='&timer_router.initiator_socket'},
        -- QSEE selects physical frames three and four for its secure timers.
        frame3_socket={address=0x17817000,size=0x1000,bind='&timer_router.initiator_socket'},
        frame4_socket={address=0x17819000,size=0x1000,bind='&timer_router.initiator_socket'},
        frame5_socket={address=0x1781b000,size=0x1000,bind='&timer_router.initiator_socket'},
        frame6_socket={address=0x1781d000,size=0x1000,bind='&timer_router.initiator_socket'}}
    -- TME 0x200738 reads its timestamp from the same virtual system counter.
    p.tme_counter={moduletype='addrtr',mapped_base_addr=0x17813000,
        target_socket={address=0x1b813000,size=4,relative_addresses=false,
            bind='&'..t..'.initiator_socket'},
        initiator_socket={bind='&timer_router.target_socket'}}
    -- TME 0x200752..0x200764 clears IMASK in its two local timer frames.
    -- Their counter uses the virtual 19.2 MHz source from reset.
    p.tme_timers={moduletype='qcom,sm8850-qtimer-v1',clock_hz=19200000,
        enabled_at_reset=true,frame_access_reset=0x3f,
        frame0_socket={address=0x00c22000,size=0x1000,bind='&'..t..'.initiator_socket'},
        frame1_socket={address=0x00c23000,size=0x1000,bind='&'..t..'.initiator_socket'}}
    p.gcc_pll_router={moduletype='router'}
    p.gcc_plls={moduletype='qcom,gcc-taycan-eko-t-pll-v1',reference_hz=19200000,pll_count=12,vote_count=2,
        target_socket={address=0x100000,size=0xb038,bind='&gcc_pll_router.initiator_socket'},
        vote_socket={address=0x152020,size=0x5004,bind='&gcc_pll_router.initiator_socket'}}
    -- XBL source descriptors 0x148f14a8..0x148f1710 identify twelve GCC PLL banks.
    -- XBL 0x148619a4 reads the APSS vote at 0x157020 independently of the ROM vote.
    local pll_ranges={{0x152020,4},{0x157020,4}}
    for index=0,11 do pll_ranges[#pll_ranges+1]={0x100000+index*0x1000,0x38} end
    for _,router in ipairs({a,c}) do
        for _,range in ipairs(pll_ranges) do
            p[router..'_gcc_pll_'..range[1]]={moduletype='addrtr',mapped_base_addr=range[1],
                target_socket={address=range[1],size=range[2],relative_addresses=false,
                    bind='&'..router..'.initiator_socket'},
                initiator_socket={bind='&gcc_pll_router.target_socket'}}
        end
    end
    p.cpucp_clock_router={moduletype='router'}
    -- ROM descriptors at 0x336d38 define each address and source selector.
    local roots={
        {0x17c01004,1,8,true},{0x17c03004,1,8,true},
        {0x17c02004,8,2,true},{0x17c00004,1,8,true},
        {0x1742d4,1,8,false},{0x174400,1,8,false},
        {0x174658,8,5,false},{0x17407c,8,5,false},
        {0x1484f0,1,8,false},{0x148030,8,5,false},{0x14402c,8,5,false},
        -- APSS descriptors at 0x31d4d0 use the same RCG interface.
        {0x18206c,1,8,false},{0x1741a8,1,8,false},{0x17452c,1,8,false},
        {0x176030,1,8,false},{0x176160,8,5,false},
        {0x14b00c,1,8,false,6},{0x177034,1,5,false,6},
        {0x1770a4,1,5,false,6},{0x139034,1,8,false,6},
        -- Append only: retain the original twenty root indices.
        -- Pinned gcc-canoe.c: ICE uses map 3. AUX uses XO-only map 4.
        {0x17708c,1,5,false,6},{0x1770c0,8,8,false},
        -- XBL domain table 0x148f29c8 adds GPLL1 and GPLL7 source selections.
        {0x141024,2,8,false,8,7},{0x1a200c,1,8,false},
        {0x148298,1,8,false},{0x148168,1,8,false},
        {0x11c0c8,4,8,false,8,1},{0x11c1f8,4,8,false,8,1},
        {0x183030,4,8,false,8,1},{0x144158,8,5,false}}
    local clock_ranges={}
    -- Explicit virtual reset policy: zero CFG selects the live XO reference.
    -- UFS roots use continuous demand, not a fabricated PLL or PHY clock.
    for index,root in ipairs(roots) do
        p['cpucp_clock_root_'..index]={moduletype='qcom,rcg-v1',pll_path='gcc_plls',
            crystal_selector=0,primary_selector=root[2],secondary_selector=root[3],
            primary_even_selector=root[5] or 8,
            primary_index=root[6] or 0,
            require_root_enable=root[4],
            target_socket={address=root[1],size=0x14,bind='&cpucp_clock_router.initiator_socket'}}
        clock_ranges[#clock_ranges+1]={root[1],0x14}
    end
    for _,row in ipairs({{1,0x17c01000,0x17c0113c},{2,0x17c03000,0x17c0225c},
        {3,0x17c02000,0x17c02168},{4,0x17c00000,0x17c00150}}) do
        local owner='cpucp_clock_root_'..row[1]
        p[owner].moduletype='qcom,cpucp-runtime-rcg-v1'
        p[owner..'_runtime_router']={moduletype='router'}
        p[owner].runtime_socket={address=0,size=0x28,bind='&'..owner..'_runtime_router.initiator_socket'}
        for _,range in ipairs({{row[2]+0x18,4,0},{row[2]+0x20,0x10,8},{row[3],8,0x20}}) do
            p[owner..'_runtime_'..range[1]]={moduletype='addrtr',mapped_base_addr=range[3],
                target_socket={address=range[1],size=range[2],relative_addresses=false,
                    bind='&cpucp_clock_router.initiator_socket'},
                initiator_socket={bind='&'..owner..'_runtime_router.target_socket'}}
            clock_ranges[#clock_ranges+1]={range[1],range[2]}
        end
    end
    -- Runtime table 0x1d8340b8 selects PLL4 through selector two on this root.
    p.cpucp_clock_root_2.secondary_selector=2
    -- XBL rate rows at 0x148f1be8 add five live sources to the existing GPLL4 selection.
    p.cpucp_clock_root_11.main_sources='1 10;2 6;3 5;4 1;7 2'
    p.cpucp_clock_divider={moduletype='qcom,clock-integer-divider-v1',
        pll_path='gcc_plls',source_index=0,
        target_socket={address=0x147110,size=4,bind='&cpucp_clock_router.initiator_socket'}}
    p.cpucp_clock_branches={moduletype='qcom,clock-branch-enables-v1',
        target_socket={address=0x17c0402c,size=0x14,bind='&cpucp_clock_router.initiator_socket'}}
    clock_ranges[#clock_ranges+1]={0x147110,4}
    clock_ranges[#clock_ranges+1]={0x17c0402c,0x14}
    -- APSS descriptors 0x31d478/0x31d490 select these branches.
    -- Their shared source is explicit virtual wiring to the neighboring RCG.
    for _,address in ipairs({0x174070,0x174074}) do
        p['boot_branch_'..address]={moduletype='qcom,clock-branch-v1',
            source_path='cpucp_clock_root_8',
            target_socket={address=address,size=4,bind='&cpucp_clock_router.initiator_socket'}}
        clock_ranges[#clock_ranges+1]={address,4}
    end
    -- Linux supplies no AHB parent. This reference is virtual wiring, not a silicon claim.
    p.ufs_ahb_reference={moduletype='qcom,virtual-always-on-clock-v1'}
    p.ufs_crystal_reference={moduletype='qcom,virtual-always-on-clock-v1'}
    -- gcc_parent_map_9/10/11: 0 selects the real PHY, 2 selects XO.
    -- Reset selector 0 is an explicit virtual policy. An absent PHY stays absent.
    for _,mux in ipairs({{'rx0',0x177068},{'rx1',0x1770ec},{'tx0',0x177058}}) do
        p['ufs_symbol_'..mux[1]]={moduletype='qcom,symbol-clock-mux-v1',
            phy_path='ufs',crystal_path='ufs_crystal_reference',reset_selector=0,
            target_socket={address=mux[2],size=4,bind='&cpucp_clock_router.initiator_socket'}}
        clock_ranges[#clock_ranges+1]={mux[2],4}
    end
    -- ROM 0x31e0b8: nine 24-byte descriptors, then a null sentinel.
    -- Symbol entries have flags=2 and skip halt polling. Status still follows the PHY.
    local ufs_branches={
        {0x1770f0,'cpucp_clock_root_18',true},
        {0x177028,'ufs_ahb_reference',true},
        {0x177018,'cpucp_clock_root_18',true},
        {0x17707c,'cpucp_clock_root_21',true},
        {0x1770bc,'cpucp_clock_root_22',true},
        {0x177030,'ufs_symbol_rx0',false},
        {0x1770d8,'ufs_symbol_rx1',false},
        {0x17702c,'ufs_symbol_tx0',false},
        {0x17706c,'cpucp_clock_root_19',true}}
    for _,branch in ipairs(ufs_branches) do
        p['ufs_branch_'..branch[1]]={moduletype='qcom,clock-branch-v1',
            source_path=branch[2],hardware_control=branch[3],
            -- Virtual demand replaces unmodeled consumer votes, never source readiness.
            continuous_demand=branch[3],
            -- UEFI ClockDxe 0x10b9c..0x10bfc sets and clears the CBCR memory-force fields
            -- 12..14 through Clock_ConfigClock. They control SRAM retention only.
            retained_mask=0x7000,
            target_socket={address=branch[1],size=4,bind='&cpucp_clock_router.initiator_socket'}}
        clock_ranges[#clock_ranges+1]={branch[1],4}
    end
    for _,router in ipairs({a,c}) do
        for _,range in ipairs(clock_ranges) do
            p[router..'_clock_root_'..range[1]]={moduletype='addrtr',mapped_base_addr=range[1],
                target_socket={address=range[1],size=range[2],relative_addresses=false,
                    bind='&'..router..'.initiator_socket'},
                initiator_socket={bind='&cpucp_clock_router.target_socket'}}
        end
    end
    -- CPH2771 QSEE 0xd8219ca0 configures 31 PDC-to-GIC trigger words.
    -- The virtual bank starts at zero. Writes do not generate interrupts.
    p.pdc_spi_trigger={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x179600f0,size=0x7c,bind='&'..a..'.initiator_socket'}}
    -- QSEE 0xd821ba4c and 0xd8218e38 acquire hardware mutex one before ticket allocation.
    p.qsee_ticket_guard={moduletype='qcom,smem-mutex-v1',
        target_socket={address=0x18860004,size=4,bind='&'..a..'.initiator_socket'}}
    -- CPUCP 0x3348ca configures the two domain lanes.
    p.cpu_domain_router={moduletype='router'}
    p.cpu_domains={moduletype='qcom,cpu-domain-power-v1',
        root_path='cpucp_clock_root_3',branches_path='cpucp_clock_branches',
        ncc_pll0_path='ncc_pll0',ncc_pll1_path='ncc_pll1',
        cprh_path='cpucp_cprh',
        initially_powered_mask=0,boot_lane=0,boot_core=0,
        lane0_core_mask=0x3f,lane1_core_mask=3,
        lane0_attached_mask=0x3f,lane1_attached_mask=3,
        init_socket={address=0x17960020,size=0x14,bind='&cpu_domain_router.initiator_socket'},
        clock_socket={address=0x17c020a4,size=0x14,bind='&cpu_domain_router.initiator_socket'},
        control_socket={address=0x18878000,size=0x4188,bind='&cpu_domain_router.initiator_socket'},
        tags0_socket={address=0x18000000,size=0x638,bind='&cpu_domain_router.initiator_socket'},
        tags1_socket={address=0x18200000,size=0x638,bind='&cpu_domain_router.initiator_socket'},
        aux0_socket={address=0x18040000,size=0x420,bind='&cpu_domain_router.initiator_socket'},
        aux1_socket={address=0x18240000,size=0x420,bind='&cpu_domain_router.initiator_socket'},
        downstream0_socket={address=0x18060000,size=0x10,bind='&cpu_domain_router.initiator_socket'},
        downstream1_socket={address=0x18260000,size=0x10,bind='&cpu_domain_router.initiator_socket'}}
    for index,name in ipairs(apss_cpus) do
        local lane=index<=6 and 0 or 1
        local core_index=index<=6 and index-1 or index-7
        local suffix='_l'..lane..'_c'..core_index
        p.cpu_domains['reset'..suffix]={bind='&'..name..'.reset'}
        p.cpu_domains['vector'..suffix..'_target']=name..'.reset_vector'
    end
    local domain_ranges={
        {0x17960020,0x14},{0x17c020a4,0x14},
        {0x188787a0,4},{0x18878424,4},{0x18878304,4},
        {0x188784a0,8},{0x18879000,8},{0x18879020,8},
        {0x1887c020,8},{0x1887c0a0,8},
        {0x18878040,8},{0x18878080,8},{0x18878400,4},{0x1887c180,8},
        {0x18000030,8},{0x18000038,8},{0x18000040,8},{0x18000630,8},
        {0x18200030,8},{0x18200038,8},{0x18200040,8},{0x18200630,8},
        {0x18040018,8},{0x18040418,8},{0x18240018,8},{0x18240418,8},
        {0x18060008,8},{0x18260008,8}}
    -- CPUCP 0x1d808d08 clears the NSI clock-gating overrides of each lane.
    -- Names, reset values, and fields: APSS_NSINW0 control plane +0xb70/+0xb78/+0xb90,
    -- debug plane +0x478/+0x480, security gasket +0x90/+0x98, LMH gasket +0xb8/+0xc0,
    -- and ARCE +0xb0/+0xb8. The CPRH gasket words +0x5c8..+0x5d8 use the CPRH owner.
    local ones='ffffffffffffffff'
    local nsi_banks={
        {'control',0x00b70,0x28,'0:'..ones..':'..ones..' 8:1:1 20:'..ones..':'..ones},
        {'debug',0x40478,0x10,'0:'..ones..':'..ones..' 8:1:1'},
        {'security',0x60090,0x10,'0:'..ones..':'..ones..' 8:1:1'},
        {'lmh',0x700b8,0x10,'0:0:'..ones..' 8:0:1'},
        {'arce',0x800b0,0x10,'0:0:'..ones..' 8:0:1'}}
    for lane=0,1 do
        for _,bank in ipairs(nsi_banks) do
            local address=0x18000000+lane*0x200000+bank[2]
            p['nsi_'..bank[1]..'_cgc'..lane]={moduletype='qcom,clock-gating-overrides-v1',words=bank[4],
                target_socket={address=address,size=bank[3],bind='&cpu_domain_router.initiator_socket'}}
            domain_ranges[#domain_ranges+1]={address,bank[3]}
        end
    end
    -- CPUCP ACEHR0 channel 41 copies each core's AMU counters at +0x000 and +0x100.
    -- NCC_AMU_0 is APSS_HM+0x02010000 for core zero, one MiB per core.
    for lane=0,1 do
        for core=0,(lane==0 and 5 or 1) do
            local address=0x19010000+lane*0x01000000+core*0x100000
            p[string.format('core_amu_l%d_c%d',lane,core)]={moduletype='arm,amu-v1',
                pll_path='ncc_pll'..lane,
                target_socket={address=address,size=0x1000,bind='&cpu_domain_router.initiator_socket'}}
            domain_ranges[#domain_ranges+1]={address,0x1000}
        end
    end
    -- CPUCP 0x334ba4 programs each cluster and releases its selected core.
    -- A sparse bank may span other owners. Priority one lets those exact owners win.
    local function domain_bank(name,address,size,offsets,width,priority)
        p.cpu_domains[name..'_socket']={
            address=address,size=size,priority=priority,bind='&cpu_domain_router.initiator_socket'}
        for _,offset in ipairs(offsets) do
            domain_ranges[#domain_ranges+1]={address+offset,width}
        end
    end
    domain_bank('startup',0x18871200,0x108,{0,4,0x100,0x104},4)
    for lane=0,1 do
        local address=0x17951060+lane*4
        p['cpucp_runtime_gate_'..lane]={moduletype='qcom,retained-boot-control-v1',
            writable_mask=3,reset_value=0,
            target_socket={address=address,size=4,bind='&'..c..'.initiator_socket'}}
    end
    p.cpucp_runtime_identity={moduletype='qcom,retained-boot-control-v1',
        writable_mask=0,reset_value=0,
        target_socket={address=0x3a04,size=4,bind='&'..c..'.initiator_socket'}}
    -- CPUCP 0x1d80f2e4..0x1d80f312 and 0x1d80f3b4..0x1d80f3ce only write these words.
    -- 0x1d80f318..0x1d80f320 then enables PLIC source 36. No event producer is identified.
    for _,row in ipairs({{0x18874004,1},{0x18874008,3},{0x18874010,0x7ffff},{0x18874014,0xfffff}}) do
        p['cpucp_runtime_release_'..row[1]]={moduletype='qcom,retained-boot-control-v1',
            writable_mask=row[2],reset_value=0,
            target_socket={address=row[1],size=4,bind='&'..c..'.initiator_socket'}}
    end
    -- TZ 0x1573edbc..0x1573ede4 sets bit zero at 0x1887004c during cluster bring-up.
    -- CPUCP 0x1d808e70..0x1d808e78 clears bit zero at the adjacent 0x18870050.
    -- Only the APSS set word is observed. Retain it without a completion effect.
    p.apss_cluster_bringup={moduletype='qcom,retained-boot-control-v1',
        writable_mask=1,reset_value=0,
        target_socket={address=0x1887004c,size=4,bind='&'..a..'.initiator_socket'}}
    -- TZ 0x1573edf0..0x1573ee18 clears bit two at 0x17952000 after the startup words.
    -- The selected TZ and CPUCP images have no other access to this word.
    -- Retain it without a power or reset effect.
    p.apss_cluster_inhibit={moduletype='qcom,retained-boot-control-v1',
        writable_mask=4,reset_value=0,
        target_socket={address=0x17952000,size=4,bind='&'..a..'.initiator_socket'}}
    local outer_power_offsets={
        0x10,0x258,0x260,0x270,0x278,0x290,0x2b0,0x2c0,0x2c8,0x310,0x328,
        0x348,0x350,0x358,0x360,0x368,0x370,0x378,0x380,0x388,
        0x390,0x398,0x3a0,0x3a8,0x3b0,0x3b8,0x3c0,0x3c8,0x3d0,
        0x3f8,0x500,0x588,0x590,0x598,0x608,0x288,0x3f0,0x4e8,0x4f8,
        0x338,0x410,0x4f0,0x420,0x570,0x578,0x600}
    for lane=0,1 do
        local delta=lane*0x01000000
        -- CPUCP descriptor +0xb8 supplies the QRNG base for each lane.
        p['cluster_rng'..lane]={moduletype='qcom,qrng-v1',
            target_socket={address=0x198a0000+delta,size=0xc008,bind='&cpu_domain_router.initiator_socket'}}
        domain_ranges[#domain_ranges+1]={0x198a0000+delta,0xc008}
        -- The NCC_MPAM_S_LLC and NCC_MPAM_NS_LLC frames start at APSS_HM+0x02860000 and +0x02870000.
        -- CPUCP 0x1d80a004 and 0x1d80a024 reset partition zero. The MPAM tasks select other partitions.
        for _,frame in ipairs({{0x19860000,true,4,4},{0x19870000,false,16,6}}) do
            local address=frame[1]+delta
            p['cluster_mpam_'..string.format('%08x',address)]={moduletype='qcom,ncc-mpam-msc-v1',
                secure=frame[2],monitors=frame[3],filter_partid_bits=frame[4],
                target_socket={address=address,size=0x4000,bind='&cpu_domain_router.initiator_socket'}}
            domain_ranges[#domain_ranges+1]={address,0x4000}
        end
        local runtime_offsets={}
        -- CPUCP 0x812d658c initializes six ordinary records in either lane.
        -- The register count does not select the number of APSS cores.
        for core=0,5 do
            for _,offset in ipairs({0x08,0x10,0x18,0x20}) do
                runtime_offsets[#runtime_offsets+1]=core*0x40+offset
            end
        end
        domain_bank('runtime'..lane,0x19850000+delta,
            lane==0 and 0xa20 or 0x190,runtime_offsets,8)
        local window_offsets={}
        for index=1,(lane==0 and 8 or 4) do window_offsets[#window_offsets+1]=index*8 end
        domain_bank('window'..lane,0x18080000+lane*0x200000,
            lane==0 and 0x48 or 0x28,window_offsets,8)
        for _,offset in ipairs({0x168,0x178,0x180,0x188}) do
            domain_ranges[#domain_ranges+1]={0x19850000+delta+offset,8}
        end
        if lane==0 then
            for _,offset in ipairs({0xa10,0xa18}) do
                domain_ranges[#domain_ranges+1]={0x19850000+offset,8}
            end
        end
        domain_bank('route'..lane,0x198f0000+delta,0x98,{0x28,0x38,0x40},8)
        for _,offset in ipairs({0x48,0x50,0x58,0x60,0x68,0x70,0x78,0x80}) do
            domain_ranges[#domain_ranges+1]={0x198f0000+delta+offset,8}
        end
        domain_ranges[#domain_ranges+1]={0x198f0088+delta,8}
        domain_ranges[#domain_ranges+1]={0x198f0090+delta,8}
        domain_bank('power'..lane,0x19880000+delta,0x610,outer_power_offsets,8)
        -- XBL 0x1484ed84 sets NCC lock timeouts. 0x1484ed80 selects the AUX clock.
        domain_bank('bridge'..lane,0x199a2000+delta,0x38,{0x10,0x30},8)
        -- XBL source descriptors 0x148f1780 and 0x148f17b8 identify these PLLs.
        p['ncc_pll'..lane]={moduletype='qcom,ncc-zonda-pll-v1',
            target_socket={address=0x19a30000+delta,size=0x34,bind='&cpu_domain_router.initiator_socket'}}
        domain_ranges[#domain_ranges+1]={0x19a30000+delta,0x34}
        -- CPUCP runtime descriptors add an auxiliary Zonda PLL for each lane.
        p['ncc_aux_pll'..lane]={moduletype='qcom,ncc-zonda-pll-v1',
            target_socket={address=0x19aa1000+delta,size=0x34,bind='&cpu_domain_router.initiator_socket'}}
        domain_ranges[#domain_ranges+1]={0x19aa1000+delta,0x34}
        domain_bank('sequence'..lane,0x198b0000+delta,0x2300,{0},0x2300)
        domain_bank('fabric'..lane,0x19830000+delta,0x328,
            {0x80,0x88,0x90,0xa0,0xa8,0xc8,0xf8,0x100,0x108,0x118,0x150,0x168,0x320},8)
        -- CPUCP runtime descriptor +0x110: QMX policy and cluster table selectors.
        -- PDP writes +0x268 during mailbox setup. CPUCP snapshots it through ACEHR.
        domain_bank('cluster'..lane,0x19898000+delta,0x688,
            {0x268,0x5d0,0x630,0x638,0x640,0x648,0x650,0x658,0x660,0x678,0x680},8)
        -- Descriptor +0x100 and +0x108 select 64 primary and three secondary counters.
        -- Their packed records start at +0x400. CPUCP samples the counters every 500 ms.
        local table_offsets={}
        for index=0,63 do
            table_offsets[#table_offsets+1]=index*8
            table_offsets[#table_offsets+1]=0x400+index*8
        end
        for index=0,2 do
            table_offsets[#table_offsets+1]=0x1000+index*8
            table_offsets[#table_offsets+1]=0x1400+index*8
        end
        domain_bank('table'..lane,0x199e0000+delta,0x1418,table_offsets,8)
        -- SM8845 has a third table at descriptor +0x120. No completion is seeded.
        domain_bank('cluster_aux'..lane,0x19aa0000+delta,0xc98,
            {0x10,0x900,0x908,0x910,0x918,0xa10,0xa18,0xa20,0xa28},8)
        for _,offset in ipairs({0x130,0x1c0,0xc18}) do
            domain_ranges[#domain_ranges+1]={0x19aa0000+delta+offset,0x80}
        end
        -- PDP 0x20004ba0 and 0x20004c34 program the QMX DPM configuration words.
        for _,range in ipairs({{0x480,0x50},{0x5b8,0x30},{0xb50,8}}) do
            domain_ranges[#domain_ranges+1]={0x19aa0000+delta+range[1],range[2]}
        end
        domain_ranges[#domain_ranges+1]={0x1887c000+lane*4,4}
        local count=lane==0 and 6 or 2
        local core_offsets={}
        for index=0,count-1 do
            domain_ranges[#domain_ranges+1]={0x18879100+lane*0x80+index*0x10,4}
            core_offsets[#core_offsets+1]=index*0x100000+0x150
            for offset=0xb8,0x130,8 do
                core_offsets[#core_offsets+1]=index*0x100000+offset
            end
            for row=0,2 do
                for _,offset in ipairs({8,0x10,0x18,0x20}) do
                    core_offsets[#core_offsets+1]=index*0x100000+0xe000+row*0x40+offset
                end
            end
            -- CPUCP 0x1d8034a0 derives the per-core debug bank from descriptor +0x138.
            local debug_base=0x1d001000+lane*0x100000+index*0x1000
            for _,word in ipairs({{0,1},{0x150,3}}) do
                p['cpucp_core_debug_'..lane..'_'..index..'_'..word[1]]={
                    moduletype='qcom,retained-boot-control-v1',writable_mask=word[2],reset_value=0,
                    target_socket={address=debug_base+word[1],size=4,bind='&'..c..'.initiator_socket'}}
            end
            -- CPUCP reads the instantiated virtual CPU identity through its feature bank.
            for _,router in ipairs({a,c}) do
                p[router..'_core_identity_'..lane..'_'..index]={moduletype='addrtr',
                    mapped_base_addr=index*0x100000+0xd00,
                    target_socket={address=0x19030d00+delta+index*0x100000,size=8,
                        relative_addresses=false,bind='&'..router..'.initiator_socket'},
                    initiator_socket={bind='&cpu_domains.feature'..lane..'_socket'}}
            end
        end
        -- The per-core span covers the AMU page of every core after core zero.
        domain_bank('core'..lane,0x19032000+delta,(count-1)*0x100000+0xe0a8,core_offsets,8,1)
        -- TZ 0x1573e964..0x1573e984 and 0x1573dc74..0x1573dc90 store an eight-byte pair.
        -- The address is 0x19050000 + core*0x100000 + lane*0x1000000.
        -- The payload goes to +0x8 before the command word at +0x0.
        -- Only TZ touches this pair. The CPUCP path is not observed.
        -- The domain applies the pair as a core start request.
        for core=0,count-1 do
            local offset=core*0x100000
            p['apss_release_'..lane..'_'..core]={moduletype='addrtr',
                mapped_base_addr=offset,
                target_socket={address=0x19050000+delta+offset,size=0x10,
                    relative_addresses=false,bind='&'..a..'.initiator_socket'},
                initiator_socket={bind='&cpu_domains.release'..lane..'_socket'}}
        end
    end
    for _,router in ipairs({a,c}) do
        for _,range in ipairs(domain_ranges) do
            p[router..'_cpu_domain_'..range[1]]={moduletype='addrtr',mapped_base_addr=range[1],
                target_socket={address=range[1],size=range[2],relative_addresses=false,
                    bind='&'..router..'.initiator_socket'},
                initiator_socket={bind='&cpu_domain_router.target_socket'}}
        end
    end
    -- Evidence: TME 0x20d06e reads LCS_0. Value 5 selects EXT_OPERATIONAL.
    -- This virtual profile does not establish a physical device's fuse values.
    p.fuses={moduletype='qcom,sm8850-fuse-controller',lifecycle=5,qc_config1=0x88000000,
        jtag_id=0x002b30e1,oem_hw_id=0x51,oem_product_id=0,
        chip_id=hardware_version,serial_number=1,
        pte_row4_msb=0,
        tme_debug_vector0=0,tme_debug_vector1=0,
        qc_config0=0,qc_config4=0,qc_config5=0,qc_config6=0,qc_config7=0,secure_boot=0x130,
        secondary_hw_key_programmed=true,
        oem_config6=0,mrc0=0,
        qfprom_write_permissions_lsw=0x1800c060,qfprom_write_permissions_msw=0,
        qfprom_read_permissions_lsw=0x10000000,qfprom_read_permissions_msw=0,
        fuse_redundancy_enable0=0x08000000,fuse_redundancy_enable1=0,
        oem_config0=0x1000,oem_config2=0x40000000,patch_version=0,boot_config=0,antirollback0=0,
        oem_config5=0x0f,
        antirollback17=0,antirollback18=0,antirollback22=0,antirollback23=0,
        oem_security_policy_lsb=0,
        captured_boot_gpio=0x20,fec_error_status=0,rom_error_status=0,
        test_image_vector1=0,
        feature_config9=0,feature_config11=0,feature_config18=0,feature_config19=0,
        pte_identity_socket={address=0x021c20ec,size=4,bind='&'..t..'.initiator_socket'},
        rom_error_socket={address=0x021c2090,size=4,bind='&'..t..'.initiator_socket'},
        target_socket={address=0x021c8000,size=0x2000,bind='&'..t..'.initiator_socket'}}
    -- The optional calibration page holds raw QFPROM words for SM8850.
    -- A manifest supplies the path. SM8845 never sets this option.
    if o.calibration_fuses then
        assert(type(o.calibration_fuses)=='string', 'The calibration fuse page path is invalid.')
        p.fuses.calibration_fuses=o.calibration_fuses
    end
    -- XBL 0x1485eba8 reads the TLMM part number. 0x1485ebbc reads its revision.
    p.fuses.tlmm_identity_socket={address=0x0f1db014,size=8,bind='&'..a..'.initiator_socket'}
    -- TME 0x201b00 tests QC_CONFIG1 bit 30. TME 0x201746 tests QC_CONFIG6 bit 12.
    -- TME 0x2011ac tests QC_CONFIG5 bit 10 without disabling QTI authentication.
    -- Both views share fuse policy and mutable status. Unknown offsets still fail.
    p.tme_secondary_fuses={moduletype='addrtr',mapped_base_addr=0,
        target_socket={address=0x02308000,size=0x2000,relative_addresses=false,
            bind='&'..t..'.initiator_socket'},
        initiator_socket={bind='&fuses.target_socket'}}
    -- QC_CONFIG1 enables ROM boot and legacy OEM authentication, without test mode.
    -- OEM_CONFIG0 disables fuse programming and retains the default boot order.
    -- OEM_CONFIG5 enables rollback policy, including donor class 2 in bit 3.
    -- Both consolidated vectors leave debug enables and authentication bypass clear.
    -- QC_CONFIG6 keeps the ROM and access-control bypass bits clear.
    -- This virtual profile applies no fuse-backed ROM patches.
    -- FEATURE_CONFIG18 leaves BOOT-BLOCK disabled because the supplied package does not contain it.
    -- CPUCP 0x33576c and APBL 0x30d630 use the APSS fuse aperture.
    for _, router in ipairs({a,c}) do
        p[router..'_fuses']={moduletype='addrtr',mapped_base_addr=0x021c8000,
            target_socket={address=0x221c8000,size=0x2000,relative_addresses=false,
                bind='&'..router..'.initiator_socket'},
            initiator_socket={bind='&'..t..'.target_socket'}}
        -- The corrected manufacturing word shares the same fuse policy owner.
        p[router..'_pte_identity']={moduletype='addrtr',mapped_base_addr=0x021c20ec,
            target_socket={address=0x221c20ec,size=4,relative_addresses=false,
                bind='&'..router..'.initiator_socket'},
            initiator_socket={bind='&'..t..'.target_socket'}}
        p[router..'_rom_error']={moduletype='addrtr',mapped_base_addr=0x021c2090,
            target_socket={address=0x221c2090,size=4,relative_addresses=false,
                bind='&'..router..'.initiator_socket'},
            initiator_socket={bind='&'..t..'.target_socket'}}
    end
    -- This virtual identity matches the rehost profile, not a physical fuse capture.
    p.tcsr_identity={moduletype='qcom,tcsr-soc-identity-v1',
        hardware_version=hardware_version,emulation_type=0,sw_once_hardware_version=0,
        target_socket={address=0x01fc8000,size=8,bind='&'..a..'.initiator_socket'}}
    -- CPUCP runtime 0x812d401a reads the existing system identity.
    p.cpucp_tcsr_identity={moduletype='addrtr',mapped_base_addr=0,
        target_socket={address=0x01fc8000,size=8,relative_addresses=false,
            bind='&'..c..'.initiator_socket'},
        initiator_socket={bind='&tcsr_identity.target_socket'}}
    -- TME 0x20cb5a/0x20cb5c read SOC_HW_VERSION, 0x20cb80/0x20cb82 read SW_WONCE.
    -- OEM 0x42040a reads EMULATION_TYPE through the same TME aperture.
    -- Zero SW_WONCE retains the shared hardware identity. This is virtual policy.
    for _,offset in ipairs({0,4,8}) do
        p['tme_tcsr_identity_'..offset]={moduletype='addrtr',mapped_base_addr=offset,
            target_socket={address=0x05fc8000+offset,size=4,relative_addresses=false,
                bind='&'..t..'.initiator_socket'},
            initiator_socket={bind='&tcsr_identity.target_socket'}}
    end
    p.boot_info={moduletype='qcom,tcsr-boot-info-v1',
        failed_attempts=0,max_attempts=0,use_set_b=0,recovery_enabled=0,
        target_socket={address=0x01ff0000,size=4,bind='&'..a..'.initiator_socket'}}
    -- QSEE 0x15772f14 sets bit zero at 0x01fc0800 after secure-camera init.
    -- TZ 0x157bbea0 reads the TCSR word, preserves the other bits, and writes bit zero.
    -- The TCSR device struct at 0x157d60a8 holds base 0x01f40000 plus offset 0x80800.
    -- Only bit zero is modeled. The other bits are unmodeled and read zero.
    -- See docs/qcom/sm88xx/boot-dependencies.md for the read-only capture request.
    p.tcsr_seccam_control={moduletype='qcom,retained-boot-control-v1',
        writable_mask=1,reset_value=0,
        target_socket={address=0x01fc0800,size=4,bind='&'..a..'.initiator_socket'}}
    -- APSS 0x3094b4 starts the generator. 0x3094ec consumes its entropy.
    p.rng={moduletype='qcom,qrng-v1',
        target_socket={address=0x010c0000,size=0xc008,bind='&'..a..'.initiator_socket'}}
    for name,module in pairs(p) do
        if name~='rng' and type(module)=='table' then
            for _,socket in pairs(module) do
                if type(socket)=='table' and socket.bind=='&'..a..'.initiator_socket'
                    and type(socket.address)=='number' and type(socket.size)=='number' then
                    assert(socket.address>=0x010cc008 or socket.address+socket.size<=0x010c0000,
                        'The shared RNG aperture overlaps '..name)
                end
            end
        end
    end
    p.virtual_power={moduletype='qcom,virtual-pmic-power-v1'}
    p.ps_hold={moduletype='qcom,mpm-ps-hold-v1',initial_hold=0,
        ps_hold={bind='&virtual_power.ps_hold'},
        target_socket={address=0x0c264000,size=4,bind='&'..c..'.initiator_socket'}}
    p.cpucp_pad={moduletype='qcom,cpucp-pad-control-v1',reset_value=0,
        target_socket={address=0x0f1e0000,size=4,bind='&'..c..'.initiator_socket'}}
    p.cpucp_boot_router={moduletype='router'}
    p.cpucp_boot={moduletype='qcom,cpucp-boot-control-v1',args={'&cpucp'},
        hart_index=0,sifive_cease=true,legacy_rnmi=true,reset={bind='&cpucp.reset'},
        hart1_path='cpucp_hart1',reset1={bind='&cpucp_hart1.reset'},
        target_socket={address=0x17c40000,size=0x64,bind='&cpucp_boot_router.initiator_socket'},
        reset_socket={address=0x17c42000,size=4,bind='&cpucp_boot_router.initiator_socket'},
        setup_socket={address=0x18877600,size=0x98,bind='&cpucp_boot_router.initiator_socket'}}
    dofile(directory..'cpucp-runtime-controls.lua')(p,
        {boot='cpucp_boot',clock='cpucp_clock_root_3',lanes=3,routers={a,c}})
    dofile(directory..'cpucp-clock-gating.lua')(p,{routers={a,c}})
    dofile(directory..'cpucp-cluster-watchdog.lua')(p,{router=c})
    dofile(directory..'cpucp-subsystem.lua')(p,
        {router=c,cpu='cpucp',cpu1='cpucp_hart1',instance='cpucp_instance',apss_router=a})
    p.cpucp_subsystem.hart1_release={bind='&cpucp_boot.hart1_release'}
    dofile(directory..'cpucp-secure-notify.lua')(p,{apss_router=a})
    -- Both CPUCP images register these ACEHR control and descriptor apertures.
    for lane=0,1 do
        p['cpucp_acehr'..lane]={moduletype='qcom,cpucp-acehr-v1',
            dma_beat_hz=19200000,
            target_socket={address=0x18180000+lane*0x200000,size=0x8000,
                bind='&'..c..'.initiator_socket'},
            dma_socket={bind='&'..c..'.target_socket'},
            irq={bind='&cpucp_subsystem.source_'..(76+lane)}}
    end
    -- Selected ACEHR snapshot tables use this separate local SRAM range.
    memory('cpucp_acehr_sram',c,0x1d880000,0x8000)
    p.cpucp_acehr_sram.init_mem=true
    p.apss_acehr_sram={moduletype='addrtr',mapped_base_addr=0,
        target_socket={address=0x1d880000,size=0x8000,relative_addresses=false,
            bind='&'..a..'.initiator_socket'},
        initiator_socket={bind='&cpucp_acehr_sram.target_socket'}}
    -- Snapshot transfers reach the same GIC state as the APSS CPUs.
    for slot=0,7 do
        local address=0x17080000+slot*0x40000
        p['cpucp_gic_redist_'..slot]={moduletype='addrtr',mapped_base_addr=address,
            target_socket={address=address,size=0x20000,relative_addresses=false,
                bind='&'..c..'.initiator_socket'},
            initiator_socket={bind='&'..a..'.target_socket'}}
    end
    for _,router in ipairs({a,c}) do
        for _,range in ipairs({{0x17c40000,0x64},{0x17c42000,4},{0x18877600,0x98}}) do
            p[router..'_boot_'..range[1]]={moduletype='addrtr',mapped_base_addr=range[1],
                target_socket={address=range[1],size=range[2],relative_addresses=false,
                    bind='&'..router..'.initiator_socket'},
                initiator_socket={bind='&cpucp_boot_router.target_socket'}}
        end
    end
    p.apss_ps_hold={moduletype='addrtr',mapped_base_addr=0,
        target_socket={address=0x0c264000,size=4,relative_addresses=false,bind='&'..a..'.initiator_socket'},
        initiator_socket={bind='&ps_hold.target_socket'}}
    -- Evidence: TME ROM 0x213018..0x213020 bounds this shared memory.
    -- APBL 0x30f0b4 and 0x30f11c clear its two mailbox rings.
    memory('tme_mailbox',t,0x02000000,0x3800)
    p.tme_mailbox.dmi_allow=false
    -- ROM 0x205220..0x205252 updates these software state banks through write aliases.
    p.tme_software_state={moduletype='qcom,sm8850-tme-software-state-v1',
        low_reset=0,high_reset=0,handoff_reset=0,
        state_socket={address=0x00c02008,size=8,bind='&'..t..'.initiator_socket'},
        update_socket={address=0x00c0022c,size=8,bind='&'..t..'.initiator_socket'},
        handoff_socket={address=0x00c02018,size=4,bind='&'..t..'.initiator_socket'}}
    -- APSS 0x30de68 updates the same handoff word through its external view.
    p.apss_tme_handoff={moduletype='addrtr',mapped_base_addr=0x00c02018,
        target_socket={address=0x20c02018,size=4,relative_addresses=false,
            bind='&'..a..'.initiator_socket'},
        initiator_socket={bind='&tme_external_router.target_socket'}}
    -- Virtual bank-six status follows the response header that the real TME publishes.
    p.tme_boot_status={moduletype='qcom,tme-boot-status-v1',interface_version=0,
        hardware_version=hardware_version,
        target_socket={address=0x22236000,size=0x50,bind='&'..a..'.initiator_socket'},
        mailbox_socket={bind='&tme_mailbox.target_socket'},
        handoff_socket={bind='&tme_handoff_reader.target_socket'}}
    p.tme_handoff_reader={moduletype='addrtr',mapped_base_addr=0x00c02018,
        target_socket={address=0,size=4,relative_addresses=false},
        initiator_socket={bind='&'..t..'.target_socket'}}
    p.tme_boot_status_view={moduletype='addrtr',mapped_base_addr=0,
        target_socket={address=0x02236000,size=0x50,relative_addresses=false,
            bind='&'..t..'.initiator_socket'},
        initiator_socket={bind='&tme_boot_status.target_socket'}}
    p.apss_mailbox={moduletype='addrtr',mapped_base_addr=0x02000000,
        target_socket={address=0x22000000,size=0x3800,relative_addresses=false,
            bind='&'..a..'.initiator_socket'},
        initiator_socket={bind='&'..t..'.target_socket'}}
    -- ROM 0x211e34 translates the sequencer's input and output payloads.
    p.tme_sequencer={moduletype='qcom,tme-sequencer-v1',policy_selection=0,
        target_socket={address=0x02230000,size=0x168,bind='&'..t..'.initiator_socket'},
        policy_socket={address=0x00c00200,size=4,bind='&'..t..'.initiator_socket'},
        input_socket={address=0x02281000,size=0x1000,bind='&'..t..'.initiator_socket'},
        dma_socket={bind='&sequencer_dma_protection.target_socket'},
        output_socket={address=0x02289000,size=0x1000,bind='&'..t..'.initiator_socket'}}
    -- sequencer_ram.elf has a 4 KiB program segment at this TME address.
    memory('sequencer_program',t,0x022a4000,0x1000)
    p.sequencer_program.dmi_allow=false
    p.sequencer_program.init_mem=true
    p.tme_boot_status.program_socket={bind='&sequencer_program.target_socket'}
    p.tme_boot_status.input_socket={bind='&tme_boot_input_reader.target_socket'}
    p.tme_boot_input_reader={moduletype='addrtr',mapped_base_addr=0x02281000,
        target_socket={address=0,size=0x1000,relative_addresses=false},
        initiator_socket={bind='&'..t..'.target_socket'}}
    p.apss_sequencer_program={moduletype='addrtr',mapped_base_addr=0x022a4000,
        target_socket={address=0x222a4000,size=0x1000,relative_addresses=false,
            bind='&'..a..'.initiator_socket'},
        initiator_socket={bind='&tme_external_router.target_socket'}}
    p.apss_sequencer_input={moduletype='addrtr',mapped_base_addr=0x02281000,
        target_socket={address=0x22281000,size=0x1000,relative_addresses=false,
            bind='&'..a..'.initiator_socket'},
        initiator_socket={bind='&apss_sequencer_input_protection.target_socket'}}
    p.apss_sequencer_input_relative={moduletype='addrtr',mapped_base_addr=0,
        target_socket={address=0x02281000,size=0x1000,relative_addresses=false},
        initiator_socket={bind='&tme_sequencer.load_socket'}}
    -- Evidence: TME 0x20d0ea..0x20d196 programs the first protection bank.
    -- TME 0x20d1b2..0x20d1fc programs the second bank.
    for i, address in ipairs({0x05f00000,0x000c28000}) do
        p['tme_protection'..i]={moduletype='qcom,sm8850-tme-protection',
            region_count=16,address_bits=32,alignment_bits=12,mode_reset=0,
            target_socket={address=address,size=0x2000,bind='&'..t..'.initiator_socket'}}
    end
    p.tme_protection1.mode_socket={address=0x05fd0000,size=4,bind='&'..t..'.initiator_socket'}
    -- Virtual requester domains follow the ROM permission table at 0x212fd4.
    -- The external APSS view uses QAD zero. The local TME view uses QAD one.
    -- TCSR occupies the one-MiB aperture at system address 0x01f00000.
    -- The controller compares offsets within that aperture.
    local function protection_port(name,controller,cpu,qad,downstream,client_base)
        p[name]={moduletype='qcom,tme-protection-port-v1',
            args={'&'..controller,'&'..cpu},qad=qad,secure_unspecified=cpu=='tme',
            protected_base=client_base or 0,protected_size=client_base and 0x100000 or 0,
            initiator_socket={bind='&'..downstream..'.target_socket'}}
    end
    protection_port('apss_sequencer_input_protection','tme_protection2','apss',0,
        'apss_sequencer_input_relative')
    -- ROM 0x20efd6 supplies system addresses, including the private TME hash tail.
    -- Address translation must retain the sequencer's TME requester domain.
    p.sequencer_dma_router={moduletype='router'}
    protection_port('sequencer_dma_protection','tme_protection1','tme',1,'sequencer_dma_router',0x01f00000)
    protection_port('sequencer_local_protection','tme_protection2','tme',1,t)
    p.sequencer_local_router={moduletype='router'}
    p.sequencer_local_protection.target_socket={address=0x400000,size=0x5c000,
        relative_addresses=false,bind='&sequencer_local_router.initiator_socket'}
    p.sequencer_dma_ram={moduletype='addrtr',mapped_base_addr=0x400000,
        target_socket={address=0x20400000,size=0x5c000,relative_addresses=false,
            bind='&sequencer_dma_router.initiator_socket'},
        initiator_socket={bind='&sequencer_local_router.target_socket'}}
    -- Donor 0x21dd86 submits the digest through its local TME address.
    p.sequencer_dma_local_ram={moduletype='addrtr',mapped_base_addr=0x400000,
        target_socket={address=0x400000,size=0x5c000,relative_addresses=false,
            bind='&sequencer_dma_router.initiator_socket'},
        initiator_socket={bind='&sequencer_local_router.target_socket'}}
    -- The image hash request reads the UFS-loaded segment at 0x14827000.
    p.sequencer_sram={moduletype='addrtr',mapped_base_addr=0,
        target_socket={address=0x14800000,size=0x280000,relative_addresses=false,
            bind='&sequencer_dma_router.initiator_socket'},
        initiator_socket={bind='&apss_sram.target_socket'}}
    for _,cpu in ipairs({'apss','cpucp','tme'}) do
        local port=cpu..'_protection_port'
        protection_port(port,'tme_protection1',cpu,cpu=='tme' and 1 or 0,cpu..'_router',
            cpu=='tme' and 0x05f00000 or 0x01f00000)
        p[cpu..'_access'].initiator_socket.bind='&'..port..'.target_socket'
    end
    protection_port('tme_local_protection','tme_protection2','tme',1,t)
    p.tme_protection_port.initiator_socket.bind='&tme_local_protection.target_socket'
    p.tme_external_router={moduletype='router'}
    protection_port('tme_external_protection','tme_protection2','apss',0,t)
    p.tme_external_protection.target_socket={address=0,size=0x100000000,
        relative_addresses=false,bind='&tme_external_router.initiator_socket'}
    p.apss_tme_ram.initiator_socket.bind='&tme_external_router.target_socket'
    p.apss_mailbox.initiator_socket.bind='&tme_external_router.target_socket'
    dofile(directory..'tme-crashdump-policy.lua')(p)
    local irq=dofile(directory..'../apss-gic.lua')(p,
        {cpus=apss_cpus,group='apss_group',instance='apss_instance',router=a})
    -- OEM SEC selects this SE. The local Kaanapali DT assigns SPI 835.
    p.qup_common_1={moduletype='qcom,geni-common-v1',
        target_socket={address=0x00ac0000,size=0x1000,bind='&'..a..'.initiator_socket'}}
    p.uart={moduletype='qcom,geni-uart-v1',tx_log=o.trace..'-uart.log',
        se_socket={address=0x00a9c000,size=0x3000,bind='&'..a..'.initiator_socket'},
        irq={bind='&apss_gic.spi_in_835'}}
    -- Canoe DT timer@17810000 assigns physical frame interrupts to SPI 8 and SPI 9.
    p.system_timer.irq_0={bind='&apss_gic.spi_in_8'}
    p.system_timer.irq_1={bind='&apss_gic.spi_in_9'}
    p.system_timer.irq_2={bind='&apss_gic.spi_in_10'}
    -- QSEE 0x157666d8 registers architectural INTIDs 43 and 44 without an SPI offset.
    p.system_timer.irq_3={bind='&apss_gic.spi_in_11'}
    p.system_timer.irq_4={bind='&apss_gic.spi_in_12'}
    p.system_timer.irq_5={bind='&apss_gic.spi_in_13'}
    p.ufs_ram_router={moduletype='router'}
    p.ufs_sram={moduletype='addrtr',mapped_base_addr=0,
        target_socket={address=0x14800000,size=0x280000,relative_addresses=false,
            bind='&ufs_ram_router.initiator_socket'},
        initiator_socket={bind='&apss_sram.target_socket'}}
    -- The OEM loader submits UFS DMA into TME staging RAM at 0x204084d0.
    p.ufs_tme_ram={moduletype='addrtr',mapped_base_addr=0x400000,
        target_socket={address=0x20400000,size=0x5c000,relative_addresses=false,
            bind='&ufs_ram_router.initiator_socket'},
        initiator_socket={bind='&tme_external_router.target_socket'}}
    protection_port('ufs_dma_protection','tme_protection1','apss',0,'ufs_ram_router',0x01f00000)
    -- QSEE 0x157485a4..0x157485d8 accepts 0x100..0x8000 RPMB sectors from UFSDxe.
    -- The default profile declares 4 MiB. An explicit profile can select another geometry.
    dofile(directory..'ufs.lua')(p,{router=a,ram_router='ufs_dma_protection',
        manager='qemu_inst_mgr',irq=irq,virtual_phy=true,luns=o.luns,
        boot_enable=o.boot_enable,boot_lun_en=o.boot_lun_en,boot_lun_ids=o.boot_lun_ids,
        rpmb_size_kib=o.rpmb_size_kib or 4096,rpmb_state_file=o.rpmb_state_file})
    -- QSEE 0x1577b1d8 uses the UFS ICE error branch.
    -- 0x1577b238 writes zero to 0x01d8e204.
    -- 0x1577b240 reads 0x01d8e200 and writes its top two bits to 0x01d8e208.
    -- 0x1577b298..b0 read the ICE version at 0x01d88008 three times.
    -- No ICE engine, key slot, status producer, or interrupt source is modeled.
    -- The read words are virtual zero. Unmapped key registers fault.
    p.ufs_ice_error_status={moduletype='qcom,retained-boot-control-v1',
        writable_mask=0,reset_value=0,
        target_socket={address=0x01d8e200,size=4,bind='&'..a..'.initiator_socket'}}
    p.ufs_ice_error_clear={moduletype='qcom,retained-boot-control-v1',
        writable_mask=0,reset_value=0,
        target_socket={address=0x01d8e204,size=4,bind='&'..a..'.initiator_socket'}}
    p.ufs_ice_error_mask={moduletype='qcom,retained-boot-control-v1',
        writable_mask=0xc0000000,reset_value=0,
        target_socket={address=0x01d8e208,size=4,bind='&'..a..'.initiator_socket'}}
    p.ufs_ice_version={moduletype='qcom,retained-boot-control-v1',
        writable_mask=0,reset_value=0,
        target_socket={address=0x01d88008,size=4,bind='&'..a..'.initiator_socket'}}
    -- DMA uses the same backing as APSS. No second copy of shared storage exists.
    local boot_memory={
        {'apss_imem',0x14680000,0x2c000},
        {'soccp_debug_sram',0x00d20000,0xc000},
        {'cpucp_image',0x1d800000,0x40000},
        {'shrm_iram',0x31020000,0x28000},
        {'shrm_dmem',0x31048000,0x18000}}
    for _,region in ipairs(boot_memory) do
        for _,router in ipairs({'sequencer_dma_router','ufs_ram_router',t}) do
            p[router..'_'..region[1]]={moduletype='addrtr',mapped_base_addr=0,
                target_socket={address=region[2],size=region[3],relative_addresses=false,
                    bind='&'..router..'.initiator_socket'},
                initiator_socket={bind='&'..region[1]..'.target_socket'}}
        end
        -- CPUCP uses the same IMEM, image, and remote-load SRAM as every DMA producer.
        p['cpucp_router_'..region[1]]={moduletype='addrtr',mapped_base_addr=0,
            target_socket={address=region[2],size=region[3],relative_addresses=false,
                bind='&'..c..'.initiator_socket'},
            initiator_socket={bind='&'..region[1]..'.target_socket'}}
        -- Reject any competing socket on each affected bus, including future additions.
        for _,router in ipairs({a,c,t,'sequencer_dma_router','ufs_ram_router'}) do
            local found=false
            for name,module in pairs(p) do
                if type(module)=='table' then
                    for socket_name,socket in pairs(module) do
                        if type(socket)=='table' and socket.bind=='&'..router..'.initiator_socket'
                            and socket.address and socket.size
                            and socket.address<region[2]+region[3]
                            and region[2]<socket.address+socket.size then
                            assert(not found and socket.address==region[2] and socket.size==region[3],
                                'Overlapping boot SRAM mapping: '..name..'.'..socket_name)
                            found=true
                        end
                    end
                end
            end
            assert(found, 'Missing boot SRAM mapping.')
        end
    end
    return p
end
