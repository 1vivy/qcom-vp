-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- QSEE direct controls and initializer-called clock/power descriptor groups.
local directory=assert(debug.getinfo(1,'S').source:match('^@(.*/)'))
local append_vote=dofile(directory..'clock-branch-votes.lua')
return function(p, oem)
    assert(oem=='sm8850' or oem=='sm8845', 'A selected QSEE clock OEM is required.')
    assert(p.cpucp_clock_router and p.apss_router and p.cpucp_router and p.xbl_boot_clock_reference,
        'The reset platform clock routers are required.')
    assert(p.dcp_bootstrap, 'The existing DCP bootstrap owner is required.')

    local routers={'cpucp_clock_router','apss_router','cpucp_router'}
    local function overlap(router,address,size)
        for name,module in pairs(p) do
            if type(module)=='table' then
                for _,socket in pairs(module) do
                    if type(socket)=='table' and socket.bind=='&'..router..'.initiator_socket'
                        and type(socket.address)=='number' and type(socket.size)=='number'
                        and address<socket.address+socket.size and socket.address<address+size then
                        return name,module,socket
                    end
                end
            end
        end
    end
    local function insert(name,module)
        assert(p[name]==nil, 'Duplicate QSEE clock module: '..name)
        p[name]=module
    end
    local function map(name,address,size,module)
        for _,router in ipairs(routers) do
            local collision=overlap(router,address,size)
            if collision then
                error(string.format('QSEE clock target 0x%x overlaps %s in %s',address,collision,router))
            end
        end
        module.target_socket={address=address,size=size,bind='&cpucp_clock_router.initiator_socket'}
        insert(name,module)
        for _,router in ipairs({'apss_router','cpucp_router'}) do
            insert(name..'_'..router,{moduletype='addrtr',mapped_base_addr=address,
                target_socket={address=address,size=size,relative_addresses=false,
                    bind='&'..router..'.initiator_socket'},
                initiator_socket={bind='&cpucp_clock_router.target_socket'}})
        end
    end
    -- QSEE gcc_gpll0/gcc_gpll7 records (SM8850 0x157f6458/0x157f68d0, SM8845
    -- 0x157e9860/0x157e9d10) vote bits 0 and 7 at 0x153058 and poll PLL_MODE bit 30.
    -- The shared PLL owner keeps this independent master vote in its fourth slot.
    assert(p.gcc_plls and p.gcc_pll_router, 'The shared GCC PLL owner is required.')
    p.gcc_plls.vote_count=math.max(p.gcc_plls.vote_count,4)
    p.gcc_plls.vote_socket.size=math.max(p.gcc_plls.vote_socket.size,0xf004)
    for _,router in ipairs({'apss_router','cpucp_router'}) do
        local collision=overlap(router,0x153058,4)
        if collision then error('The QSEE GPLL vote overlaps '..collision..' in '..router) end
        insert('qsee_gpll_vote_'..router,{moduletype='addrtr',mapped_base_addr=0x161020,
            target_socket={address=0x153058,size=4,relative_addresses=false,
                bind='&'..router..'.initiator_socket'},
            initiator_socket={bind='&gcc_pll_router.target_socket'}})
    end
    local function owner(address,size)
        for name,module in pairs(p) do
            if type(module)=='table' and module.moduletype~='addrtr' then
                local socket=module.target_socket
                if type(socket)=='table' and socket.address==address then
                    for _,router in ipairs(routers) do
                        if socket.bind=='&'..router..'.initiator_socket' then
                            assert(socket.size>=size, 'QSEE clock owner is too small: '..name)
                            for _,view in ipairs(routers) do
                                if not overlap(view,address,socket.size) then
                                    insert(name..'_qsee_'..view,{moduletype='addrtr',
                                        mapped_base_addr=address,
                                        target_socket={address=address,size=socket.size,
                                            relative_addresses=false,
                                            bind='&'..view..'.initiator_socket'},
                                        initiator_socket={bind='&'..router..'.target_socket'}})
                                end
                            end
                            return module,name
                        end
                    end
                end
            end
        end
    end
    local function ensure_branch(address,status_on_high)
        local module=owner(address,4)
        if module then
            if module.moduletype=='qcom,retained-boot-control-v1' then
                assert(module.writable_mask==0x200000 or
                    ((address==0x14b004 or address==0x14b008) and module.writable_mask==1),
                    'Unexpected retained QSEE branch fields')
                module.moduletype='qcom,clock-branch-v1'
                module.retained_mask=module.writable_mask & ~1
                module.writable_mask=nil
                module.reset_value=nil
                module.source_path='xbl_boot_clock_reference'
            end
            assert(module.moduletype=='qcom,clock-branch-v1',
                string.format('QSEE clock target 0x%x has a non-branch owner',address))
            if status_on_high then module.status_on_high=true end
            return module
        end
        module={moduletype='qcom,clock-branch-v1',
            source_path='xbl_boot_clock_reference',status_on_high=status_on_high or false}
        map('qsee_clock_branch_'..address,address,4,module)
        return module
    end
    local function ensure_domain(address,retained)
        local module=owner(address,4)
        if module then
            if module.moduletype=='qcom,retained-boot-control-v1' then
                assert(module.writable_mask==1 and module.reset_value==1,
                    'Unexpected retained QSEE domain fields')
                module.moduletype='qcom,gdsc-v1'
                module.writable_mask=nil
                module.reset_value=nil
                module.virtual_supply=true
            end
            assert(module.moduletype=='qcom,gdsc-v1',
                string.format('QSEE power target 0x%x has a non-GDSC owner',address))
            module.retained_mask=(module.retained_mask or 0) | (retained or 0)
            return
        end
        local domain={moduletype='qcom,gdsc-v1',virtual_supply=true,retained_mask=retained or 0}
        local size=8
        -- Linux gpucc-canoe.c declares gpu_cc_cx_gdsc with gds_hw_ctrl at GDSCR + 0x14.
        if address==0x03d99080 then size=0x18; domain.hw_ctrl_offset=0x14 end
        map('qsee_power_domain_'..address,address,size,domain)
    end
    local function ensure_control(address,mask)
        for _,router in ipairs(routers) do
            local name,module,socket=overlap(router,address,4)
            if name then
                if socket.address<=address and address+4<=socket.address+socket.size then return end
                error(string.format('QSEE control 0x%x partially overlaps %s',address,name))
            end
        end
        map('qsee_clock_control_'..address,address,4,
            {moduletype='qcom,retained-boot-control-v1',writable_mask=mask,reset_value=0})
    end

    ensure_branch(0x00185000,true)
    for _,word in ipairs({{0x03d990d4,0x09c07004},{0x03d990e8,0x09c00004}}) do
        local branch=ensure_branch(word[1],false)
        branch.retained_mask=(branch.retained_mask or 0) | word[2]
    end
    ensure_domain(0x03d99080,0x100)
    -- QSEE gpucc_gpu_smmu_clk records (SM8850 0x157f41c0, SM8845 0x157e7808) enable
    -- 0x03d95000 after the gcc_gpu_smmu_vote_clk and gcc_mmu_tcu_clk branches.
    -- The descriptor has no RCG parent, like those two XBL SMMU branches.
    ensure_branch(0x03d95000,false)
    ensure_domain(0x03d68024,0x100)

    local controls={
        {0x00132004,0x8fd0000f},
        {0x03d94000,0x804},{0x03d94004,0x804},
        {0x09bb2000,0x804},{0x01ff7000,1},
        {0x00147174,0x8fd0000e},{0x00147014,0x8fd0000e},
        {0x01fd3300,1},{0x0014d000,0x8fd00004},{0x0011828c,0xf},
        -- Descriptor setup below promotes the existing DDR gate controls.
    }
    if oem=='sm8850' then
        controls[#controls+1]={0x06e1f100,0}
        controls[#controls+1]={0x00172008,0x8fd0000f}
        controls[#controls+1]={0x0017200c,0x8fd00005}
    else
        controls[#controls+1]={0x00147178,0x8fd0000e}
    end
    for _,word in ipairs(controls) do ensure_control(word[1],word[2]) end

    -- Null-terminated descriptors reached by the selected initializer.
    -- An explicit virtual source replaces absent consumers, not a physical mux.
    local direct={
        0x174044,0x14b004,0x14b008,0x114014,0x147030,0x17707c,
        0x0788105c,0x07881054,0x0788107c,0x07881074,
        0x171004,0x126014,0x126028,0x0958e37c,0x0958e348,0x0958e36c,
        0x0958e2bc,0x0958e16c,0x0958d020,0x0958e19c,0x0958e15c,
        0x0958e198,0x0958e188,0x126004,0x12603c,
        0x12701c,0x09baa0c4,0x127004,
        0x06bd5018,0x06bd502c,0x06e1100c,0x06e11300,0x06e21038,
        0x06be7000,0x06bd1000,0x07f06000,0x147000,
    }
    if oem=='sm8850' then
        direct[#direct+1]=0x116014
        direct[#direct+1]=0x115008
    end
    -- These gates remain inside the existing DCP bank owner.
    for _,address in ipairs({0x09bb4040,0x09bae004}) do
        local _,module=overlap('apss_router',address,4)
        assert(module==p.dcp_bootstrap, 'The existing DCP gate owner is required.')
    end
    for _,address in ipairs(direct) do ensure_branch(address,false) end
    for _,address in ipairs({0x16b004,0x177004,0x139004,0x110004,
        0x0958e334,0x06e11090,0x06e20090}) do ensure_domain(address) end

    -- Keep each QSEE vote word separate from the XBL/RPM vote bank.
    local votes={
        {0x14101c,0x153038,0x8},{0x141004,0x153038,0x20},
        {0x123148,0x153040,0x80000},{0x1232a0,0x153048,1},
        {0x1233f8,0x153050,0x400},{0x123298,0x153048,4},
        {0x1233f0,0x153050,0x100},{0x123144,0x153040,0x200000},
        {0x12329c,0x153048,2},{0x1233f4,0x153050,0x200},
        -- QUP common hardware arrays from QUPV3_HW_PROP. QUP1 enables
        -- wrap1_core_2x, wrap1_core, wrap_1_m_axi, and wrap_1_s_ahb. QUP2
        -- and QUP3 enable the same four clocks for their wraps.
        {0x12315c,0x153040,0x40000},{0x123140,0x153040,0x100000},
        {0x1232b4,0x153048,0x8},{0x12340c,0x153050,0x800},
        {0x110050,0x153038,0x100000},{0x110058,0x153040,0x40},
        {0x0788301c,0x078a0000,0x400},{0x0789a01c,0x078a0000,0x400000},
        {0x07883030,0x078a0000,0x800},{0x0789a030,0x078a0000,0x800000},
        {0x07881050,0x078a0000,2},{0x0788104c,0x078a0000,1},
        {0x07881070,0x078a0000,0x2000},{0x0788106c,0x078a0000,0x1000},
    }
    if oem=='sm8850' then
        votes[#votes+1]={0x123550,0x153050,0x1000000}
        votes[#votes+1]={0x123548,0x153050,0x400000}
        votes[#votes+1]={0x12354c,0x153050,0x800000}
        -- QUP4 is present only on SM8850. The i2chub arrays likewise exist
        -- only in the selected SM8850 QUPV3_HW_PROP.
        votes[#votes+1]={0x123564,0x153050,0x2000000}
        votes[#votes+1]={0x123004,0x153040,0x100}
        votes[#votes+1]={0x123000,0x153040,0x80}
        -- Per-engine SE clocks from the selected devcfg /dev/buses/qupac
        -- permission entries with the enable bit. Descriptors 0x157f4bd8
        -- through 0x157f5318. The two i2chub permission entries select the
        -- i2c S0 and S1 clocks at descriptors 0x157f4998 and 0x157f49d8.
        for _,clock in ipairs({
            {0x118004,0x153040,0x400000},{0x118140,0x153040,0x800000},
            {0x11827c,0x153040,0x1000000},{0x1183cc,0x153040,0x4000000},
            {0x118508,0x153040,0x8000000},
            {0x117004,0x153040,0x400},{0x117020,0x153040,0x800},
            {0x11e004,0x153048,0x10},{0x11e140,0x153048,0x20},
            {0x11e4f4,0x153048,0x100},
            {0x1a8140,0x153050,0x2000},{0x1a827c,0x153050,0x4000},
            {0x1a83b8,0x153050,0x8000},{0x1a84f4,0x153050,0x10000},
            {0x1a8630,0x153050,0x20000},
            {0x1a9004,0x153050,0x4000000},{0x1a9140,0x153050,0x8000000},
            {0x1a927c,0x153050,0x10000000},{0x1a93b8,0x153050,0x20000000},
        }) do votes[#votes+1]=clock end
    else
        votes[#votes+1]={0x1aa134,0x153050,0x80}
        votes[#votes+1]={0x1aa15c,0x153060,4}
        votes[#votes+1]={0x1aa158,0x153060,2}
        -- Per-engine SE clocks from the selected SM8845 permission set.
        -- Descriptors 0x157e8120 through 0x157e8720.
        for _,clock in ipairs({
            {0x118004,0x153040,0x400000},{0x118140,0x153040,0x800000},
            {0x11827c,0x153040,0x1000000},{0x1183cc,0x153040,0x4000000},
            {0x11e004,0x153048,0x10},{0x11e140,0x153048,0x20},
            {0x11e27c,0x153048,0x40},{0x11e3b8,0x153048,0x80},
            {0x11e4f4,0x153048,0x100},{0x11e76c,0x153048,0x400},
            {0x1a8004,0x153050,0x1000},{0x1a8140,0x153050,0x2000},
            {0x1a827c,0x153050,0x4000},{0x1a83b8,0x153050,0x8000},
        }) do votes[#votes+1]=clock end
    end
    local banks={}
    for _,vote in ipairs(votes) do banks[vote[2]]=(banks[vote[2]] or 0) | vote[3] end
    for address,mask in pairs(banks) do
        local module,name=owner(address,4)
        if module then
            assert(address==0x153038 and module.moduletype=='qcom,fault-monitor-setup-v1'
                and module.configuration_offsets=='0x0', 'Unexpected QSEE vote word owner')
            module.moduletype='qcom,clock-vote-bank-v1'
            module.configuration_offsets=nil
            module.mask0=mask
        else
            name='qsee_clock_votes_'..address
            map(name,address,4,{moduletype='qcom,clock-vote-bank-v1',mask0=mask})
        end
        banks[address]=name
    end
    for _,vote in ipairs(votes) do
        local module=ensure_branch(vote[1],false)
        append_vote(module,banks[vote[2]],0,vote[3],vote[1])
    end
    return p
end
