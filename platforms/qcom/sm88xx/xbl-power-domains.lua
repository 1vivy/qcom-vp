-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Native descriptor inventories: models/qcom/sm88xx/cpucp/clock_roots_evidence.md.
return function(p, additional)
    assert(p.cpucp_clock_router and p.apss_router and p.cpucp_router and p.xbl_boot_clock_reference,
        'The reset platform clock routers are required.')
    local addresses={0x17b03c,0x17b068,0x03d95004}
    for _,address in ipairs(additional or {}) do addresses[#addresses+1]=address end
    local function insert(name,module)
        assert(p[name]==nil, 'Duplicate XBL power module: '..name)
        p[name]=module
    end
    local function map(name,address,size,module)
        for _,router in ipairs({'cpucp_clock_router','apss_router','cpucp_router'}) do
            for name,module in pairs(p) do
                if type(module)=='table' then
                    for _,socket in pairs(module) do
                        if type(socket)=='table' and socket.bind=='&'..router..'.initiator_socket'
                            and type(socket.address)=='number' and type(socket.size)=='number'
                            and address<socket.address+socket.size and socket.address<address+size then
                            error(string.format('Power target 0x%x overlaps %s in %s',address,name,router))
                        end
                    end
                end
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
    for _,address in ipairs(addresses) do
        -- This virtual supply powers only the domain latch, not an absent peripheral.
        -- The native table does not declare a clock source or an upstream domain.
        local size=(address==0x03d99080 or address==0x03d68024) and 8 or 4
        local domain={moduletype='qcom,gdsc-v1',virtual_supply=true,retained_mask=size==8 and 0x100 or 0}
        -- Linux gpucc-canoe.c declares gpu_cc_cx_gdsc with gds_hw_ctrl at GDSCR + 0x14.
        if address==0x03d99080 then size=0x18; domain.hw_ctrl_offset=0x14 end
        map('xbl_power_domain_'..address,address,size,domain)
    end
    -- Native SMMU maps pair these independent branch requests with the GDS votes.
    -- Their descriptors have no RCG parent. Use the explicit virtual reference policy.
    for _,address in ipairs({0x17b000,0x17b02c}) do
        map('xbl_smmu_branch_'..address,address,4,
            {moduletype='qcom,clock-branch-v1',source_path='xbl_boot_clock_reference'})
    end
    -- UFS, USB, and MM RSCC keep their existing owners and router aliases.
    return p
end
