-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Existing SM8850 virtual geometry. OEM clients: 0x4555e4. DBGAR: 0x449288.
-- Permission arrays: 0x45b2b8. Preserve the established virtual aperture bounds.
local debug_addresses={
    0x0c290304,0x0c294304,0x310c0304,0x01e50304,0x01524304,0x01530304,0x01534304,0x01520304,
    0x01540304,0x01538304,0x07476304,0x07472304,0x002f0304,0x34c60304,0x34cb0304,0x310c4304,
    0x0061c304,0x17e04304,0x0c41c304,0x17e00304,0x310cc304,0x010d0304,0x0c292304,0x07474304,
    0x310c8304,0x00d00304,0x01f00304,0x0f000304,0x20c28304,0x20c2a304,0x07f20304}
local fixed={[13]=true,[22]=true,[28]=true,[30]=true}
local regions={[3]=128,[4]=128,[10]=256,[13]=256,[14]=128,[28]=1024}
local protection={}
for i,address in ipairs(debug_addresses) do
    protection[i]={address=address-0x304,fixed=fixed[i],regions=regions[i] or 64,
        address_bits=(i==5 or i==9) and 40 or 32,
        alignment_bits=i==15 and 0 or 12}
end
-- The selected AC tables supplement the independent runtime descriptor cohort.
-- Evidence: models/qcom/sm88xx/tme_protection/namespace-platform-evidence.md.
local xbl_protection={
    {address=0x01dc1000,fixed=true,regions=64,address_bits=32,alignment_bits=12},
    {address=0x01de6000,fixed=true,regions=64,address_bits=32,alignment_bits=12},
    {address=0x01df8000,fixed=true,regions=64,address_bits=32,alignment_bits=12},
    {address=0x03e00000,fixed=true,regions=64,address_bits=32,alignment_bits=12},
    {address=0x088de000,fixed=true,regions=64,address_bits=32,alignment_bits=12},
    {address=0x34c60000,regions=70,address_bits=37,alignment_bits=12},
    {address=0x0061c000,regions=64,address_bits=32,alignment_bits=12,
        aperture_size=0x40000,source=0x1494730c},
    {address=0x010bc000,regions=64,address_bits=32,alignment_bits=12,
        aperture_size=0x4000,source=0x14947318},
}
return {
    -- OEM 0x435372 selects three channel records at this address.
    channels=0x448f1c,
    plls={0x021e8000,0x081ae000,0x080c0000,0x04100000},
    branches={0x04147000,0x04147170,0x041a4004,0x04125014,0x04125010,
        0x0412500c,0x04125008,0x04125004,0x041a4008},
    clients={0x07f20000,0x01740000,0x07478000,0x003fe000,0x0152c000,0x01dc1000,0x01de6000,0x01df8000,
        0x09c00000,0x0747a000,0x03e00000,0x03f20000,0x32460000,0x324b0000,0x32060000,0x320b0000,
        0x34860000,0x348b0000,0x0691c000,0x0693c000,0x06cbc000,0x06cb6000,0x06ef8000,0x07f30000,
        0x06cb8000,0x06cb4000,0x06cba000,0x31860000,0x318b0000,0x32860000,0x328b0000,0x31c60000,
        0x31cb0000,0x32c60000,0x32cb0000,0x09b21000,0x09b1f000,0x0a90d000,0x041f8000,0x0061a000,
        0x01d0e000,0x0747c000,0x100e0000,0x010bc000,0x088de000,0x00ac8000,0x00a00000,0x008c8000,
        0x00800000,0x019c8000},
    debug_addresses=debug_addresses,protection=protection,
    xbl_protection=xbl_protection,
    -- RADIO/tz_ac_config.img policy records select these types and capacities.
    -- Sources are AC file offsets. See models/qcom/sm88xx/tme_protection/tz-inventory-evidence.md.
    -- 0x01ac8000 and 0x01a00000 have no policy array. Their type is virtual.
    tz_protection={
        {address=0x03f20000,fixed=true,regions=64,address_bits=32,alignment_bits=12,source=0x2210},
        {address=0x041f8000,fixed=true,regions=64,address_bits=32,alignment_bits=12,source=0x2300},
        {address=0x0061a000,fixed=true,regions=64,address_bits=32,alignment_bits=12,source=0x2328},
        {address=0x100e0000,fixed=true,regions=64,address_bits=32,alignment_bits=12,source=0x23f0},
        {address=0x00a00000,fixed=true,regions=64,address_bits=32,alignment_bits=12,source=0x2468},
        {address=0x00800000,fixed=true,regions=64,address_bits=32,alignment_bits=12,source=0x24b8},
        {address=0x01900000,fixed=true,regions=64,address_bits=32,alignment_bits=12,source=0x2508},
        {address=0x01ac8000,regions=64,address_bits=32,alignment_bits=12,source=0x2530},
        {address=0x01a00000,regions=64,address_bits=32,alignment_bits=12,source=0x1db0},
        {address=0x07470000,regions=64,address_bits=32,alignment_bits=12,source=0x25d0},
        {address=0x01890000,fixed=true,regions=64,address_bits=32,alignment_bits=12,source=0x2620},
        {address=0x07850000,regions=64,address_bits=32,alignment_bits=12,source=0x2648},
        {address=0x07840000,regions=64,address_bits=32,alignment_bits=12,source=0x2670},
        {address=0x07a00000,fixed=true,regions=64,address_bits=32,alignment_bits=12,source=0x2698},
        {address=0x07900000,fixed=true,regions=64,address_bits=32,alignment_bits=12,source=0x26c0},
        {address=0x01d8a000,fixed=true,regions=64,address_bits=32,alignment_bits=12,source=0x2738},
        -- Region 1 ends at 0x100000000. A 32-bit encoded zero end needs this aperture.
        {address=0x07f30000,regions=64,address_bits=32,alignment_bits=12,
            aperture_size=0x100000000,source=0x2288},
    },
    fault_masks={0x05fc40ec,0x05fc40f0,0x05fc40f4,0x05fc40f8,0x05fc40fc,
        0x05fc4100,0x05fc4104,0x05fc4108,0x05fc410c,0x05fc4110,0x05fc4114,
        0x05fc4118,0x05fc411c,0x05fc4120,0x05fc4124,0x05fc4128,0x05fc412c,
        0x05fc4130,0x05fc4134,0x05fc4138,0x05fc413c,0x05fc4140,0x05fc4144,
        0x05fc4148,0x05fc414c,0x05fc4150,0x05fc4154},
    external_fault_descriptor={0x00c0040c,0x00c00410,0x00c00408},
}
