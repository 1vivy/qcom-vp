-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Native address formulas and virtual timing rows: models/qcom/sm88xx/ddr/phy-evidence.md.
return function(p)
    local mc = assert(p.ddr_phy_mc, 'The DDR MC configuration owner is required.')
    assert(p.apss_router, 'The APSS router is required.')
    mc.configuration_banks = 4
    mc.retained_extent = mc.target_socket.size
    mc.mc_calibration = true
    local words, seen = {}, {}
    local function insert(offset)
        if not seen[offset] then
            seen[offset] = true
            words[#words+1] = string.format('0x%x', offset)
        end
    end
    for token in mc.configuration_offsets:gmatch('[^,]+') do insert(assert(tonumber(token))) end
    for _, base in ipairs({0, 0x3000, 0x74fc}) do
        for level = 0, 10 do
            for field = 0, 124 do insert(base + level * 0x1f4 + field * 4) end
        end
    end
    for _, offset in ipairs({0x64ac,0x64b4,0x6c3c,0x6cb8,0x6ecc,0x6f0c,0x6f24,0x8e74,0x8e7c,0x8e98,0x8ea4}) do insert(offset) end
    mc.configuration_offsets = table.concat(words, ',')
    for index, address in ipairs({0x31b80000, 0x32b80000, 0x31f80000, 0x32f80000}) do
        local name = 'ddr_mc_pair'..(index-1)
        assert(not p[name], 'The DDR MC pair mapping already exists.')
        p[name] = {moduletype='addrtr', mapped_base_addr=index * 0x10000,
            target_socket={address=address, size=mc.target_socket.size, relative_addresses=false,
                bind='&apss_router.initiator_socket'},
            initiator_socket={bind='&ddr_phy_mc.target_socket'}}
    end
end
