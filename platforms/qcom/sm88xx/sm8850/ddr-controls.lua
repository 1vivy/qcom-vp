-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Selected DDR controls with logged configuration substitutes.
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
    -- XBL 0x148a082c..0x148a08e4: retained requests only, no root-ready claims.
    -- UFS owns GDSC 0x177004 and live branches 0x177028/0x17707c.
    for _, word in ipairs({
        {0x153028, 3}, {0x15301c, 3}, {0x152000, 4},
        {0x14b004, 1}, {0x14b008, 1}, {0x310a8178, 1},
    }) do
        retain(word[1], word[2])
    end
    -- DCB pair +0x80: base 0x31060000, interpreted as offset/value records.
    local shrm_frontend = {
        0x160a0, 0x160b0, 0x16400, 0x16404, 0x16408, 0x1640c,
        0x16420, 0x16424, 0x16428, 0x1642c, 0x16440, 0x16444,
        0x16448, 0x1644c, 0x16460, 0x16464, 0x16468, 0x1646c,
        0x16480, 0x16484, 0x16488, 0x1648c, 0x164a0, 0x164a4,
        0x164a8, 0x164ac, 0x164c0, 0x164c4, 0x164c8, 0x164cc,
        0x164e0, 0x164e4, 0x164e8, 0x164ec, 0x16500, 0x16504,
        0x16508, 0x1650c, 0x16800, 0x16804, 0x16808, 0x1680c,
        0x16810, 0x16814, 0x16818, 0x1681c, 0x16820, 0x16840,
        0x16844, 0x16848, 0x1684c, 0x16850, 0x16854, 0x16858,
        0x1685c, 0x16860, 0x16880, 0x16884, 0x16888, 0x1688c,
        0x16890, 0x16894, 0x16898, 0x1689c, 0x168a0, 0x168c0,
        0x168c4, 0x168c8, 0x168cc, 0x168d0, 0x168d4, 0x168d8,
        0x168dc, 0x168e0, 0x16900, 0x16904, 0x16908, 0x1690c,
        0x16910, 0x16914, 0x16918, 0x1691c, 0x16920, 0x16940,
        0x16944, 0x16950, 0x169fc, 0x16a30, 0x16f00, 0x16f04,
        0x16f10, 0x16f14, 0x16f20, 0x16f24, 0x16f30, 0x16f34,
        0x16f40, 0x16f50, 0x16f54, 0x16f5c, 0x16f60, 0x16f78,
        0x17050, 0x18000,
    }
    local shrm_words = {'0x8dc'}
    for _, offset in ipairs(shrm_frontend) do
        shrm_words[#shrm_words+1] = string.format('0x%x', offset - 0x16070)
    end
    p.ddr_shrm_configuration = {
        moduletype='qcom,ddr-phy-program-v1',
        configuration_offsets=table.concat(shrm_words, ','),
        retained_extent=0x2f90,
        target_socket={address=0x31076070, size=0x2f90, bind='&apss_router.initiator_socket'},
    }
    -- DCB pairs +0x88/+0x8c. The first uses base+0x100.
    local retained_offsets = {
        0x64, 0xa4, 0xc0, 0xc4, 0x130, 0x134,
        0x138, 0x140, 0x154, 0x21c, 0x240, 0x248,
        0x250, 0x258, 0x260, 0x268, 0x288, 0x308,
        0x448, 0x450, 0x604, 0x608, 0x610, 0x618,
        0x658, 0x7f0, 0x7f4,
    }
    local global_words = {}
    for _, offset in ipairs(retained_offsets) do
        global_words[#global_words+1] = string.format('0x%x', offset)
    end
    p.ddr_global_configuration = {
        moduletype='qcom,ddr-phy-program-v1',
        configuration_offsets=table.concat(global_words, ','),
        retained_extent=0x1000,
        target_socket={address=0x34bc0000, size=0x1000, bind='&apss_router.initiator_socket'},
    }
    dofile(directory..'../ddr-mccc.lua')(p)
    assert(p.ddr_frontend_program == nil, 'DDR frontend program already exists.')
    p.ddr_frontend_program = {
        moduletype='qcom,ddr-frontend-program-v1',
        configuration_offsets='0x20,0x50,0x54,0x58,0x70,0x78,0x80,0x88,0x90,0x98,0x138,0x13c,0x140,0x144,0x158,0x2a8',
        profile_offsets='0x29c,0x2a0',
        profile_values='0x139,0x1dd',
        apply_offset=0x250, retained_offset=0x210,
        target_socket={address=0x310ba000, size=0x2ac, bind='&apss_router.initiator_socket'},
    }
    return p
end
