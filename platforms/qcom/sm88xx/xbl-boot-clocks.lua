-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Register facts: pinned OEM XBL, routine 0x148605c0 and HW_CTL loop 0x1484f330.
local directory=assert(debug.getinfo(1,'S').source:match('^@(.*/)'))
local append_vote=dofile(directory..'clock-branch-votes.lua')
return function(p, data)
    data=data or {}
    assert(p.cpucp_clock_router and p.apss_router and p.cpucp_router,
        'The reset platform clock routers are required.')
    local function insert(name, module)
        assert(p[name]==nil, 'Duplicate XBL clock module: '..name)
        p[name]=module
    end
    local function overlaps(router,address,size)
        for module_name,module in pairs(p) do
            if type(module)=='table' then
                for _,socket in pairs(module) do
                    if type(socket)=='table' and socket.bind=='&'..router..'.initiator_socket'
                        and type(socket.address)=='number' and type(socket.size)=='number'
                        and address<socket.address+socket.size and socket.address<address+size then
                        return module_name
                    end
                end
            end
        end
        return false
    end
    local function map(name,address,size,module)
        for _,router in ipairs({'cpucp_clock_router','apss_router','cpucp_router'}) do
            local collision=overlaps(router,address,size)
            if collision then
                error(string.format('Clock target 0x%x overlaps %s in %s',address,collision,router))
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
    -- Unmodeled consumers use an explicit virtual reference. This is not physical topology.
    insert('xbl_boot_clock_reference',{moduletype='qcom,virtual-always-on-clock-v1'})
    map('xbl_boot_clock_votes',0x151000,0x1c,data.vote_bank or
        {moduletype='qcom,clock-vote-bank-v1',
            mask0=0x00bc317b,mask1=0xe03c0200,mask2=0x01f8280f,mask3=0x03c00f70})
    -- Six unidentified controls retain bit zero. The MM RSCC word is a GDSCR.
    local clear={
        0x110004,0x189004,0x199000,0x180004,0x11c004,0x183004,0x19d000,
    }
    for _,address in ipairs(data.clear or clear) do
        if address==0x180004 then
            map('xbl_boot_control_'..address,address,4,
                {moduletype='qcom,gdsc-v1',virtual_supply=true})
        else
            map('xbl_boot_control_'..address,address,4,{moduletype='qcom,retained-boot-control-v1',
                writable_mask=1,reset_value=1})
        end
    end
    local enable={
        -- XBL descriptors 0x148f2508..0x148f2548 supply the three SPMI2 branches.
        0xc2a81dc,0xc2a81d4,0xc2a81cc,
        0xc2a8044,0xc2a8114,0xc2a8118,0xc2a811c,0xc2a8120,0xc2a8124,0xc2a8128,0xc2a812c,
        0xc2a8130,0xc2a8134,0xc2a81f0,0xc2a81e8,0xc2a80dc,0xc2a8050,0xc2a81c0,0xc2a81b8,
        0xc2a81b4,0xc2a805c,0x182014,0x182018,0x182064,0x18917c,0x182010,0x18201c,
        0x16a004,0x184004,0x145158,0x11c330,0x11c328,0x18a298,0x18a290,0x144028,
        0x1a7008,0x1a7000,0x1a7010,0x144004,0x171004,0x189018,0x189038,0x189028,
        0x18903c,0x147030,0x14701c,0x147018,0x199040,0x147000,0x147020,0x199060,
        0x199054,0x199050,0x19903c,0x199058,0x19905c,0x11c044,0x11c0a0,0x11c08c,
        0x11c064,0x11c0b4,0x11c0b8,0x11c078,0x11c04c,0x11c050,0x13b028,0x13b008,
        0x17405c,0x174050,0x145154,0x174064,0x11c048,0x174054,0x174058,0x17404c,
        0x19d058,0x19d054,0x136004,0x136008,0x13600c,0x126010,0x126008,0x12600c,
        0x148008,0x127030,0x171008,0x147004,0x18a004,0x18a008,0x145018,0x14501c,
        0x132014,0x132008,0x132010,0x13200c,0x14e008,0x14e004,0x119008,0x119004,
        0x154008,0x154004,0x120008,0x120004,0x11d008,0x11d004,0x14f008,0x14f004,
        0x11d140,0x11d13c,0x144024,0x176024,0x183180,0x199064,0x183170,0x183178,
        0x183168,0x19d068,0x19d050,0x19d064,0x19d03c,0x19d05c,0x19d060,0x3d99224,
        0x3d9922c,
    }
    local hwctl={
        0x18200c,0x126008,0x12600c,0x126010,0x132008,0x13200c,0x132010,0x132014,
        0x147004,0x145018,0x14501c,0x148008,0x16b018,0x18a004,0x18a008,0x171008,
        0x189038,0x1a4008,0x125014,0x125010,0x12500c,0x125008,0x125004,0x174008,
        0x174070,0x148028,0x144018,0x171010,0x148000,0x11c330,0x11c328,0x18301c,
        0x183188,0x144028,0x1a7010,0x182020,0x182030,0x110068,0x182004,0x110050,
        0x182008,0x17601c,0x176008,0x17600c,0x176004,0x11c044,0x11c0a0,0x11c078,
        0x11c064,0x11c08c,0x11c050,0x11c0b4,0x199060,0x14701c,0x19903c,0x31320558,
        0x31320560,0x31321000,0x17c02134,
    }
    -- Recovered target ClockDxe source relations. Keep the existing live RCG objects.
    local sources={[0x171004]='cpucp_clock_root_6',[0x110050]='cpucp_clock_root_6',
        [0x171010]='cpucp_clock_root_11',
        [0xc2a81dc]='xbl_aoss_root1',[0xc2a81d4]='xbl_aoss_root1',
        [0xc2a81cc]='xbl_aoss_root1'}
    -- Linux branch fields and the RPM vote bank supply this explicit virtual vote wiring.
    local votes={[0x110068]={0,0x1000},[0x110050]={0,0x100000},
        [0x144024]={16,0x400000},[0x182020]={8,0x200}}
    -- Scalar store PCs are recorded in xbl_scalar_clock_operations.json.
    local retention={
        [0x144004]=0x100000,[0x11c330]=0x100000,[0x11c328]=0x100000,
        [0x136008]=0x100000,[0x147018]=0x300000,[0x183188]=0x200000,
        [0x147030]=0x200000,[0x14701c]=0x200000,[0x147020]=0x200000,
        [0x18301c]=0x200000,[0x1a7010]=0x100000,[0x183180]=0x100000,
        [0x148000]=0x100000,[0x183168]=0x100000,[0x183170]=0x100000,
        [0x183178]=0x100000,[0x19d068]=0x100000,[0x199064]=0x100000}
    sources=data.sources or sources
    votes=data.votes or votes
    retention=data.retention or retention
    local function branch(address,hardware)
        local module
        for _,candidate in pairs(p) do
            if type(candidate)=='table' and candidate.moduletype=='qcom,clock-branch-v1'
                and candidate.target_socket and candidate.target_socket.address==address then
                module=candidate
                break
            end
        end
        if not module then
            module={moduletype='qcom,clock-branch-v1',
                source_path=sources[address] or 'xbl_boot_clock_reference'}
            map('xbl_boot_branch_'..address,address,4,module)
        end
        if hardware then
            module.hardware_control=true
            -- Virtual demand replaces absent consumers. It cannot replace a stopped source.
            module.continuous_demand=true
        end
        if retention[address] then module.retained_mask=retention[address] end
        if votes[address] then
            append_vote(module,'xbl_boot_clock_votes',
                votes[address][1],votes[address][2],address)
        end
    end
    for _,address in ipairs(data.enable or enable) do branch(address,false) end
    for _,address in ipairs(data.hwctl or hwctl) do branch(address,true) end
    -- XBL 0x1484eef8..0x1484ef34 disables five unused branches.
    for _,address in ipairs(data.disable or {0x182048,0x174034,0x174018,0x174014,0x148010}) do
        branch(address,false)
    end
    -- XBL scalar clock-policy writes retain only their declared fields.
    -- No physical memory collapse, clock clamp, or timing optimization is simulated.
    local policy={
        {0x11006c,0x200000},{0x110078,0x100000},{0x11007c,0x100000},
        {0x11c080,0x200000},{0x11c0a8,0x200000},{0x126014,0x200000},
        {0x126028,0x200000},{0x12701c,0x200000},{0x132018,0x200000},
        {0x132020,0x400000},{0x13202c,0x200000},{0x132034,0x400000},
        {0x14400c,0x100000},{0x145000,0x200000},{0x145008,0x200000},
        {0x1483c4,2},{0x158004,0x80},{0x16e000,0xe},{0x173018,0x33},
        {0x1748ec,1},{0x182044,0x100000},{0x18204c,0x100000},
        {0x18205c,0x100000},{0x182068,0x100000},{0x183024,0x200000},
        {0x183160,0x100000},{0x183164,0x100000},{0x199044,0x200000},
        {0x19d044,0x200000},{0x19f01c,0x200000},{0x1a7018,1},{0x1a701c,1},
        -- 0x1fd5054 remains connected to the existing UFS reference gate.
        {0x1fd5044,1},{0x1fd504c,1},{0x1fd505c,1},{0x1fdf000,1},
    }
    for _,word in ipairs(data.policy or policy) do
        map('xbl_clock_policy_'..word[1],word[1],4,{moduletype='qcom,retained-boot-control-v1',
            writable_mask=word[2],reset_value=0})
    end
    -- XBL table 0x148f11f4 supplies 56 dynamic-divider controls.
    -- Their enable state is retained. The virtual sources do not change cycle timing.
    local dcd={
        0x310a82bc,0x310a83ec,0x182524,0x182528,0x182574,0x182530,0x18252c,0x182520,
        0x14302c,0x1748c4,0x1a2024,0x1748e4,0x1748e8,0x1748c0,0x1748bc,0x1748b4,
        0x1748cc,0x1748c8,0x1748d8,0x1748b0,0x1748e0,0x1748d4,0x1748dc,0x1748d0,
        0x147034,0x11c1f4,0x11c324,0x146038,0x14603c,0x146040,0x144284,0x17615c,
        0x17602c,0x17628c,0x176028,0x145150,0x18251c,0x141020,0x148294,0x148160,
        0x14815c,0x148164,0x144288,0x189174,0x180024,0x11c0bc,0x11c0c4,0x11c0c0,
        0x18315c,0x18a02c,0x18a15c,0x1a4068,0x182568,0x18256c,0x182570,0x182564,
    }
    for _,address in ipairs(data.dcd or dcd) do
        map('xbl_clock_dcd_'..address,address,4,{moduletype='qcom,retained-boot-control-v1',
            writable_mask=1,reset_value=0})
    end
    -- OEM XBL table 0x14920bc8 supplies 118 register addresses and masks.
    -- The guest supplies all values. The virtual interconnect retains policy without physical timing.
    local interconnect={
        {0x1604008,0xfffff03f},
        {0x1604018,0x3ffc7},
        {0x1604020,0xfffff03f},
        {0x1604030,0xffffc07f},
        {0x1604040,0x3ffc7},
        {0x1604048,0xffff00ff},
        {0x1604058,0x3d},
        {0x1604068,0x3d},
        {0x1604070,0x3d},
        {0x1604078,0x3d},
        {0x1604080,0x3d},
        {0x1604088,0x3d},
        {0x1604090,0x3d},
        {0x1604098,0x3d},
        {0x16040a0,0x3ffc7},
        {0x16040a8,0xfffc01ff},
        {0x16040b8,0x3d},
        {0x16040c0,0x3d},
        {0x31217598,0x3ff},
        {0x31217018,0x3ff},
        {0x31217098,0x3ff},
        {0x31217118,0x3ff},
        {0x31217218,0x3ff},
        {0x31217298,0x3ff},
        {0x31217318,0x3ff},
        {0x31217398,0x3ff},
        {0x31217418,0x3ff},
        {0x31217498,0x3ff},
        {0x31217518,0x3ff},
        {0x31217718,0x3ff},
        {0x31217618,0x3ff},
        {0x31217698,0x3ff},
        {0x31211028,0x1},
        {0x31211050,0x1},
        {0x31211078,0x1},
        {0x312110a8,0x1},
        {0x31211120,0x1},
        {0x31211148,0x1},
        {0x31211208,0x1},
        {0x31211258,0x1},
        {0x1507038,0x3},
        {0x1507048,0x3},
        {0x1507050,0x3},
        {0x1506008,0x3d},
        {0x1506010,0xff3},
        {0x1506018,0x3d},
        {0x1504010,0x3ff},
        {0x1504018,0x3ff},
        {0x1504098,0x3ff},
        {0x1504118,0x3ff},
        {0x1505020,0x1},
        {0x1505048,0x1},
        {0x1505068,0x1},
        {0x1706028,0x3},
        {0x16e4018,0x3ff},
        {0x16e4098,0x3ff},
        {0x1707098,0x3ff},
        {0x1707018,0x3ff},
        {0x16c7098,0x3ff},
        {0x16c7018,0x3ff},
        {0x1705020,0x1},
        {0x1705048,0x1},
        {0x1705070,0x1},
        {0x1705098,0x1},
        {0x17050d8,0x1},
        {0x17050f8,0x1},
        {0x1688218,0x3ff},
        {0x1687010,0x3ff},
        {0x1687018,0x3ff},
        {0x1687090,0x3ff},
        {0x1687098,0x3ff},
        {0x1687110,0x3ff},
        {0x1687118,0x3ff},
        {0x1685020,0x1},
        {0x1685048,0x1},
        {0x1685070,0x1},
        {0x1685098,0x1},
        {0x1788010,0x3ff},
        {0x1788018,0x3ff},
        {0x1788090,0x3ff},
        {0x1788098,0x3ff},
        {0x1788198,0x3ff},
        {0x1788218,0x3ff},
        {0x1788110,0x3ff},
        {0x1788118,0x3ff},
        {0x1788298,0x3ff},
        {0x1787028,0x1},
        {0x1787050,0x1},
        {0x1787078,0x1},
        {0x17870b8,0x1},
        {0x17870e0,0x1},
        {0x1787108,0x1},
        {0x7f45018,0x3ff},
        {0x7f44208,0x3d},
        {0x7f44028,0x1},
        {0x7bb010,0x3ff},
        {0x7bb018,0x3ff},
        {0x7b9068,0x1},
        {0x17e86018,0x3ff},
        {0x17e84020,0x1},
        {0x31333008,0x3d},
        {0x31333010,0x3d},
        {0x31333018,0x3d},
        {0x31333020,0x3d},
        {0x31333028,0x3d},
        {0x31333030,0x3d},
        {0x31333038,0x3d},
        {0x31333040,0x3d},
        {0x31333048,0x3d},
        {0x31335018,0x3ff},
        {0x31335090,0x3ff},
        {0x31335098,0x3ff},
        {0x31332028,0x1},
        {0x31332050,0x1},
        {0x310e3010,0x3ff},
        {0x310e3018,0x3ff},
        {0x310e4020,0x1},
        {0x310e4040,0x1},
    }
    for _,word in ipairs(data.interconnect or interconnect) do
        map('xbl_interconnect_policy_'..word[1],word[1],4,{moduletype='qcom,retained-boot-control-v1',
            writable_mask=word[2],reset_value=0})
    end
    return p
end
