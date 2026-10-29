-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Selected native LLCC v6 topology. Evidence: models/qcom/sm88xx/ddr/phy-evidence.md.
return function(p, soc)
    assert(p.apss_router, 'The APSS router is required for LLCC.')
    assert(not p.llcc, 'The LLCC owner already exists.')
    assert(soc == 'sm8850' or soc == 'sm8845', 'The selected LLCC image is required.')
    local offsets, seen = {}, {}
    local function word(offset)
        if not seen[offset] then
            seen[offset] = true
            offsets[#offsets+1] = string.format('0x%x', offset)
        end
    end
    local function range(first, last, stride)
        for offset = first, last, stride or 4 do word(offset) end
    end
    -- Complete DCB +0x98 address coverage. Values remain guest-owned.
    for _, offset in ipairs({
        0x42090,0x43300,0x43308,0x4330c,0x43310,0x43314,0x43324,
        0x43900,0x43908,0x4390c,0x43910,0x43914,0x45214,
        0x6401c,0x6402c,0x6403c,0x6404c,0x640b0,0x640b4,0x650d8,
        0x68000,0x6811c,0x6c028,0x71018,0x7101c,0x71020,
        0x72000,0x7201c,0x720b4,0x72200,0x72204,0x7220c,
        0x7224c,0x7225c,0x7226c,0x7227c,0x7228c,0x7229c,0x722ac,
        0x72310,0x72320,0x72400,0x72404,0x72408,0x7240c,0x72414,
        0x73044,0x7680c,0x78000,0x78034,0x7c01c,0x7c024,0x7c050,0x7c100,
        0x91000,0x91008,0x9100c,0x91020,0x91024,0x92008,0x92020,0x92024,
        0x93020,0x93024,0x94020,0x94024,0x95000,0x95008,0x96000,0x96004,0x96008,
        0xa6000,0xa6008,0xa600c,0xa6014,0xa603c,
        0x100004,0x100008,0x10000c,0x100028,0x100048,0x10004c,0x100050,0x100054,
        0x10024c,0x100250,0x100258,0x1002c0,0x1002d8,
        0x100338,0x10033c,0x100340,0x100344,0x100348,0x10034c,0x104040,
    }) do word(offset) end
    range(0x65060, 0x650a0)
    if soc == 'sm8850' then
        for _, offset in ipairs({0x7c064,0x7c118,0x7c11c,0x7c120,0x13c09c}) do word(offset) end
    end
    -- Explicit native accesses, including RMW preservation and completion reads.
    range(0x43890, 0x438ac)
    range(0x438e0, 0x438ec)
    for _, offset in ipairs({
        0x43408,0x45040,0x45218,0x45240,0x452a0,0x64074,
        0x640a0,0x640a4,0x640b0,0x640b4,0x640c0,0x640c4,0x640d0,0x640d4,
        0x6422c,0x64234,0x6423c,0x71000,0x71008,0x71010,0x7208c,
        0x7c010,0x7c014,0x7c018,0x7c020,0x7c028,0x7c038,0x7c03c,
        0xa6010,0xa6018,0xa6024,0xa6050,0xa6058,0xa606c,
        0x100088,0x100090,0x103338,0x103350,0x103380,0x104048,
    }) do word(offset) end
    -- Native helpers address 32 cache slices and 32 DARE regions.
    for slice = 0, 31 do
        range(slice * 0x1000, slice * 0x1000 + 8)
        range(0x120000 + slice * 0x1000, 0x120024 + slice * 0x1000)
        for _, offset in ipairs({0x40,0x44,0x4c,0x60,0x64,0x6c,0x74,0x7c,0x90,0x9c,0xa0}) do
            word(0x120000 + slice * 0x1000 + offset)
        end
        word(0x103010 + slice * 8)
        word(0x103014 + slice * 8)
        word(0x103500 + slice * 4)
    end
    -- DARE keys occupy four groups of four entries. Values remain guest-owned.
    for group = 0, 3 do
        for row = 0, 3 do
            local base = 0x110000 + group * 0x1000 + row * 0x100
            word(base)
            word(base + 4)
            range(base + 0x20, base + 0x2c)
        end
    end
    -- Protection controllers retain ownership of both security apertures.
    p.llcc = {moduletype='qcom,llcc-v6', configuration_banks=4,
        configuration_offsets=table.concat(offsets, ','), retained_extent=0x200000,
        opaque_encryption_key_slots=1,
        memory_socket={bind='&apss_router.target_socket'}}
    local views = {
        {'broadcast',0x34800000,0,'target_socket'},
        {'and_broadcast',0x34c00000,0,'status_socket'},
        {'bank0',0x31800000,1,'target_socket'},
        {'bank1',0x32800000,2,'target_socket'},
        {'bank2',0x31c00000,3,'target_socket'},
        {'bank3',0x32c00000,4,'target_socket'},
    }
    -- Native TME 0x43ee78 checks the same banks that XBL programs.
    -- CPUCP 0x1d81a18a..0x1d81a1e4 writes the AND-broadcast view through base 0x1d833068.
    local routers = {'apss_router'}
    if p.tme_router then routers[#routers+1] = 'tme_router' end
    if p.cpucp_router then routers[#routers+1] = 'cpucp_router' end
    for _, router in ipairs(routers) do
        for _, view in ipairs(views) do
            for index, aperture in ipairs({{0,0x60000},{0x64000,0x4c000},{0xc0000,0x140000}}) do
                local prefix = router=='apss_router' and '' or router:gsub('_router$','')..'_'
                local name = prefix..'llcc_'..view[1]..'_'..index
                assert(not p[name], 'The LLCC mapping already exists.')
                p[name] = {moduletype='addrtr',
                    mapped_base_addr=view[3] * 0x200000 + aperture[1],
                    target_socket={address=view[2]+aperture[1],size=aperture[2],
                        relative_addresses=false,bind='&'..router..'.initiator_socket'},
                    initiator_socket={bind='&llcc.'..view[4]}}
            end
        end
    end
    return p
end
