-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Selected XBL cold debug group. See models/qcom/eud/evidence.md.
return function(p, clocks)
    assert(p.apss_router and p.cpucp_router and p.cpucp_clock_router,
        'The reset platform clock routers are required for EUD.')
    assert(clocks and type(clocks.second_branch) == 'number'
        and type(clocks.second_branch_hardware_control) == 'boolean',
        'The selected USB clock branch data is required for EUD.')
    local master_root
    for name, module in pairs(p) do
        if type(module) == 'table' and module.moduletype == 'qcom,rcg-v1'
            and module.target_socket and module.target_socket.address == 0x139034
            and module.target_socket.bind == '&cpucp_clock_router.initiator_socket' then
            assert(master_root == nil, 'Duplicate USB master clock root.')
            master_root = name
        end
    end
    assert(master_root, 'The existing USB master clock root is required for EUD.')
    local function insert(name, module)
        assert(p[name] == nil, 'Duplicate EUD module: '..name)
        p[name] = module
    end
    local function socket(address, size, router)
        for name, module in pairs(p) do
            if type(module) == 'table' then
                for _, target in pairs(module) do
                    if type(target) == 'table' and target.bind == '&'..router..'.initiator_socket'
                        and type(target.address) == 'number' and type(target.size) == 'number'
                        and address < target.address + target.size and target.address < address + size then
                        error(string.format('EUD target 0x%x overlaps %s in %s', address, name, router))
                    end
                end
            end
        end
        return {address=address, size=size, bind='&'..router..'.initiator_socket'}
    end
    local function retain(name, address, mask)
        insert(name, {moduletype='qcom,retained-boot-control-v1', writable_mask=mask, reset_value=0,
            target_socket=socket(address, 4, 'apss_router')})
    end
    insert('eud_disconnected', {moduletype='qcom,disconnected-usb-debug-v1', disconnected=true,
        enable_socket=socket(0x88e2000, 4, 'apss_router'),
        enumeration_socket=socket(0x88e0240, 4, 'apss_router'),
        attach_socket=socket(0x88e1020, 4, 'apss_router'),
        controller_socket=socket(0xa60c704, 4, 'apss_router'),
        detect_socket=socket(0xc278000, 4, 'apss_router')})
    for i=0,3 do retain('eud_dev_id_'..i, 0x88e1000 + i*4, 0xff) end
    retain('eud_software_enable', 0x88e1014, 1)
    retain('eud_enable_delay_lsb', 0x88e1180, 0xff)
    retain('eud_enable_delay_msb', 0x88e1184, 0xff)
    retain('eud_pbus_control', 0x88e1024, 1)
    retain('eud_extra_tune', 0x88e102c, 1)
    -- Guest-provided PHY address/value pairs, not USB string descriptors.
    for i=0,16 do
        retain('eud_tune_address_'..i, 0x88e1080 + i*8, 0xff)
        retain('eud_tune_value_'..i, 0x88e1084 + i*8, 0xff)
    end
    local function clock_map(name, address, size, module)
        module.target_socket = socket(address, size, 'cpucp_clock_router')
        insert(name, module)
        for _, router in ipairs({'apss_router', 'cpucp_router'}) do
            local target = socket(address, size, router)
            target.relative_addresses = false
            insert(name..'_'..router, {moduletype='addrtr', mapped_base_addr=address,
                target_socket=target, initiator_socket={bind='&cpucp_clock_router.target_socket'}})
        end
    end
    -- Share existing votes. SM8845's boot policy does not create these words.
    for _, address in ipairs({0x1fd504c, 0x1fd505c}) do
        local owner
        for _, module in pairs(p) do
            if type(module) == 'table' and module.moduletype == 'qcom,retained-boot-control-v1'
                and module.target_socket and module.target_socket.address == address then
                assert(owner == nil, 'Duplicate EUD refclk vote.')
                assert(module.writable_mask == 1, 'The EUD refclk vote must retain bit zero.')
                owner = module
            end
        end
        if not owner then
            clock_map('eud_refclk_vote_'..address, address, 4,
                {moduletype='qcom,retained-boot-control-v1', writable_mask=1, reset_value=0})
        end
    end
    -- Explicit virtual references for non-PIPE branches without modeled consumers.
    -- The three master branches consume the live, existing RCG instead.
    insert('eud_clock_reference', {moduletype='qcom,virtual-always-on-clock-v1'})
    for _, branch in ipairs({
        {0x139094, master_root}, {clocks.second_branch, master_root},
        {0x139018, master_root}, {0x13902c, 'eud_clock_reference'},
        {0x139028, 'eud_clock_reference'}, {0x139068, 'eud_clock_reference'},
        {0x13906c, 'eud_clock_reference'}, {0x139070, 'eud_disconnected'},
    }) do
        clock_map('eud_clock_'..branch[1], branch[1], 4,
            {moduletype='qcom,clock-branch-v1', source_path=branch[2],
                hardware_control=branch[1] == 0x139094 or branch[1] == 0x139070
                    or (branch[1] == clocks.second_branch and clocks.second_branch_hardware_control)})
    end
    clock_map('eud_usb_power', 0x139004, 8, {moduletype='qcom,gdsc-v1',
        power_socket={bind='&eud_disconnected.power_socket'}})
    -- BCR 0x139000, EUD reset, and USB service registers remain unmapped.
    -- Enable requests do not create an attached host or enumeration.
    return p
end
