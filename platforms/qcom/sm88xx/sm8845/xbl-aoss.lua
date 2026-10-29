-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- CPH2771 AOSS/LPASS sources, roots, controls, and REFGEN startup group.
-- Address and field evidence: models/qcom/sm88xx/tme_runtime_abi/evidence.md.
return function(p)
    assert(p.apss_router,'The APSS router is required for CPH2771 AOSS clocks.')
    local function add(name,address,size,module)
        assert(p[name]==nil,'Duplicate CPH2771 AOSS module: '..name)
        module.target_socket={address=address,size=size,bind='&apss_router.initiator_socket'}
        p[name]=module
    end
    -- Source descriptors 0x148eeff0, 0x148ef028, 0x148ef060, and 0x148ef098.
    -- The last two descriptors name consecutive banks of one existing PLL protocol.
    add('xbl_aoss_pll0',0x0c2a0000,0x38,
        {moduletype='qcom,gcc-taycan-eko-t-pll-v1',reference_hz=19200000})
    add('xbl_aoss_pll1',0x0c2a1000,0x40,
        {moduletype='qcom,aoss-pongo-pll-v1',reference_hz=19200000,calibration_ratio=8})
    add('xbl_lpass_plls',0x310a0000,0x1038,
        {moduletype='qcom,gcc-taycan-eko-t-pll-v1',reference_hz=19200000,pll_count=2})
    -- Source 14 is initialized directly by the 17-source loop at 0x1484d654.
    -- Its voted OffNoReCAL path observes the existing global vote, never a second owner.
    add('xbl_external_pll',0x06e00000,0x38,{
        moduletype='qcom,gcc-taycan-eko-t-pll-v1',reference_hz=19200000,
        external_vote_path='gcc_plls',external_vote_mask=1,
        coarse_calibration_mask=1,virtual_coarse_calibration_code=0,
    })
    -- Descriptor, command address, source object, selected output, mux selector.
    local roots={
        {0x148ef3c8,0x0c2a8084,'xbl_aoss_pll0','primary_selector',4},
        {0x148ef408,0x0c2a80e8,'xbl_aoss_pll1','calibration_selector',2},
        {0x148ef448,0x0c2a80a0,'xbl_aoss_pll1','primary_selector',6},
        {0x148ef488,0x0c2a818c,'xbl_aoss_pll0','primary_even_selector',1},
        {0x148ef4c8,0x0c2a81a4,'xbl_aoss_pll1','primary_even_selector',3},
    }
    for i,root in ipairs(roots) do
        local module={moduletype='qcom,rcg-v1',pll_path=root[3],crystal_selector=0,
            require_root_enable=false}
        module[root[4]]=root[5]
        add('xbl_aoss_root'..(i-1),root[2],0x14,module)
    end
    -- Offset from 0x0c2a0000, writable mask, selected guest store PC.
    -- All words cold-reset to zero. They retain only the observed policy fields.
    -- PLL_CLK_SEL retains the reference selection, not a physical clock netlist.
    local controls={
        {0x901c,0x1,0x1484d1dc},{0x9024,0x1,0x1484d1e8},
        {0x80b8,0xf,0x1484d308},{0x80c0,0xf,0x1484d318},
        {0x80e4,0xf,0x1484d328},{0x80c8,0xf,0x1484d334},
        {0x80d0,0xf,0x1484d340},{0x80d8,0xf,0x1484d34c},
        {0x80e0,0xf,0x1484d358},{0x81d0,0xf,0x1484d364},
        {0x81dc,0xf,0x1484d370},{0x81ec,0xf,0x1484d37c},
        {0x900c,0x1,0x1484d3d8},
    }
    for _,control in ipairs(controls) do
        add('xbl_aoss_configuration_'..control[1],0x0c2a0000+control[1],4,
            {moduletype='qcom,retained-boot-control-v1',writable_mask=control[2],reset_value=0})
    end
    -- XBL 0x1484d3e0..0x1484d42c: two blocks, not the SM8850 three-block table.
    -- Readiness follows the tune write and bounded virtual calibration, never a ready word.
    for i,address in ipairs({0x00ff0000,0x00fa0000}) do
        add('xbl_refgen_'..i,address,0xac,{moduletype='qcom,refgen-nxgen-v1',
            virtual_power=true,calibration_policy='bounded-virtual',calibration_delay_us=5})
    end
    return p
end
