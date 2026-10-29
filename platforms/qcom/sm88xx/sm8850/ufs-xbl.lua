-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Extend the existing ROM PHY contract. Keep its controller, LUNs, and DMA route.
-- Selected SM8845 uses this same contract. See models/qcom/ufs/README.md.
return function(p)
    assert(type(p) == 'table' and p.ufs and p.ufs.virtual_phy == true,
           'The existing virtual UFS PHY is required.')
    assert(p.ufs.phy_axi_clock == nil,
           'The UFS source clocks are already configured.')
    local sources = {
        phy_axi_clock = 0x177018,
        phy_ahb_clock = 0x177028,
        phy_ice_clock = 0x17707c,
        phy_unipro_clock = 0x17706c,
        phy_aux_clock = 0x1770bc,
        phy_aggre_clock = 0x1770f0,
    }
    for parameter, address in pairs(sources) do
        local name = 'ufs_branch_' .. address
        assert(p[name] and p[name].moduletype == 'qcom,clock-branch-v1',
               'The live UFS source branch is required: ' .. name)
        p.ufs[parameter] = name
    end
    -- Both boot stages use the same QMP v4 register backing and link state.
    -- This is an explicit virtual host revision, not a measured chip revision.
    p.ufs.virtual_hw_revision = 0x30010000
    return p
end
