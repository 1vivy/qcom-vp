-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- XBL descriptors 0x148f1ee8..0x148f21a8 share rate table 0x148f1e40.
local directory=assert(debug.getinfo(1,'S').source:match('^@(.*/)'))
local append_vote=dofile(directory..'clock-branch-votes.lua')
return function(p, data)
    data=data or {}
    assert(p.cpucp_clock_router and p.apss_router and p.cpucp_router and p.gcc_plls,
        'The reset platform clock routers and GCC PLL bank are required.')
    local function insert(name,module)
        assert(p[name]==nil, 'Duplicate XBL peripheral clock module: '..name)
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
    -- Root, CBCR, vote offset from 0x152008, vote mask. Branch table: 0x148f26a8.
    local clocks={
        {0x118014,0x118004,0,0x400000},
        {0x118150,0x118140,0,0x800000},
        {0x1182a0,0x118290,0,0x2000000},
        {0x1183dc,0x1183cc,0,0x4000000},
        {0x118518,0x118508,0,0x8000000},
        {0x118654,0x118644,0,0x10000000},
        {0x118790,0x118780,8,0x10000},
        {0x11e014,0x11e004,8,0x10},
        {0x11e150,0x11e140,8,0x20},
        {0x11e28c,0x11e27c,8,0x40},
        {0x11e3c8,0x11e3b8,8,0x80},
        {0x11e504,0x11e4f4,8,0x100},
        {0x1a8014,0x1a8004,16,0x1000},
        {0x1a8150,0x1a8140,16,0x2000},
        {0x1a828c,0x1a827c,16,0x4000},
        {0x1a83c8,0x1a83b8,16,0x8000},
        {0x1a8504,0x1a84f4,16,0x10000},
        {0x1a8640,0x1a8630,16,0x20000},
        {0x1a9014,0x1a9004,16,0x4000000},
        {0x1a9150,0x1a9140,16,0x8000000},
        {0x1a928c,0x1a927c,16,0x10000000},
        {0x1a93c8,0x1a93b8,16,0x20000000},
        {0x1a9504,0x1a94f4,16,0x40000000},
    }
    map('xbl_peripheral_clock_votes',0x152008,0x14,data.vote_bank or
        {moduletype='qcom,clock-vote-bank-v1',
            mask0=0x1ec00000,mask1=0x101f0,mask2=0x7c03f000})
    for _,clock in ipairs(data.clocks or clocks) do
        local root='xbl_peripheral_root_'..clock[1]
        -- Linux gcc_parent_map_0: XO=0, GPLL0 main=1, GPLL0 even=6.
        -- Continuous virtual root demand replaces absent consumer-to-root vote wiring.
        map(root,clock[1],0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',
            crystal_selector=0,primary_selector=1,primary_even_selector=6,
            primary_index=0,mnd_width=16,require_root_enable=false})
        local branch={moduletype='qcom,clock-branch-v1',source_path=root}
        append_vote(branch,'xbl_peripheral_clock_votes',clock[3],clock[4],clock[2])
        map('xbl_peripheral_branch_'..clock[2],clock[2],4,branch)
    end
    return p
end
