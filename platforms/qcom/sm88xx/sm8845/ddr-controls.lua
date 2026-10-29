-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- CPH2771 selected DCB address sets. Values remain guest-owned.
-- Source coordinates and virtual effects: models/qcom/sm88xx/ddr/controls-evidence.md.
local directory=assert(debug.getinfo(1,'S').source:match('^@(.*/)'))
return function(p)
    assert(p.apss_router, 'The APSS router is required for DDR controls.')
    local function retain(address, mask)
        local name = 'ddr_frontend_'..address
        assert(p[name] == nil, 'DDR frontend control already exists: '..name)
        p[name] = {moduletype='qcom,retained-boot-control-v1',
            writable_mask=mask or 0xffffffff, reset_value=0,
            target_socket={address=address, size=4, bind='&apss_router.initiator_socket'}}
    end
    -- Selected boot-pass consumers and later cleanup. Explicit cold-boot state.
    -- Preserve all guest bits. Do not assert first-pass completion or retention.
    for _, address in ipairs({
        0x0c2f0024, 0x0c2f1000, 0x3108012c,
    }) do retain(address) end
    -- XBL 0x1489f140..0x1489f1f8. UFS retains power and branch ownership.
    for _, word in ipairs({
        {0x153028,3}, {0x15301c,3}, {0x152000,4},
        {0x14b004,1}, {0x14b008,1}, {0x310a8178,1},
    }) do retain(word[1],word[2]) end
    local function words(offsets, base)
        local result = {}
        for _, item in ipairs(offsets) do
            local first = type(item) == 'number' and item or item[1]
            local last = type(item) == 'number' and item or item[2]
            for offset=first,last,4 do
                result[#result+1] = string.format('0x%x', offset - (base or 0))
            end
        end
        return table.concat(result, ',')
    end
    -- DCB +0x80, interpreted at XBL 0x148ac134.
    local shrm_frontend = {
        0x160a0, 0x160b0, {0x16400,0x1640c}, {0x16420,0x1642c}, {0x16440,0x1644c}, {0x16460,0x1646c},
        {0x16480,0x1648c}, {0x164a0,0x164ac}, {0x164c0,0x164cc}, {0x164e0,0x164ec}, {0x16500,0x1650c}, {0x16800,0x16820},
        {0x16840,0x16860}, {0x16880,0x168a0}, {0x168c0,0x168e0}, {0x16900,0x16920}, 0x16940, 0x16944,
        0x16950, 0x169fc, 0x16a30, 0x16f00, 0x16f04, 0x16f10,
        0x16f14, 0x16f20, 0x16f24, 0x16f30, 0x16f34, 0x16f40,
        0x16f50, 0x16f54, 0x16f5c, 0x16f60, 0x16f78, 0x17050,
        0x18000,
    }
    local shrm_words = {'0x8dc', words(shrm_frontend, 0x16070)}
    p.ddr_shrm_configuration = {
        moduletype='qcom,ddr-phy-program-v1',
        configuration_offsets=table.concat(shrm_words, ','),
        retained_extent=0x2f90,
        target_socket={address=0x31076070, size=0x2f90, bind='&apss_router.initiator_socket'},
    }
    -- DCB +0x88 uses base+0x100. DCB +0x8c uses the unadjusted base.
    local global_offsets = {
        0x64, 0xa4, 0xc0, 0xc4, {0x130,0x138}, 0x140,
        0x154, 0x21c, 0x240, 0x248, 0x250, 0x258,
        0x260, 0x268, 0x288, 0x28c, 0x308, 0x410,
        0x448, 0x450, 0x604, 0x608, 0x610, 0x618,
        0x658, 0x674, 0x7f0, 0x7f4,
    }
    p.ddr_global_configuration = {
        moduletype='qcom,ddr-phy-program-v1',
        configuration_offsets=words(global_offsets),
        retained_extent=0x1000,
        target_socket={address=0x34bc0000, size=0x1000, bind='&apss_router.initiator_socket'},
    }
    dofile(directory..'../ddr-mccc.lua')(p)
    assert(p.ddr_frontend_program == nil, 'DDR frontend program already exists.')
    p.ddr_frontend_program = {
        moduletype='qcom,ddr-frontend-program-v1',
        configuration_offsets='0x20,0x50,0x54,0x58,0x70,0x78,0x80,0x88,0x90,0x98,0x138,0x13c,0x140,0x144,0x158,0x2a8',
        apply_offset=0x250, retained_offset=0x210,
        target_socket={address=0x310ba000, size=0x2ac, bind='&apss_router.initiator_socket'},
    }
    return p
end
