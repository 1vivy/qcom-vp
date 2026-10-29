-- SPDX-License-Identifier: BSD-3-Clause-Clear
local fixture = assert(debug.getinfo(1, 'S').source:match('^@(.*/)'))
dofile(fixture .. '../tme-keys/platform.lua')
platform.tme_sequencer = platform.sequencer
platform.sequencer = nil
platform.gpce = {
    moduletype = 'qcom,qce-v6',
    target_socket = {address=0x01df0000,size=0x10000,bind='&apss_router.initiator_socket'},
    -- This authored composition exposes the stream through a separate test window.
    stream_socket = {address=0x50000000,size=0x10000,bind='&apss_router.initiator_socket'},
}
local profile = dofile(fixture .. 'profile.lua')
local directory = assert(os.getenv('QCOM_VP_PLATFORM_DIR')) .. '/'
dofile(directory .. 'qcom/sm88xx/security-roots.lua')(platform, profile)
