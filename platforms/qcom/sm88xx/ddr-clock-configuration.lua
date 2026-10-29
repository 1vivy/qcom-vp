-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Preserve masked controls between the logged configuration windows.
return function(p, retained)
    local windows = {
        {0x310a8000, 0x178}, {0x310a817c, 0x140},
        {0x310a82c0, 0x12c}, {0x310a83f0, 0xc10},
        -- DDR_SS_REGS spans 0x400 bytes. The early VDDQ owner retains +0x12c.
        {0x31080000, 0x12c}, {0x31080130, 0x2d0},
    }
    for index, window in ipairs(windows) do
        local name = 'ddr_phy_clock_configuration'..index
        assert(not p[name], 'The DDR clock configuration owner already exists.')
        window.owner = {moduletype='qcom,ddr-phy-program-v1',
            retained_extent=window[2],
            target_socket={address=window[1], size=window[2], bind='&apss_router.initiator_socket'}}
        window.words = {}
        p[name] = window.owner
    end
    local reference = assert(p.ddr_phy_reference, 'The DDR reference owner is required.')
    windows[#windows+1] = {0x310a9000, 0x1000, owner=reference,
        words={reference.configuration_offsets}}
    for _, address in ipairs(retained) do
        local owned = false
        for _, window in ipairs(windows) do
            if address >= window[1] and address < window[1] + window[2] then
                window.words[#window.words+1] = string.format('0x%x', address - window[1])
                owned = true
                break
            end
        end
        if not owned then
            local name = 'ddr_phy_retained_'..address
            assert(not p[name], 'The DDR retained control already exists: '..name)
            p[name] = {moduletype='qcom,retained-boot-control-v1', writable_mask=0xffffffff, reset_value=0,
                target_socket={address=address, size=4, bind='&apss_router.initiator_socket'}}
        end
    end
    for _, window in ipairs(windows) do
        window.owner.configuration_offsets = table.concat(window.words, ',')
    end
end
