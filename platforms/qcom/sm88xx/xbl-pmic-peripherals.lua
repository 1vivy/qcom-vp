-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Explicit virtual inventory, not physical PMIC revisions or a reset dump.
-- Call this helper after constructing platform.xbl_pmic. It needs no host path.
-- The ranges are decoded discovery facts, not vendor code or register contents.
local discovery = {
    [0x5c]={{{0x1,0x1},{0x4,0x9},{0xb,0xb},{0xd,0xd},{0x12,0x33},{0x36,0x37},{0x40,0x45},{0x48,0x4b},{0x50,0x51},{0x54,0x5c},{0x5e,0x5e},{0x60,0x64},{0x67,0xbf},{0xd0,0xd7},{0xe8,0xe9},{0xef,0xfc},{0xfe,0xfe}}},
    [0x5d]={{{0x1,0x1},{0x4,0xb},{0xd,0xf},{0x13,0x15},{0x1c,0x1d},{0x36,0x36},{0x39,0x39},{0x3c,0x3c},{0x3e,0x3e},{0x40,0x43},{0x47,0x4e},{0x51,0x51},{0x5e,0x5e},{0x88,0x9b},{0xc1,0xd2},{0xe1,0xe2},{0xe4,0xeb},{0xee,0xef},{0xfe,0xff}},{{0x1,0x1},{0x4,0x6},{0x8,0xb},{0xd,0xf},{0x13,0x15},{0x1c,0x1d},{0x36,0x36},{0x39,0x39},{0x3c,0x3c},{0x3e,0x3e},{0x40,0x41},{0x47,0x4a},{0x51,0x51},{0x5e,0x5e},{0x88,0x93},{0x98,0x99},{0xc1,0xd1},{0xe1,0xe2},{0xe4,0xeb},{0xee,0xef},{0xfe,0xff}}},
    [0x61]={{{0x1,0x1},{0x4,0xb},{0xe,0xf},{0x1c,0x1d},{0x36,0x36},{0x39,0x39},{0x51,0x51},{0x5e,0x5e},{0x88,0x95},{0x99,0xb8},{0xc1,0xc4},{0xe1,0xe1},{0xfe,0xff}},{{0x1,0x1},{0x4,0xb},{0xe,0xf},{0x1c,0x1d},{0x51,0x51},{0x5e,0x5e},{0x88,0x8f},{0x99,0xa6},{0xfe,0xff}}},
    [0x56]={{{0x1,0x1},{0x4,0x6},{0x8,0xc},{0xe,0xf},{0x13,0x14},{0x16,0x19},{0x1c,0x1e},{0x22,0x2d},{0x30,0x30},{0x36,0x36},{0x39,0x3a},{0x3c,0x3e},{0x47,0x4a},{0x4e,0x4f},{0x51,0x51},{0x5e,0x5e},{0x88,0x99},{0xf0,0xf2},{0xfd,0xfe}},{{0x1,0x1},{0x4,0x6},{0x8,0xb},{0xd,0xf},{0x13,0x15},{0x1c,0x1d},{0x36,0x36},{0x39,0x39},{0x3c,0x3c},{0x3e,0x3e},{0x40,0x41},{0x47,0x4a},{0x51,0x51},{0x5e,0x5e},{0x88,0x93},{0x98,0xac},{0xc1,0xd8},{0xe1,0xe2},{0xe4,0xe5},{0xe8,0xeb},{0xee,0xef},{0xfe,0xff}}},
    [0x60]={{{0x1,0x1},{0x4,0xb},{0xe,0xf},{0x1c,0x1d},{0x51,0x51},{0x5e,0x5e},{0x88,0x8f},{0x99,0xa6},{0xfe,0xff}}},
    [0x48]={{{0x1,0x1},{0x4,0x6},{0x8,0xb},{0xe,0xe},{0x1c,0x1d},{0x51,0x51},{0x5e,0x5e},{0x88,0x89},{0x99,0x99},{0xc1,0xc7},{0xe1,0xe1},{0xfe,0xff}},{{0x1,0x1},{0x4,0x6},{0x8,0xb},{0xe,0xe},{0x1c,0x1d},{0x51,0x51},{0x5e,0x5e},{0x88,0x8b},{0x99,0x9d},{0xfe,0xff}}},
    [0x41]={{{0x1,0x1},{0x4,0x7},{0x9,0x9},{0x24,0x24},{0x2c,0x2c},{0x40,0x46},{0x51,0x51},{0xc0,0xc1},{0xe1,0xe1}},{{0x1,0x1},{0x4,0x6},{0x8,0xb},{0xe,0xe},{0x1c,0x1d},{0x4b,0x4d},{0x51,0x51},{0x5e,0x5e},{0x88,0x8b},{0x99,0x99},{0xf8,0xfa},{0xfe,0xff}}},
}

local function rows(text, count, consume)
    for row in (text or ''):gmatch('[^;]+') do
        local values = {}
        for token in row:gmatch('%S+') do
            values[#values+1] = assert(tonumber(token), 'Invalid PMIC inventory number')
        end
        assert(#values == count, 'Invalid PMIC inventory row')
        consume(values)
    end
end

return function(platform, policy)
    policy = policy or {}
    local function inventory(model)
        return (policy.discovery and policy.discovery[model]) or discovery[model]
    end
    local pmic = assert(platform.xbl_pmic, 'Construct xbl_pmic before its peripheral inventory')
    assert(not pmic.absent_peripherals and not pmic.sdam_peripherals and
           not pmic.regulator_peripherals and not pmic.pbs_peripherals,
           'PMIC peripheral inventory is already configured')
    local present, populated, missing = {}, {}, {}
    local function id(bus, sid, peripheral)
        return string.format('%d %d %d', bus, sid, peripheral)
    end
    local manufacturing = {}
    rows(pmic.identities, 7, function(p)
        local banks=inventory(p[3])
        assert(banks and #banks >= p[6],
               'No exact XBL discovery inventory for this identity span')
        for bank=0,p[6]-1 do
            populated[string.format('%d %d', p[1], p[2]+bank)] = true
            if policy.unassigned_manufacturing then
                local device = string.format('%d %d ', p[1], p[2]+bank)
                -- The virtual device has no assigned factory coordinates or lot.
                -- The PBS information reader reads 01f0..01ff and the OTP identifier 0154.
                -- QSEE run499 reads the eight-byte burst at 01f0.
                manufacturing[#manufacturing+1] = device .. '0x154 0 0'
                for address=0x1f0,0x1ff do
                    manufacturing[#manufacturing+1] =
                        device .. string.format('0x%x 0 0', address)
                end
            end
        end
    end)
    if #manufacturing > 0 then
        pmic.registers = (pmic.registers or '') .. ';' .. table.concat(manufacturing, ';')
    end
    if policy.xo_configuration_variant then
        assert(policy.xo_configuration_variant == 1 or policy.xo_configuration_variant == 2,
               'Unsupported virtual XO configuration variant')
        -- Both PSI programs select electrical request groups through this byte.
        -- The selector is not a frequency measurement or a ready flag.
        pmic.registers = (pmic.registers or '') ..
            string.format(';0 0 0x504b %d 0xff', policy.xo_configuration_variant)
    end
    if policy.sleep_clock_configuration_variant ~= nil then
        assert(policy.sleep_clock_configuration_variant == 0 or
               policy.sleep_clock_configuration_variant == 1,
               'Unsupported virtual sleep-clock configuration variant')
        -- This byte selects one-shot configuration. It is not CLK_OK at offset08.
        pmic.registers = (pmic.registers or '') ..
            string.format(';0 0 0x604a %d 0xff', policy.sleep_clock_configuration_variant)
    end
    rows(pmic.pon_peripherals, 3, function(p)
        present[id(p[1],p[2],p[3])] = true
    end)
    rows(pmic.pon_hlos_peripherals or '', 3, function(p)
        present[id(p[1],p[2],p[3])] = true
    end)
    -- Boards can declare a bounded SDAM inventory instead of the default topology.
    local sdam = {}
    if policy.sdam ~= nil then
        rows(policy.sdam, 4, function(p)
            assert(populated[string.format('%d %d',p[1],p[2])], 'SDAM SID is not populated')
            sdam[#sdam+1] = table.concat(p, ' ')
            for peripheral=p[3],p[3]+p[4]-1 do
                assert(not present[id(p[1],p[2],peripheral)], 'Overlapping virtual peripheral')
                present[id(p[1],p[2],peripheral)] = true
            end
        end)
    else
        -- The existing model5c policy declares 72 virtual banks on each primary.
        rows(pmic.identities, 7, function(p)
            if p[3] == 0x5c then
                sdam[#sdam+1] = string.format('%d %d 0x70 72', p[1], p[2])
                for peripheral=0x70,0xb7 do present[id(p[1],p[2],peripheral)] = true end
            end
        end)
    end
    -- Optional functional rails must be explicit. No scan bit implies a rail type.
    -- Type3=SMPS/subtype0a, type4=LDO/subtype30, type41=BOB/subtype1.
    local regulators = policy.regulators or ''
    rows(regulators, 4, function(p)
        assert(populated[string.format('%d %d',p[1],p[2])], 'Rail SID is not populated')
        assert(p[4] == 3 or p[4] == 4 or p[4] == 41, 'Unsupported virtual rail type')
        assert(not present[id(p[1],p[2],p[3])], 'Overlapping virtual peripheral')
        present[id(p[1],p[2],p[3])] = true
    end)
    -- PBS capacities describe virtual RAM. The guest supplies every program byte.
    local pbs = policy.pbs or ''
    rows(pbs, 6, function(p)
        assert(populated[string.format('%d %d',p[1],p[2])], 'PBS SID is not populated')
        assert(not present[id(p[1],p[2],p[3])], 'Overlapping virtual peripheral')
        present[id(p[1],p[2],p[3])] = true
    end)
    rows(pmic.identities, 7, function(p)
        for bank=0,p[6]-1 do
            for _, range in ipairs(inventory(p[3])[bank+1]) do
                for peripheral=range[1],range[2] do
                    if peripheral ~= 1 and not present[id(p[1],p[2]+bank,peripheral)] then
                        missing[#missing+1] = id(p[1],p[2]+bank,peripheral)
                    end
                end
            end
        end
    end)
    pmic.sdam_peripherals = table.concat(sdam, ';')
    pmic.absent_peripherals = table.concat(missing, ';')
    pmic.regulator_peripherals = regulators
    pmic.pbs_peripherals = pbs
    -- This service models the rehost's three battery channels, not arbitrary PBS.
    pmic.adc_mailbox = policy.adc_mailbox == true
    pmic.battery_millivolts = policy.battery_millivolts or 8800
    pmic.battery_id_ohms = policy.battery_id_ohms or 180000
    return platform
end
