-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Selected OEM AC inventories. See models/qcom/vmidmt/platform-evidence.md.
-- Geometry is an explicit virtual capability, not a physical silicon claim.
local inventories = {
    sm8850 = {
        {port=0x3e, base=0x008c6000, entry_count=128, stream_id_bits=15,
            ssd_index_bits=7, input_address_bits=48, vmid_count=32, stream_match_support=true},
        {port=0x19, base=0x00ac6000, entry_count=128, stream_id_bits=15,
            ssd_index_bits=7, input_address_bits=48, vmid_count=32, stream_match_support=true},
        {port=0x5c, base=0x019c6000, entry_count=128, stream_id_bits=15,
            ssd_index_bits=7, input_address_bits=48, vmid_count=32, stream_match_support=true},
        {port=0x5d, base=0x01ac6000, entry_count=128, stream_id_bits=15,
            ssd_index_bits=7, input_address_bits=48, vmid_count=32, stream_match_support=true},
    },
    sm8845 = {
        {port=0x3e, base=0x008c6000, entry_count=128, stream_id_bits=15,
            ssd_index_bits=7, input_address_bits=48, vmid_count=32, stream_match_support=true},
        {port=0x19, base=0x00ac6000, entry_count=128, stream_id_bits=15,
            ssd_index_bits=7, input_address_bits=48, vmid_count=32, stream_match_support=true},
        {port=0x5c, base=0x019c6000, entry_count=128, stream_id_bits=15,
            ssd_index_bits=7, input_address_bits=48, vmid_count=32, stream_match_support=true},
    },
}

-- TZ AC hardware descriptors. The first field is the port, not an ordinal.
-- Shared XBL port/base pairs must retain the same module and register state.
local tz_inventories = {
    sm8850 = {
        {0x02,0x01dc0000},{0x4a,0x01def000},{0x28,0x03f30000},
        {0x47,0x06cb1000},{0x0b,0x06c00000},{0x40,0x06cb2000},
        {0x4c,0x07f0c000},{0x49,0x06cb0000},{0x41,0x06cb3000},
        {0x3f,0x088dd000},{0x19,0x00ac6000},{0x3e,0x008c6000},
        {0x5c,0x019c6000},{0x5d,0x01ac6000},{0x3b,0x070c0000},
        {0x63,0x07ac6000},{0x39,0x079c6000},
    },
    sm8845 = {
        {0x3e,0x008c6000},{0x19,0x00ac6000},{0x5c,0x019c6000},
        {0x02,0x01dc0000},{0x4a,0x01def000},{0x28,0x03f30000},
        {0x0b,0x06c00000},{0x47,0x06cb1000},{0x40,0x06cb2000},
        {0x41,0x06cb3000},{0x3b,0x070c0000},{0x39,0x079c6000},
        {0x63,0x07ac6000},{0x4c,0x07f0c000},{0x3f,0x088dd000},
    },
}

return function(p, soc)
    assert(p.apss_router, 'The APSS router is required for VMID mappers.')
    local xbl = assert(inventories[soc], 'Unsupported VMID mapper platform: '..tostring(soc))
    local inventory, by_port, tz_ports = {}, {}, {}
    for _, row in ipairs(xbl) do
        assert(not by_port[row.port], 'Duplicate XBL VMID mapper port.')
        by_port[row.port] = row
        inventory[#inventory + 1] = row
    end
    for _, descriptor in ipairs(tz_inventories[soc]) do
        local port, base = descriptor[1], descriptor[2]
        assert(not tz_ports[port], 'Duplicate TZ VMID mapper port.')
        tz_ports[port] = true
        local existing = by_port[port]
        if existing then
            assert(existing.base == base, 'Conflicting XBL/TZ VMID mapper base.')
        else
            local row = {port=port, base=base, entry_count=128, stream_id_bits=15,
                ssd_index_bits=7, input_address_bits=48, vmid_count=32, stream_match_support=true}
            by_port[port] = row
            inventory[#inventory + 1] = row
        end
    end
    local size = 0x1000
    local bind = '&apss_router.initiator_socket'
    local ports = {}
    local pending = {}
    -- Check every owner and aperture before changing the platform.
    for _, row in ipairs(inventory) do
        local name = string.format('vmid_mapper_%02x', row.port)
        assert(not ports[row.port], 'Duplicate VMID mapper port: '..name)
        assert(p[name] == nil, 'Duplicate VMID mapper module: '..name)
        ports[row.port] = true
        for _, other in ipairs(pending) do
            assert(row.base >= other.base + size or other.base >= row.base + size,
                'Overlapping VMID mapper inventory: '..name)
        end
        for owner, module in pairs(p) do
            if type(module) == 'table' then
                for _, target in pairs(module) do
                    if type(target) == 'table' and target.bind == bind
                        and type(target.address) == 'number' and type(target.size) == 'number'
                        and row.base < target.address + target.size
                        and target.address < row.base + size then
                        error(string.format('VMID mapper 0x%x overlaps %s in apss_router', row.base, owner))
                    end
                end
            end
        end
        pending[#pending + 1] = row
    end
    for _, row in ipairs(inventory) do
        p[string.format('vmid_mapper_%02x', row.port)] = {
            moduletype='qcom,vmid-mapper-v1',
            entry_count=row.entry_count,
            stream_id_bits=row.stream_id_bits,
            ssd_index_bits=row.ssd_index_bits,
            input_address_bits=row.input_address_bits,
            vmid_count=row.vmid_count,
            stream_match_support=row.stream_match_support,
            target_socket={address=row.base, size=size, bind=bind},
        }
    end
    return p
end
