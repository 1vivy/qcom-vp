-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Selected XBL DCP driver banks. No image, ready cookie, or command queue is seeded.
return function(p)
    assert(p.apss_router, 'The APSS router is required.')
    assert(p.dcp, 'The DCP CPU is required.')
    assert(not p.dcp_bootstrap, 'DCP bootstrap hardware already exists.')
    local banks = {0x09bab000, 0x09bae000, 0x09bb4000, 0x09c70000, 0x09ca0000, 0x09ca2000}
    local function check_range(base,size)
        for name,module in pairs(p) do
            if type(module)=='table' then
                for _,socket in pairs(module) do
                    if type(socket)=='table' and socket.bind=='&apss_router.initiator_socket' and
                        type(socket.address)=='number' and type(socket.size)=='number' and
                        base<socket.address+socket.size and socket.address<base+size then
                        error('DCP bootstrap bank overlaps target '..name)
                    end
                end
            end
        end
    end
    for _,base in ipairs(banks) do check_range(base,0x1000) end
    assert(p.xbl_boot_clock_reference, 'The shared clock reference is required.')
    -- Both selected XBL drivers enable these external clock branches.
    for _,address in ipairs({0x00127004,0x00127008}) do
        check_range(address,4)
        p['dcp_shared_branch_'..address]={moduletype='qcom,clock-branch-v1',
            source_path='xbl_boot_clock_reference',
            target_socket={address=address,size=4,bind='&apss_router.initiator_socket'}}
    end
    -- SM8850 14899eac and SM8845 14898824 retain these preparation requests.
    -- Their values do not grant access or establish firmware readiness.
    for _,offset in ipairs({0,0x40,0x80,0xc0}) do
        local address=0x0b7fea60+offset
        check_range(address,4)
        p['dcp_shared_configuration_'..offset]={moduletype='qcom,retained-boot-control-v1',
            writable_mask=0xffffffff,reset_value=0,
            target_socket={address=address,size=4,bind='&apss_router.initiator_socket'}}
    end
    local sockets = {'power_socket','branch_socket','clock_socket','boot_socket','dtb_socket','config_socket'}
    p.dcp_bootstrap={moduletype='qcom,dcp-bootstrap-v1',args={'&dcp'},reset={bind='&dcp.reset'}}
    p.dcp.wfi={bind='&dcp_bootstrap.wfi'}
    for i,base in ipairs(banks) do
        p.dcp_bootstrap[sockets[i]] = {address=base,size=0x1000,relative_addresses=false,
            bind='&apss_router.initiator_socket'}
    end
    if p.dcp_router then
        -- Window one carries DCP system accesses through XBL's programmed entries.
        p.dcp_bootstrap.remap_socket={address=0x40000000,size=0x60000000,relative_addresses=false,
            bind='&dcp_router.initiator_socket'}
        p.dcp_bootstrap.soc_socket={bind='&dcp_system_router.target_socket'}
    end
    return p
end
