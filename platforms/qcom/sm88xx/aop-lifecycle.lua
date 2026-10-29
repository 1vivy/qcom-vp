-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Both selected OEMs use these registers. See models/qcom/rpmh/evidence.md.
return function(p)
    assert(p.tme_router and p.apss_router and p.runtime_clock_reference and p.runtime_pll_1,
        'The runtime routers and clock sources are required.')
    assert(not p.aop_transfer_control, 'The AOP lifecycle hardware already exists.')
    local function overlap(router,address,size)
        local found
        for name,module in pairs(p) do
            if type(module)=='table' then
                for _,socket in pairs(module) do
                    if type(socket)=='table' and socket.bind=='&'..router..'.initiator_socket' and
                        type(socket.address)=='number' and type(socket.size)=='number' and
                        address<socket.address+socket.size and socket.address<address+size then
                        assert(not found and socket.address==address and socket.size==size,
                            'Partial or duplicate AOP hardware mapping: '..name)
                        found={name=name,module=module,socket=socket}
                    end
                end
            end
        end
        return found
    end
    local function insert(name,module)
        assert(not p[name], 'Duplicate AOP hardware module: '..name)
        p[name]=module
    end
    local function map(name,address,size,module)
        assert(not overlap('tme_router',address,size), 'The TME hardware range already has an owner.')
        local physical=address-0x04000000
        local existing=overlap('apss_router',physical,size)
        if existing then
            -- The SM8845 XBL helper already owns this HW_CTL branch.
            assert(address==0x0418a01c and module.moduletype=='qcom,clock-branch-v1',
                'An existing AOP hardware owner requires an explicit shared contract.')
            insert(name,{moduletype='addrtr',mapped_base_addr=physical,
                target_socket={address=address,size=size,relative_addresses=false,
                    bind='&tme_router.initiator_socket'},
                initiator_socket={bind='&apss_router.target_socket'}})
        else
            module.target_socket={address=address,size=size,bind='&tme_router.initiator_socket'}
            insert(name,module)
            insert(name..'_system',{moduletype='addrtr',mapped_base_addr=0,
                target_socket={address=physical,size=size,relative_addresses=false,
                    bind='&apss_router.initiator_socket'},
                initiator_socket={bind='&'..name..'.target_socket'}})
        end
    end
    map('aop_transfer_control',0x08080000,0x418,{moduletype='qcom,aop-control-v1'})
    for i,address in ipairs({0x05fb4000,0x05fbb000,0x05fb5000,0x05fb8014}) do
        map('aop_request_ack_'..i,address,8,{moduletype='qcom,request-ack-v1'})
    end
    for i,address in ipairs({0x05fb4014,0x05fb4024,0x05fb4034}) do
        map('aop_halt_channel_'..i,address,0x10,{moduletype='qcom,halt-channel-v1',
            source_path='runtime_clock_reference',initial_request=1})
    end
    local halt_sources={
        [0x081af41c]='aop_halt_channel_1',
        [0x0418a00c]='aop_halt_channel_3',
    }
    for _,address in ipairs({0x0418a000,0x081af41c,0x081af400,0x0418a00c,
            0x0418a01c,0x081af418,0x0418a360,0x081a80d8}) do
        map('aop_branch_'..address,address,4,{moduletype='qcom,clock-branch-v1',
            source_path=halt_sources[address] or 'runtime_clock_reference',
            hardware_control=true,continuous_demand=true,retained_mask=0x7ffffffc})
    end
    -- The selected mux input uses an explicit virtual reference, not an inferred PLL parent.
    -- The existing RCG retains, commits, and divides the requested configuration.
    for i,root in ipairs({{0x080c8020,2},{0x081af42c,2},{0x081af578,1}}) do
        map('aop_root_'..i,root[1],8,{moduletype='qcom,rcg-v1',
            pll_path='runtime_pll_1',crystal_selector=root[2],require_root_enable=false})
    end
    -- These controls retain requests. They do not create a reset acknowledgment.
    -- Cold zero and full-width retention are explicit virtual policies.
    for _,address in ipairs({0x102f3000,0x081a8030,0x102f4000,0x0f5ee000,
            0x081a8074,0x081af420}) do
        map('aop_retained_control_'..address,address,4,{moduletype='qcom,retained-boot-control-v1',
            writable_mask=0xffffffff,reset_value=0})
    end
    return p
end
