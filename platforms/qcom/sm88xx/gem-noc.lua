-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Selected ICB programs configure these GEM NoC policy blocks.
-- Evidence: models/qcom/sm88xx/ddr/phy-evidence.md. The guest supplies all policy values.
return function(p)
    assert(p.apss_router, 'The APSS router is required for GEM NoC configuration.')
    for index, block in ipairs({
        {0x31102000,'0x8'}, {0x31107000,'0x30'},
        {0x31181000,'0x8'}, {0x31187000,'0x30'},
        {0x31214000,'0x8'}, {0x31215000,'0x8'},
        {0x31228000,'0x8,0x50'}, {0x31229000,'0x8,0x50'},
    }) do
        local name = 'gem_noc_configuration'..index
        assert(not p[name], 'The GEM NoC configuration owner already exists.')
        p[name] = {moduletype='qcom,ddr-phy-program-v1',retained_extent=0x100,
            configuration_offsets=block[2],
            target_socket={address=block[1],size=0x100,bind='&apss_router.initiator_socket'}}
    end
end
