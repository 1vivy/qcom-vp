-- SPDX-License-Identifier: BSD-3-Clause-Clear
local directory=assert(debug.getinfo(1,'S').source:match('^@(.*/)'))
dofile(directory..'../dcp-spmi/platform.lua')
platform.pmic.virtual_absent_discovery=true
platform.pmic.exploratory_configuration=true
platform.pmic.registers=platform.pmic.registers..';0 0 0x3040 0 0xff'
