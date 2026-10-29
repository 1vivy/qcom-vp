-- SPDX-License-Identifier: BSD-3-Clause-Clear
local directory=assert(debug.getinfo(1,'S').source:match('^@(.*/)'))
local inventories=dofile(directory..'qsee-fault-inventory.lua')
return function(p,soc)
    local inventory=assert(inventories[soc], 'Unsupported fault monitor platform')
    assert(p.apss_router and p.llcc and p.ddr_command and p.ddr_phy_llc,
        'Fault setup requires the existing APSS, LLCC, and DDR command owners.')
    assert(p.llcc.moduletype=='qcom,llcc-v6' and
        p.ddr_command.moduletype=='qcom,ddr-command-engine-v1' and
        p.ddr_phy_llc.moduletype=='qcom,ddr-phy-program-v1', 'Unexpected DDR fault owner')
    local bind='&apss_router.initiator_socket'
    local pending={}
    local function add(name,base,size,offsets)
        assert(not p[name], 'Duplicate fault setup owner: '..name)
        for owner,module in pairs(p) do
            if type(module)=='table' then
                for _,target in pairs(module) do
                    if type(target)=='table' and target.bind==bind and
                        type(target.address)=='number' and type(target.size)=='number' then
                        assert(base>=target.address+target.size or target.address>=base+size,
                            string.format('Fault setup 0x%x overlaps %s',base,owner))
                    end
                end
            end
        end
        pending[#pending+1]={name=name,base=base,size=size,offsets=offsets}
    end
    -- Page-sized source banks contain only explicit retained words, not RAM.
    local pages={}
    local function word(address)
        local base=address-address%0x1000
        pages[base]=pages[base] or {}
        pages[base][address-base]=true
    end
    for _,op in ipairs(inventory.global) do word(op[1]) end
    for _,monitor in ipairs(inventory.monitors) do
        for _,op in ipairs(monitor.operations) do word(op[1]) end
    end
    for _,op in ipairs(inventories.common) do
        if op.group=='cmsr' then word(op.address) end
    end
    if soc=='sm8845' then
        -- Opaque retained words. No forced ID, ready bit, or conditional branch.
        for _,address in ipairs({0x17e80010,0x17e80030,0x17e80018,
            0x01680018,0x01500018,0x01600018,0x31200018}) do word(address) end
    end
    -- QHEE reads the GEM_NOC fault summary word 0x31205020 during its interrupt
    -- setup at image offset 0x49084. No silicon capture gives a value for this
    -- word, so it keeps the declared virtual cold value zero of this bank.
    -- The model creates no fault, no source identifier, and no completion.
    word(0x31205020)
    word(0x31205028)
    for base,words in pairs(pages) do
        local offsets={}
        for offset in pairs(words) do offsets[#offsets+1]=offset end
        table.sort(offsets)
        for i,offset in ipairs(offsets) do offsets[i]=string.format('0x%x',offset) end
        add(string.format('qsee_fault_%08x',base),base,0x1000,table.concat(offsets,','))
    end
    -- These exact words have no prior clock owner in the selected helpers.
    -- Use one retained owner on the clock router and aliases for its other views.
    local control_router=p.cpucp_clock_router and 'cpucp_clock_router' or 'apss_router'
    for _,address in ipairs({0x00153038,0x00145010}) do
        local name=string.format('qsee_fault_control_%08x',address)
        add(name,address,4,'0x0')
        for _,router in ipairs({'cpucp_clock_router','cpucp_router'}) do
            if p[router] then
                for owner,module in pairs(p) do
                    if type(module)=='table' then
                        for _,target in pairs(module) do
                            if type(target)=='table' and target.bind=='&'..router..'.initiator_socket'
                                and type(target.address)=='number' and type(target.size)=='number' then
                                assert(address<target.address or address>=target.address+target.size,
                                    'Fault control already owned by '..owner)
                            end
                        end
                    end
                end
            end
        end
    end
    -- Extend declarations only. Do not create, reset, or replace DDR state.
    local function declare(module,extra)
        local seen={}
        for token in (module.configuration_offsets or ''):gmatch('[^,]+') do
            seen[assert(tonumber(token))]=true
        end
        local words={}
        for offset in pairs(seen) do words[#words+1]=offset end
        for _,offset in ipairs(extra) do
            if not seen[offset] then words[#words+1]=offset; seen[offset]=true end
        end
        table.sort(words)
        for i,offset in ipairs(words) do words[i]=string.format('0x%x',offset) end
        module.configuration_offsets=table.concat(words,',')
    end
    declare(p.llcc,{0x68058,0x6800c,0x70000,0x70024})
    declare(p.ddr_phy_llc,{0x9118,0x9114,0x52a4})
    for _,row in ipairs(pending) do
        local router=row.size==4 and control_router or 'apss_router'
        p[row.name]={moduletype='qcom,fault-monitor-setup-v1',configuration_offsets=row.offsets,
            target_socket={address=row.base,size=row.size,bind='&'..router..'.initiator_socket'}}
        if row.size==4 then
            for _,view in ipairs({'apss_router','cpucp_router'}) do
                if p[view] and view~=router then
                    p[row.name..'_'..view]={moduletype='addrtr',mapped_base_addr=row.base,
                        target_socket={address=row.base,size=4,relative_addresses=false,
                            bind='&'..view..'.initiator_socket'},
                        initiator_socket={bind='&'..router..'.target_socket'}}
                end
            end
        end
    end
    return p
end
