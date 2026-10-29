-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- This compatibility experiment uses the supplied QRD8850 ROMs.
local directory = assert(debug.getinfo(1, 'S').source:match('^@(.*/)'))
local build = dofile(directory .. '../reset.lua')
return function(o)
    local p = build(o)
    assert(not p.fuses.calibration_fuses, 'SM8845 does not use the private calibration page.')
    -- The CPH2771 XBL ChipInfo entry at file 0xb7bf0 selects part 0x2fd.
    p.fuses.jtag_id = 0x002fd0e1
    -- The selected QTI ELF metadata at file 0x2050 requires SoC version 0xa022.
    -- CPR table 0x14915f30 accepts revision 1.0 through 1.255.
    -- Revision 1.0 is virtual policy, not a physical capture.
    local hardware_version=0xa0220100
    p.fuses.chip_id=hardware_version
    p.tcsr_identity.hardware_version=hardware_version
    p.tme_boot_status.hardware_version=hardware_version
    if o.tme_runtime_api_map then
        dofile(directory..'runtime.lua')(p,o)
    end
    dofile(directory .. '../security-roots.lua')(p, o)
    return p
end
