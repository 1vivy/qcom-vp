-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Explicit virtual services for the selected OEM PMIC callbacks after main PSI.
-- Values describe virtual hardware, not physical revisions or captured resets.
local function rows(text, count, consume)
    for row in (text or ''):gmatch('[^;]+') do
        local values = {}
        for token in row:gmatch('%S+') do
            values[#values+1] = assert(tonumber(token), 'Invalid post-init PMIC number')
        end
        assert(#values == count, 'Invalid post-init PMIC row')
        consume(values, row)
    end
end

return function(platform, policy)
    local pmic = assert(platform.xbl_pmic, 'Construct xbl_pmic before post-init services')
    assert(pmic.regulator_peripherals == '' and not pmic.gpio_peripherals and
           not pmic.clock_peripherals and not pmic.rtc_status_peripherals and
           not pmic.misc_peripherals,
           'Post-init PMIC services are already configured')
    local exposed, owned = {}, {}
    local function peripheral(bus, sid, number)
        exposed[string.format('%d %d %d', bus, sid, number)] = true
    end
    local function byte(bus, sid, number, offset)
        owned[string.format('%d %d %d', bus, sid, number * 0x100 + offset)] = true
    end
    local function revision(bus, sid, number)
        peripheral(bus, sid, number)
        for offset=0,6 do byte(bus, sid, number, offset) end
    end
    local function irq(bus, sid, number)
        for _, offset in ipairs({0x10,0x11,0x12,0x13,0x14,0x15,0x16,
                                 0x18,0x19,0x1a,0x1b}) do
            byte(bus, sid, number, offset)
        end
    end
    -- The PON service owns reset requests and the cold manufacturing-test option.
    -- This also removes sparse PSI aliases of the same controls on secondary SIDs.
    rows(pmic.pon_peripherals, 3, function(p)
        for offset=0x40,0x4b do byte(p[1],p[2],p[3],offset) end
        for _, offset in ipairs({0x52,0x53,0x74,0x75,0x84,0x86,0x8f}) do
            byte(p[1],p[2],p[3],offset)
        end
        irq(p[1],p[2],p[3])
    end)
    -- Full declared banks keep the driver's counted GPIO indices coherent.
    -- The guest supplies all mode, source, pull, drive and enable requests.
    pmic.gpio_peripherals = policy.gpios
    rows(policy.gpios, 4, function(p)
        for number=p[3],p[3]+p[4]-1 do
            revision(p[1],p[2],number)
            byte(p[1],p[2],number,8)
            for offset=0x40,0x46 do byte(p[1],p[2],number,offset) end
            irq(p[1],p[2],number)
        end
    end)
    local rails = {}
    rows(policy.regulators, 5, function(p)
        for number=p[3],p[3]+p[4]-1 do
            rails[#rails+1] = string.format('%d %d %d %d',p[1],p[2],number,p[5])
            revision(p[1],p[2],number)
            for _, offset in ipairs({8,9,0xa,0xb,0x39,0x3a,0x40,0x41,0x42,0x43,0x45,
                0x46,0x47,0x48,0x4a,0x50,0x51,0x60,0x68,0x69,0x88,0x89,
                0x8b,0x98,0x99,0xa0}) do
                byte(p[1],p[2],number,offset)
            end
        end
    end)
    pmic.regulator_peripherals = table.concat(rails, ';')
    -- Identified non-functional peripherals. Type 20 is MISC, and the MISC
    -- subtype table has no 0x6b entry. The service reports the observed
    -- revision and the recorded read-only zero span only.
    pmic.misc_peripherals = policy.misc or ''
    rows(policy.misc, 5, function(p)
        revision(p[1],p[2],p[3])
        for offset=8,0x1f do byte(p[1],p[2],p[3],offset) end
    end)
    -- Driver post-init calls CALRC SMPL configuration without an absence guard:
    -- SM8850 1483bf4c/1483bf60, SM8845 1483a160/1483a174.
    -- XO must be discovered first. SM8850 also requests BB clock index10.
    pmic.clock_peripherals = policy.clocks
    rows(policy.clocks, 4, function(p)
        revision(p[1],p[2],p[3])
        for _, offset in ipairs({0x44,0x46,0x48,0x4a,0x4d,0x50,0x69}) do
            byte(p[1],p[2],p[3],offset)
        end
    end)
    -- Only the loss-of-power interface is used before this callback returns.
    -- Cold-clear status is not a claim of valid time or a running counter.
    pmic.rtc_status_peripherals = '0 0 0x61'
    revision(0,0,0x61)
    byte(0,0,0x61,8)
    byte(0,0,0x61,9)
    local retained = {}
    rows(pmic.absent_peripherals, 3, function(p, row)
        if not exposed[string.format('%d %d %d',p[1],p[2],p[3])] then
            retained[#retained+1] = row
        end
    end)
    pmic.absent_peripherals = table.concat(retained, ';')
    retained = {}
    rows(pmic.registers, 5, function(p, row)
        if owned[string.format('%d %d %d',p[1],p[2],p[3])] then
            assert(p[4] == 0, 'Post-init service conflicts with a sparse reset value')
        else
            retained[#retained+1] = row
        end
    end)
    pmic.registers = table.concat(retained, ';')
    -- SM8850 PSI broadcasts these requests to the primary PMIC group.
    -- Another board PSI configuration uses the same addresses on individual primary SIDs.
    -- These bytes retain requests. They do not report completion or readiness.
    local configured = {}
    rows(pmic.registers, 5, function(p)
        configured[string.format('%d %d %d',p[1],p[2],p[3])] = true
    end)
    local broadcast = {}
    rows(pmic.identities, 7, function(p)
        for _, address in ipairs({0x546,0x5da,0x647}) do
            local key = string.format('%d %d %d',p[1],p[2],address)
            if not configured[key] then
                broadcast[#broadcast+1] = key .. ' 0 0xff'
            end
        end
    end)
    if #broadcast > 0 then
        pmic.registers = pmic.registers .. ';' .. table.concat(broadcast, ';')
    end
    retained = {}
    rows(pmic.disconnected_irq_peripherals, 3, function(p, row)
        if not owned[string.format('%d %d %d',p[1],p[2],p[3]*0x100+0x10)] then
            retained[#retained+1] = row
        end
    end)
    pmic.disconnected_irq_peripherals = table.concat(retained, ';')
    return platform
end
