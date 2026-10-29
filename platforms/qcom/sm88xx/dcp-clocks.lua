-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Descriptor addresses, rate tables, and access PCs: models/qcom/sm88xx/dcp/clocks-evidence.md.
local directory=assert(debug.getinfo(1,'S').source:match('^@(.*/)'))
local append_vote=dofile(directory..'clock-branch-votes.lua')
return function(p, soc)
    assert(soc=='sm8850' or soc=='sm8845', 'Unsupported DCP clock SoC')
    assert(p.apss_router and p.gcc_plls, 'The APSS router and GCC PLL bank are required.')
    assert(p.dcp_bootstrap, 'Compose the DCP bootstrap before its clock parents.')
    local function owner(address,size)
        for name,module in pairs(p) do
            if type(module)=='table' then
                for _,socket in pairs(module) do
                    if type(socket)=='table' and socket.bind=='&apss_router.initiator_socket'
                        and type(socket.address)=='number' and type(socket.size)=='number'
                        and address<socket.address+socket.size and socket.address<address+size then
                        if module.moduletype=='addrtr' then
                            for target_name,target in pairs(p) do
                                if type(target)=='table' and target.moduletype~='addrtr'
                                    and target.target_socket and target.target_socket.address==address then
                                    return target_name,target,target.target_socket
                                end
                            end
                        end
                        return name,module,socket
                    end
                end
            end
        end
    end
    local function map(name,address,size,module)
        assert(not owner(address,size), string.format('DCP clock overlap at 0x%x',address))
        assert(not p[name], 'Duplicate DCP clock module: '..name)
        module.target_socket={address=address,size=size,bind='&apss_router.initiator_socket'}
        -- An alias receives the absolute address that its mapped base translates.
        if module.moduletype=='addrtr' then module.target_socket.relative_addresses=false end
        p[name]=module
        return name,module
    end
    local function reserved(address)
        local page=address-address%0x1000
        return page==0x09bab000 or page==0x09bae000 or page==0x09bb4000
    end
    -- DCP has an independent GPLL vote. The existing owner combines all masters.
    p.gcc_plls.vote_count=math.max(p.gcc_plls.vote_count,3)
    p.gcc_plls.vote_socket.size=math.max(p.gcc_plls.vote_socket.size,0xa004)
    map('dcp_gpll_vote',0x15a020,4,{moduletype='addrtr',mapped_base_addr=0x15c020,
        initiator_socket={bind='&gcc_pll_router.target_socket'}})
    map('dcp_display_plls',0x09ba2000,0x1038,
        {moduletype='qcom,gcc-taycan-eko-t-pll-v1',pll_count=2})
    map('dcp_display_pongo',0x09ba4000,0x40,{moduletype='qcom,aoss-pongo-pll-v1',reference_hz=32768})
    -- The reset reference has no register or completion bit.
    local reference='xbl_boot_clock_reference'
    assert(p[reference], 'The shared reference clock is required.')

    if soc=='sm8850' then
        local roots={
            {0x9baa0d4,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0212798
            {0x9baa0ec,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa02127e4
            {0x9baa378,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=4,primary_even_selector=6,primary_index=1,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0212830
            {0x9baa194,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa021287c
            {0x9baa1b0,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa02128c8
            {0x9baa248,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0212914
            {0x9baa1fc,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0212960
            {0x9baa218,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa02129ac
            {0x9baa230,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa02129f8
            {0x9baa2ac,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0212a44
            {0x9baa290,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0212a90
            {0x9baa260,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0212adc
            {0x9baa278,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0212b28
            {0x9baa310,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0212b74
            {0x9baa2c4,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0212bc0
            {0x9baa2e0,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0212c0c
            {0x9baa2f8,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0212c58
            {0x9baa35c,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0212ca4
            {0x9baa340,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0212cf0
            {0x9baa328,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0212d3c
            {0x9baa1cc,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0212d88
            {0x9baa1e4,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0212dd4
            {0x9baa164,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=1,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0212e20
            {0x9baa11c,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0212e6c
            {0x9baa134,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0212eb8
            {0x9baa14c,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0212f04
            {0x9baa17c,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0212f50
            {0x9baa104,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_pongo',crystal_selector=8,primary_selector=2,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0212f9c
            {0x9bb3004,0x4,{moduletype='qcom,clock-integer-divider-v1',pll_path='dcp_display_plls',reference_source=true,field_shift=16,field_width=9}}, -- descriptor 0xa0212fe8
            {0x9bb0044,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0213034
            {0x17206c,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa02146e8
            {0x164400,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0214734
            {0x10c0c8,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0214780
            {0x10c1f8,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa02147cc
            {0x113014,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0214818
            {0x107008,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0214864
            {0x107024,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa02148b0
            {0x107040,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa02148fc
            {0x10705c,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0214948
            {0x107078,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0214994
            {0x11316c,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa02149e0
            {0x1088c0,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0214a2c
            {0x108014,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0214a78
            {0x108150,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0214ac4
            {0x1082a0,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0214b10
            {0x1083dc,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0214b5c
            {0x108518,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0214ba8
            {0x108654,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0214bf4
            {0x108790,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0214c40
            {0x1132c4,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0214c8c
            {0x10e014,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0214cd8
            {0x10e150,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0214d24
            {0x10e28c,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0214d70
            {0x10e3c8,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0214dbc
            {0x10e504,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0214e08
            {0x11341c,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0214e54
            {0x19877c,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa0214ea0
            {0x198014,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0214eec
            {0x198150,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0214f38
            {0x19828c,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0214f84
            {0x1983c8,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0214fd0
            {0x198504,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa021501c
            {0x198640,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0215068
            {0x113574,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa02150b4
            {0x199014,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0215100
            {0x199150,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa021514c
            {0x19928c,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0215198
            {0x1993c8,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa02151e4
            {0x199504,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa0215230
        }
        for _,r in ipairs(roots) do
            -- GCC_REG starts at system 0x110000, after its sixteen PLL/PHY slots.
            if r[1]<0x200000 then r[1]=r[1]+0x10000 end
            if not owner(r[1],r[2]) then map('dcp_root_'..r[1],r[1],r[2],r[3]) end
        end
        local branches={
            {'disp_cc_dcp_sleep_clk',0x9bb400c,0x0,0x0,0x0},
            {'disp_cc_debug_clk',0x9baf004,0x0,0x0,0x0},
            {'disp_cc_mdss_spdm_debug_clk',0x9bb0020,0x0,0x0,0x0},
            {'disp_cc_sleep_clk',0x9bb0064,0x0,0x0,0x0},
            {'disp_cc_dcp_dbg_clk',0x9bb4028,0x9bb4010,0x0,0x0},
            {'disp_cc_dcp_f_clk',0x9bb402c,0x9bb4010,0x0,0x0},
            {'disp_cc_dcp_proc_h_clk',0x9bb4030,0x9bb4010,0x0,0x0},
            {'disp_cc_dcp_ss_h_clk',0x9bb4040,0x9bb4010,0x0,0x0},
            {'disp_cc_dcp_dbg_apb_clk',0x9bb405c,0x9bb4044,0x0,0x0},
            {'disp_cc_dcp_dbg_atb_clk',0x9bb4094,0x9bb407c,0x0,0x0},
            {'disp_cc_dcp_dbg_qts_clk',0x9bb4078,0x9bb4060,0x0,0x0},
            {'disp_cc_esync0_clk',0x9baa0cc,0x9baa0d4,0x0,0x0},
            {'disp_cc_esync1_clk',0x9baa0d0,0x9baa0ec,0x0,0x0},
            {'disp_cc_dcp_ahb_clk',0x9bb4004,0x9baa378,0x0,0x0},
            {'disp_cc_mdss_ahb1_clk',0x9bac028,0x9baa378,0x0,0x0},
            {'disp_cc_mdss_ahb_clk',0x9baa0c4,0x9baa378,0x0,0x0},
            {'disp_cc_mdss_ahb_swi_clk',0x9baa0c0,0x9baa378,0x0,0x0},
            {'disp_cc_mdss_non_gdsc_ahb_clk',0x9bae004,0x9baa378,0x0,0x0},
            {'disp_cc_mdss_rscc_ahb_clk',0x9bae00c,0x9baa378,0x0,0x0},
            {'disp_cc_mdss_byte0_clk',0x9baa044,0x9baa194,0x0,0x0},
            {'disp_cc_mdss_byte0_intf_clk',0x9baa048,0x9baa194,0x0,0x0},
            {'disp_cc_mdss_byte1_clk',0x9baa04c,0x9baa1b0,0x0,0x0},
            {'disp_cc_mdss_byte1_intf_clk',0x9baa050,0x9baa1b0,0x0,0x0},
            {'disp_cc_mdss_dptx0_aux_clk',0x9baa074,0x9baa248,0x0,0x0},
            {'disp_cc_mdss_dptx0_crypto_clk',0x9baa068,0x9baa1fc,0x0,0x0},
            {'disp_cc_mdss_dptx0_link_clk',0x9baa05c,0x9baa1fc,0x0,0x0},
            {'disp_cc_mdss_dptx0_link_intf_clk',0x9baa064,0x9baa1fc,0x0,0x0},
            {'disp_cc_mdss_dptx0_usb_router_link_intf_clk',0x9baa060,0x9baa1fc,0x0,0x0},
            {'disp_cc_mdss_spdm_dp_crypto_clk',0x9bb0014,0x9baa1fc,0x0,0x0},
            {'disp_cc_mdss_dptx0_pixel0_clk',0x9baa06c,0x9baa218,0x0,0x0},
            {'disp_cc_mdss_spdm_dp_pixel_clk',0x9bb0018,0x9baa218,0x0,0x0},
            {'disp_cc_mdss_dptx0_pixel1_clk',0x9baa070,0x9baa230,0x0,0x0},
            {'disp_cc_mdss_spdm_dp_pixel1_clk',0x9bb001c,0x9baa230,0x0,0x0},
            {'disp_cc_mdss_dptx1_aux_clk',0x9baa090,0x9baa2ac,0x0,0x0},
            {'disp_cc_mdss_dptx1_crypto_clk',0x9baa08c,0x9baa290,0x0,0x0},
            {'disp_cc_mdss_dptx1_link_clk',0x9baa080,0x9baa290,0x0,0x0},
            {'disp_cc_mdss_dptx1_link_intf_clk',0x9baa088,0x9baa290,0x0,0x0},
            {'disp_cc_mdss_dptx1_usb_router_link_intf_clk',0x9baa084,0x9baa290,0x0,0x0},
            {'disp_cc_mdss_dptx1_pixel0_clk',0x9baa078,0x9baa260,0x0,0x0},
            {'disp_cc_mdss_dptx1_pixel1_clk',0x9baa07c,0x9baa278,0x0,0x0},
            {'disp_cc_mdss_dptx2_aux_clk',0x9baa0a8,0x9baa310,0x0,0x0},
            {'disp_cc_mdss_dptx2_crypto_clk',0x9baa0a4,0x9baa2c4,0x0,0x0},
            {'disp_cc_mdss_dptx2_link_clk',0x9baa09c,0x9baa2c4,0x0,0x0},
            {'disp_cc_mdss_dptx2_link_intf_clk',0x9baa0a0,0x9baa2c4,0x0,0x0},
            {'disp_cc_mdss_dptx2_pixel0_clk',0x9baa094,0x9baa2e0,0x0,0x0},
            {'disp_cc_mdss_dptx2_pixel1_clk',0x9baa098,0x9baa2f8,0x0,0x0},
            {'disp_cc_mdss_dptx3_aux_clk',0x9baa0b8,0x9baa35c,0x0,0x0},
            {'disp_cc_mdss_dptx3_crypto_clk',0x9baa0bc,0x9baa340,0x0,0x0},
            {'disp_cc_mdss_dptx3_link_clk',0x9baa0b0,0x9baa340,0x0,0x0},
            {'disp_cc_mdss_dptx3_link_intf_clk',0x9baa0b4,0x9baa340,0x0,0x0},
            {'disp_cc_mdss_dptx3_pixel0_clk',0x9baa0ac,0x9baa328,0x0,0x0},
            {'disp_cc_mdss_esc0_clk',0x9baa054,0x9baa1cc,0x0,0x0},
            {'disp_cc_mdss_esc1_clk',0x9baa058,0x9baa1e4,0x0,0x0},
            {'disp_cc_mdss_mdp1_clk',0x9bac004,0x9baa164,0x0,0x0},
            {'disp_cc_mdss_mdp_clk',0x9baa010,0x9baa164,0x0,0x0},
            {'disp_cc_mdss_mdp_lut1_clk',0x9bac014,0x9baa164,0x0,0x0},
            {'disp_cc_mdss_mdp_lut_clk',0x9baa020,0x9baa164,0x0,0x0},
            {'disp_cc_mdss_mdp_ss_ip_clk',0x9baa030,0x9baa164,0x0,0x0},
            {'disp_cc_mdss_spdm_mdp_clk',0x9bb0010,0x9baa164,0x0,0x0},
            {'disp_cc_mdss_pclk0_clk',0x9baa004,0x9baa11c,0x0,0x0},
            {'disp_cc_mdss_spdm_pclk0_clk',0x9bb0004,0x9baa11c,0x0,0x0},
            {'disp_cc_mdss_pclk1_clk',0x9baa008,0x9baa134,0x0,0x0},
            {'disp_cc_mdss_spdm_pclk1_clk',0x9bb0008,0x9baa134,0x0,0x0},
            {'disp_cc_mdss_pclk2_clk',0x9baa00c,0x9baa14c,0x0,0x0},
            {'disp_cc_mdss_spdm_pclk2_clk',0x9bb000c,0x9baa14c,0x0,0x0},
            {'disp_cc_mdss_rscc_vsync_clk',0x9bae008,0x9baa17c,0x0,0x0},
            {'disp_cc_mdss_vsync1_clk',0x9bac024,0x9baa17c,0x0,0x0},
            {'disp_cc_mdss_vsync_clk',0x9baa040,0x9baa17c,0x0,0x0},
            {'disp_cc_osc_clk',0x9baa0c8,0x9baa104,0x0,0x0},
            {'disp_cc_pll_test_clk',0x9baf010,0x9bb3004,0x0,0x0},
            {'disp_cc_dcp_tmr_clk',0x9bb4008,0x9bb0044,0x0,0x0},
            {'disp_cc_mdss_accu_shift_clk',0x9bb0060,0x9bb0044,0x0,0x0},
            {'disp_cc_xo_clk',0x9bb005c,0x9bb0044,0x0,0x0},
            {'gcc_qupv3_wrap_1_m_axi_clk',0x113140,0x17206c,0x14a008,0x100000},
            {'gcc_qupv3_wrap_2_m_ahb_clk',0x113298,0x17206c,0x14a010,0x4},
            {'gcc_qupv3_wrap_3_m_ahb_clk',0x1133f0,0x17206c,0x14a018,0x100},
            {'gcc_qupv3_wrap_4_m_ahb_clk',0x113548,0x17206c,0x14a018,0x400000},
            {'gcc_boot_rom_ahb_clk',0x128004,0x164400,0x14a010,0x40000},
            {'gcc_disp_ahb_clk',0x117004,0x164400,0x0,0x0},
            {'gcc_qupv3_i2c_s_ahb_clk',0x113000,0x164400,0x14a008,0x80},
            {'gcc_qupv3_wrap_1_s_ahb_clk',0x113144,0x164400,0x14a008,0x200000},
            {'gcc_qupv3_wrap_2_s_ahb_clk',0x11329c,0x164400,0x14a010,0x2},
            {'gcc_qupv3_wrap_3_ibi_1_ahb_clk',0x19876c,0x164400,0x14a018,0x40000},
            {'gcc_qupv3_wrap_3_ibi_2_ahb_clk',0x198770,0x164400,0x14a018,0x80000},
            {'gcc_qupv3_wrap_3_s_ahb_clk',0x1133f4,0x164400,0x14a018,0x200},
            {'gcc_qupv3_wrap_4_s_ahb_clk',0x11354c,0x164400,0x14a018,0x800000},
            {'gcc_disp_hf_axi_clk',0x11701c,0x10c0c8,0x0,0x0},
            {'gcc_disp_sf_axi_clk',0x117008,0x10c1f8,0x0,0x0},
            {'gcc_qupv3_i2c_core_clk',0x113004,0x113014,0x14a008,0x100},
            {'gcc_qupv3_i2c_s0_clk',0x107004,0x107008,0x14a008,0x400},
            {'gcc_qupv3_i2c_s1_clk',0x107020,0x107024,0x14a008,0x800},
            {'gcc_qupv3_i2c_s2_clk',0x10703c,0x107040,0x14a008,0x1000},
            {'gcc_qupv3_i2c_s3_clk',0x107058,0x10705c,0x14a008,0x2000},
            {'gcc_qupv3_i2c_s4_clk',0x107074,0x107078,0x14a008,0x4000},
            {'gcc_qupv3_wrap1_core_2x_clk',0x11315c,0x11316c,0x14a008,0x40000},
            {'gcc_qupv3_wrap1_core_clk',0x113148,0x11316c,0x14a008,0x80000},
            {'gcc_qupv3_wrap1_qspi_ref_clk',0x1088bc,0x1088c0,0x14a010,0x20000000},
            {'gcc_qupv3_wrap1_s2_clk',0x10827c,0x1088c0,0x14a008,0x1000000},
            {'gcc_qupv3_wrap1_s0_clk',0x108004,0x108014,0x14a008,0x400000},
            {'gcc_qupv3_wrap1_s1_clk',0x108140,0x108150,0x14a008,0x800000},
            {'gcc_qupv3_wrap1_s3_clk',0x108290,0x1082a0,0x14a008,0x2000000},
            {'gcc_qupv3_wrap1_s4_clk',0x1083cc,0x1083dc,0x14a008,0x4000000},
            {'gcc_qupv3_wrap1_s5_clk',0x108508,0x108518,0x14a008,0x8000000},
            {'gcc_qupv3_wrap1_s6_clk',0x108644,0x108654,0x14a008,0x10000000},
            {'gcc_qupv3_wrap1_s7_clk',0x108780,0x108790,0x14a010,0x10000},
            {'gcc_qupv3_wrap2_core_2x_clk',0x1132b4,0x1132c4,0x14a010,0x8},
            {'gcc_qupv3_wrap2_core_clk',0x1132a0,0x1132c4,0x14a010,0x1},
            {'gcc_qupv3_wrap2_s0_clk',0x10e004,0x10e014,0x14a010,0x10},
            {'gcc_qupv3_wrap2_s1_clk',0x10e140,0x10e150,0x14a010,0x20},
            {'gcc_qupv3_wrap2_s2_clk',0x10e27c,0x10e28c,0x14a010,0x40},
            {'gcc_qupv3_wrap2_s3_clk',0x10e3b8,0x10e3c8,0x14a010,0x80},
            {'gcc_qupv3_wrap2_s4_clk',0x10e4f4,0x10e504,0x14a010,0x100},
            {'gcc_qupv3_wrap3_core_2x_clk',0x11340c,0x11341c,0x14a018,0x800},
            {'gcc_qupv3_wrap3_core_clk',0x1133f8,0x11341c,0x14a018,0x400},
            {'gcc_qupv3_wrap3_ibi_ctrl_1_clk',0x198774,0x19877c,0x14a018,0x100000},
            {'gcc_qupv3_wrap3_ibi_ctrl_2_clk',0x198778,0x19877c,0x14a018,0x200000},
            {'gcc_qupv3_wrap3_s0_clk',0x198004,0x198014,0x14a018,0x1000},
            {'gcc_qupv3_wrap3_s1_clk',0x198140,0x198150,0x14a018,0x2000},
            {'gcc_qupv3_wrap3_s2_clk',0x19827c,0x19828c,0x14a018,0x4000},
            {'gcc_qupv3_wrap3_s3_clk',0x1983b8,0x1983c8,0x14a018,0x8000},
            {'gcc_qupv3_wrap3_s4_clk',0x1984f4,0x198504,0x14a018,0x10000},
            {'gcc_qupv3_wrap3_s5_clk',0x198630,0x198640,0x14a018,0x20000},
            {'gcc_qupv3_wrap4_core_2x_clk',0x113564,0x113574,0x14a018,0x2000000},
            {'gcc_qupv3_wrap4_core_clk',0x113550,0x113574,0x14a018,0x1000000},
            {'gcc_qupv3_wrap4_s0_clk',0x199004,0x199014,0x14a018,0x4000000},
            {'gcc_qupv3_wrap4_s1_clk',0x199140,0x199150,0x14a018,0x8000000},
            {'gcc_qupv3_wrap4_s2_clk',0x19927c,0x19928c,0x14a018,0x10000000},
            {'gcc_qupv3_wrap4_s3_clk',0x1993b8,0x1993c8,0x14a018,0x20000000},
            {'gcc_qupv3_wrap4_s4_clk',0x1994f4,0x199504,0x14a018,0x40000000},
        }
        local masks={0,0,0}
        for _,b in ipairs(branches) do
            if b[2]<0x200000 then
                b[2]=b[2]+0x10000
                if b[3]~=0 then b[3]=b[3]+0x10000 end
            end
            if b[4]~=0 then local i=(b[4]-0x14a008)//8+1 masks[i]=masks[i]|b[5] end
        end
        map('dcp_clock_votes',0x15a008,0x14,{moduletype='qcom,clock-vote-bank-v1',
            mask0=masks[1],mask1=masks[2],mask2=masks[3]})
        for _,b in ipairs(branches) do
            if not reserved(b[2]) then
                local _,module=owner(b[2],4)
                if not module then
                    local source=reference
                    if b[3]~=0 then source=assert(owner(b[3],4)) end
                    _,module=map('dcp_branch_'..b[2],b[2],4,
                        {moduletype='qcom,clock-branch-v1',source_path=source,hardware_control=true})
                end
                if module.moduletype=='qcom,retained-boot-control-v1' then
                    assert((module.reset_value or 0)==0, 'A branch policy must start disabled')
                    module.moduletype='qcom,clock-branch-v1'
                    module.retained_mask=module.writable_mask & ~3
                    module.writable_mask=nil
                    module.reset_value=nil
                    module.hardware_control=true
                    module.source_path=b[3]~=0 and assert(owner(b[3],4)) or reference
                end
                assert(module.moduletype=='qcom,clock-branch-v1', 'Incompatible DCP branch owner: '..b[1]..' '..module.moduletype)
                if b[4]~=0 then
                    append_vote(module,'dcp_clock_votes',
                        b[4]-0x14a008,b[5],b[2])
                end
            end
        end

    elseif soc=='sm8845' then
        local roots={
            {0x9baa0d4,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423dce0
            {0x9baa0ec,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423dd20
            {0x9baa378,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=4,primary_even_selector=6,primary_index=1,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423dd60
            {0x9baa194,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423dda0
            {0x9baa1b0,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423dde0
            {0x9baa248,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423de20
            {0x9baa1fc,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423de60
            {0x9baa218,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423dea0
            {0x9baa230,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423dee0
            {0x9baa2ac,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423df20
            {0x9baa290,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423df60
            {0x9baa260,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423dfa0
            {0x9baa278,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423dfe0
            {0x9baa310,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423e020
            {0x9baa2c4,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423e060
            {0x9baa2e0,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423e0a0
            {0x9baa2f8,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423e0e0
            {0x9baa35c,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423e120
            {0x9baa340,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423e160
            {0x9baa328,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423e1a0
            {0x9baa1cc,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423e1e0
            {0x9baa1e4,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423e220
            {0x9baa164,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=1,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423e260
            {0x9baa11c,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423e2a0
            {0x9baa134,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423e2e0
            {0x9baa14c,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423e320
            {0x9baa17c,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423e360
            {0x9baa104,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_pongo',crystal_selector=8,primary_selector=2,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423e3a0
            {0x9bb3004,0x4,{moduletype='qcom,clock-integer-divider-v1',pll_path='dcp_display_plls',reference_source=true,field_shift=16,field_width=9}}, -- descriptor 0xa423e3e0
            {0x9bb0044,0x14,{moduletype='qcom,rcg-v1',pll_path='dcp_display_plls',crystal_selector=0,primary_selector=8,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423e420
            {0x17206c,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423f72c
            {0x164400,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423f76c
            {0x10c0c8,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423f7ac
            {0x10c1f8,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423f7ec
            {0x113014,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423f82c
            {0x107008,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423f86c
            {0x107024,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423f8ac
            {0x107040,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423f8ec
            {0x10705c,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423f92c
            {0x107078,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423f96c
            {0x11316c,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423f9ac
            {0x1088c0,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423f9ec
            {0x108014,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423fa2c
            {0x108150,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423fa6c
            {0x1082a0,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423faac
            {0x1083dc,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423faec
            {0x108518,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423fb2c
            {0x108654,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423fb6c
            {0x108790,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423fbac
            {0x1132c4,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423fbec
            {0x10e014,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423fc2c
            {0x10e150,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423fc6c
            {0x10e28c,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423fcac
            {0x10e3c8,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423fcec
            {0x10e504,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423fd2c
            {0x10e640,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423fd6c
            {0x10e77c,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423fdac
            {0x10e8b8,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423fdec
            {0x11341c,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423fe2c
            {0x19877c,0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=8}}, -- descriptor 0xa423fe6c
            {0x198014,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423feac
            {0x198150,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423feec
            {0x19828c,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423ff2c
            {0x1983c8,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423ff6c
            {0x198504,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423ffac
            {0x198640,0xfc,{moduletype='qcom,rcg-dfs-v1',pll_path='gcc_plls',crystal_selector=0,primary_selector=1,primary_even_selector=6,primary_index=0,require_root_enable=false,mnd_width=16}}, -- descriptor 0xa423ffec
        }
        for _,r in ipairs(roots) do
            if r[1]<0x200000 then r[1]=r[1]+0x10000 end
            if not owner(r[1],r[2]) then map('dcp_root_'..r[1],r[1],r[2],r[3]) end
        end
        local branches={
            {'disp_cc_dcp_sleep_clk',0x9bb400c,0x0,0x0,0x0},
            {'disp_cc_debug_clk',0x9baf004,0x0,0x0,0x0},
            {'disp_cc_mdss_spdm_debug_clk',0x9bb0020,0x0,0x0,0x0},
            {'disp_cc_sleep_clk',0x9bb0064,0x0,0x0,0x0},
            {'disp_cc_dcp_dbg_clk',0x9bb4028,0x9bb4010,0x0,0x0},
            {'disp_cc_dcp_f_clk',0x9bb402c,0x9bb4010,0x0,0x0},
            {'disp_cc_dcp_proc_h_clk',0x9bb4030,0x9bb4010,0x0,0x0},
            {'disp_cc_dcp_ss_h_clk',0x9bb4040,0x9bb4010,0x0,0x0},
            {'disp_cc_dcp_dbg_apb_clk',0x9bb405c,0x9bb4044,0x0,0x0},
            {'disp_cc_dcp_dbg_atb_clk',0x9bb4094,0x9bb407c,0x0,0x0},
            {'disp_cc_dcp_dbg_qts_clk',0x9bb4078,0x9bb4060,0x0,0x0},
            {'disp_cc_esync0_clk',0x9baa0cc,0x9baa0d4,0x0,0x0},
            {'disp_cc_esync1_clk',0x9baa0d0,0x9baa0ec,0x0,0x0},
            {'disp_cc_dcp_ahb_clk',0x9bb4004,0x9baa378,0x0,0x0},
            {'disp_cc_mdss_ahb1_clk',0x9bac028,0x9baa378,0x0,0x0},
            {'disp_cc_mdss_ahb_clk',0x9baa0c4,0x9baa378,0x0,0x0},
            {'disp_cc_mdss_ahb_swi_clk',0x9baa0c0,0x9baa378,0x0,0x0},
            {'disp_cc_mdss_non_gdsc_ahb_clk',0x9bae004,0x9baa378,0x0,0x0},
            {'disp_cc_mdss_rscc_ahb_clk',0x9bae00c,0x9baa378,0x0,0x0},
            {'disp_cc_mdss_byte0_clk',0x9baa044,0x9baa194,0x0,0x0},
            {'disp_cc_mdss_byte0_intf_clk',0x9baa048,0x9baa194,0x0,0x0},
            {'disp_cc_mdss_byte1_clk',0x9baa04c,0x9baa1b0,0x0,0x0},
            {'disp_cc_mdss_byte1_intf_clk',0x9baa050,0x9baa1b0,0x0,0x0},
            {'disp_cc_mdss_dptx0_aux_clk',0x9baa074,0x9baa248,0x0,0x0},
            {'disp_cc_mdss_dptx0_crypto_clk',0x9baa068,0x9baa1fc,0x0,0x0},
            {'disp_cc_mdss_dptx0_link_clk',0x9baa05c,0x9baa1fc,0x0,0x0},
            {'disp_cc_mdss_dptx0_link_intf_clk',0x9baa064,0x9baa1fc,0x0,0x0},
            {'disp_cc_mdss_dptx0_usb_router_link_intf_clk',0x9baa060,0x9baa1fc,0x0,0x0},
            {'disp_cc_mdss_spdm_dp_crypto_clk',0x9bb0014,0x9baa1fc,0x0,0x0},
            {'disp_cc_mdss_dptx0_pixel0_clk',0x9baa06c,0x9baa218,0x0,0x0},
            {'disp_cc_mdss_spdm_dp_pixel_clk',0x9bb0018,0x9baa218,0x0,0x0},
            {'disp_cc_mdss_dptx0_pixel1_clk',0x9baa070,0x9baa230,0x0,0x0},
            {'disp_cc_mdss_spdm_dp_pixel1_clk',0x9bb001c,0x9baa230,0x0,0x0},
            {'disp_cc_mdss_dptx1_aux_clk',0x9baa090,0x9baa2ac,0x0,0x0},
            {'disp_cc_mdss_dptx1_crypto_clk',0x9baa08c,0x9baa290,0x0,0x0},
            {'disp_cc_mdss_dptx1_link_clk',0x9baa080,0x9baa290,0x0,0x0},
            {'disp_cc_mdss_dptx1_link_intf_clk',0x9baa088,0x9baa290,0x0,0x0},
            {'disp_cc_mdss_dptx1_usb_router_link_intf_clk',0x9baa084,0x9baa290,0x0,0x0},
            {'disp_cc_mdss_dptx1_pixel0_clk',0x9baa078,0x9baa260,0x0,0x0},
            {'disp_cc_mdss_dptx1_pixel1_clk',0x9baa07c,0x9baa278,0x0,0x0},
            {'disp_cc_mdss_dptx2_aux_clk',0x9baa0a8,0x9baa310,0x0,0x0},
            {'disp_cc_mdss_dptx2_crypto_clk',0x9baa0a4,0x9baa2c4,0x0,0x0},
            {'disp_cc_mdss_dptx2_link_clk',0x9baa09c,0x9baa2c4,0x0,0x0},
            {'disp_cc_mdss_dptx2_link_intf_clk',0x9baa0a0,0x9baa2c4,0x0,0x0},
            {'disp_cc_mdss_dptx2_pixel0_clk',0x9baa094,0x9baa2e0,0x0,0x0},
            {'disp_cc_mdss_dptx2_pixel1_clk',0x9baa098,0x9baa2f8,0x0,0x0},
            {'disp_cc_mdss_dptx3_aux_clk',0x9baa0b8,0x9baa35c,0x0,0x0},
            {'disp_cc_mdss_dptx3_crypto_clk',0x9baa0bc,0x9baa340,0x0,0x0},
            {'disp_cc_mdss_dptx3_link_clk',0x9baa0b0,0x9baa340,0x0,0x0},
            {'disp_cc_mdss_dptx3_link_intf_clk',0x9baa0b4,0x9baa340,0x0,0x0},
            {'disp_cc_mdss_dptx3_pixel0_clk',0x9baa0ac,0x9baa328,0x0,0x0},
            {'disp_cc_mdss_esc0_clk',0x9baa054,0x9baa1cc,0x0,0x0},
            {'disp_cc_mdss_esc1_clk',0x9baa058,0x9baa1e4,0x0,0x0},
            {'disp_cc_mdss_mdp1_clk',0x9bac004,0x9baa164,0x0,0x0},
            {'disp_cc_mdss_mdp_clk',0x9baa010,0x9baa164,0x0,0x0},
            {'disp_cc_mdss_mdp_lut1_clk',0x9bac014,0x9baa164,0x0,0x0},
            {'disp_cc_mdss_mdp_lut_clk',0x9baa020,0x9baa164,0x0,0x0},
            {'disp_cc_mdss_mdp_ss_ip_clk',0x9baa030,0x9baa164,0x0,0x0},
            {'disp_cc_mdss_spdm_mdp_clk',0x9bb0010,0x9baa164,0x0,0x0},
            {'disp_cc_mdss_pclk0_clk',0x9baa004,0x9baa11c,0x0,0x0},
            {'disp_cc_mdss_spdm_pclk0_clk',0x9bb0004,0x9baa11c,0x0,0x0},
            {'disp_cc_mdss_pclk1_clk',0x9baa008,0x9baa134,0x0,0x0},
            {'disp_cc_mdss_spdm_pclk1_clk',0x9bb0008,0x9baa134,0x0,0x0},
            {'disp_cc_mdss_pclk2_clk',0x9baa00c,0x9baa14c,0x0,0x0},
            {'disp_cc_mdss_spdm_pclk2_clk',0x9bb000c,0x9baa14c,0x0,0x0},
            {'disp_cc_mdss_rscc_vsync_clk',0x9bae008,0x9baa17c,0x0,0x0},
            {'disp_cc_mdss_vsync1_clk',0x9bac024,0x9baa17c,0x0,0x0},
            {'disp_cc_mdss_vsync_clk',0x9baa040,0x9baa17c,0x0,0x0},
            {'disp_cc_osc_clk',0x9baa0c8,0x9baa104,0x0,0x0},
            {'disp_cc_pll_test_clk',0x9baf010,0x9bb3004,0x0,0x0},
            {'disp_cc_dcp_tmr_clk',0x9bb4008,0x9bb0044,0x0,0x0},
            {'disp_cc_mdss_accu_shift_clk',0x9bb0060,0x9bb0044,0x0,0x0},
            {'disp_cc_xo_clk',0x9bb005c,0x9bb0044,0x0,0x0},
            {'gcc_qupv3_wrap_1_m_axi_clk',0x113140,0x17206c,0x14a008,0x100000},
            {'gcc_qupv3_wrap_2_m_ahb_clk',0x113298,0x17206c,0x14a010,0x4},
            {'gcc_qupv3_wrap_3_m_ahb_clk',0x1133f0,0x17206c,0x14a018,0x100},
            {'gcc_boot_rom_ahb_clk',0x128004,0x164400,0x14a010,0x40000},
            {'gcc_disp_ahb_clk',0x117004,0x164400,0x0,0x0},
            {'gcc_qupv3_i2c_s_ahb_clk',0x113000,0x164400,0x14a008,0x80},
            {'gcc_qupv3_wrap_1_s_ahb_clk',0x113144,0x164400,0x14a008,0x200000},
            {'gcc_qupv3_wrap_2_s_ahb_clk',0x11329c,0x164400,0x14a010,0x2},
            {'gcc_qupv3_wrap_3_ibi_1_ahb_clk',0x19876c,0x164400,0x14a018,0x40000},
            {'gcc_qupv3_wrap_3_ibi_2_ahb_clk',0x198770,0x164400,0x14a018,0x80000},
            {'gcc_qupv3_wrap_3_s_ahb_clk',0x1133f4,0x164400,0x14a018,0x200},
            {'gcc_disp_hf_axi_clk',0x11701c,0x10c0c8,0x0,0x0},
            {'gcc_disp_sf_axi_clk',0x117008,0x10c1f8,0x0,0x0},
            {'gcc_qupv3_i2c_core_clk',0x113004,0x113014,0x14a008,0x100},
            {'gcc_qupv3_i2c_s0_clk',0x107004,0x107008,0x14a008,0x400},
            {'gcc_qupv3_i2c_s1_clk',0x107020,0x107024,0x14a008,0x800},
            {'gcc_qupv3_i2c_s2_clk',0x10703c,0x107040,0x14a008,0x1000},
            {'gcc_qupv3_i2c_s3_clk',0x107058,0x10705c,0x14a008,0x2000},
            {'gcc_qupv3_i2c_s4_clk',0x107074,0x107078,0x14a008,0x4000},
            {'gcc_qupv3_wrap1_core_2x_clk',0x11315c,0x11316c,0x14a008,0x40000},
            {'gcc_qupv3_wrap1_core_clk',0x113148,0x11316c,0x14a008,0x80000},
            {'gcc_qupv3_wrap1_qspi_ref_clk',0x1088bc,0x1088c0,0x14a010,0x20000000},
            {'gcc_qupv3_wrap1_s2_clk',0x10827c,0x1088c0,0x14a008,0x1000000},
            {'gcc_qupv3_wrap1_s0_clk',0x108004,0x108014,0x14a008,0x400000},
            {'gcc_qupv3_wrap1_s1_clk',0x108140,0x108150,0x14a008,0x800000},
            {'gcc_qupv3_wrap1_s3_clk',0x108290,0x1082a0,0x14a008,0x2000000},
            {'gcc_qupv3_wrap1_s4_clk',0x1083cc,0x1083dc,0x14a008,0x4000000},
            {'gcc_qupv3_wrap1_s5_clk',0x108508,0x108518,0x14a008,0x8000000},
            {'gcc_qupv3_wrap1_s6_clk',0x108644,0x108654,0x14a008,0x10000000},
            {'gcc_qupv3_wrap1_s7_clk',0x108780,0x108790,0x14a010,0x10000},
            {'gcc_qupv3_wrap2_core_2x_clk',0x1132b4,0x1132c4,0x14a010,0x8},
            {'gcc_qupv3_wrap2_core_clk',0x1132a0,0x1132c4,0x14a010,0x1},
            {'gcc_qupv3_wrap2_s0_clk',0x10e004,0x10e014,0x14a010,0x10},
            {'gcc_qupv3_wrap2_s1_clk',0x10e140,0x10e150,0x14a010,0x20},
            {'gcc_qupv3_wrap2_s2_clk',0x10e27c,0x10e28c,0x14a010,0x40},
            {'gcc_qupv3_wrap2_s3_clk',0x10e3b8,0x10e3c8,0x14a010,0x80},
            {'gcc_qupv3_wrap2_s4_clk',0x10e4f4,0x10e504,0x14a010,0x100},
            {'gcc_qupv3_wrap2_s5_clk',0x10e630,0x10e640,0x14a010,0x200},
            {'gcc_qupv3_wrap2_s6_clk',0x10e76c,0x10e77c,0x14a010,0x400},
            {'gcc_qupv3_wrap2_s7_clk',0x10e8a8,0x10e8b8,0x14a010,0x800},
            {'gcc_qupv3_wrap3_core_2x_clk',0x11340c,0x11341c,0x14a018,0x800},
            {'gcc_qupv3_wrap3_core_clk',0x1133f8,0x11341c,0x14a018,0x400},
            {'gcc_qupv3_wrap3_ibi_ctrl_1_clk',0x198774,0x19877c,0x14a018,0x100000},
            {'gcc_qupv3_wrap3_ibi_ctrl_2_clk',0x198778,0x19877c,0x14a018,0x200000},
            {'gcc_qupv3_wrap3_s0_clk',0x198004,0x198014,0x14a018,0x1000},
            {'gcc_qupv3_wrap3_s1_clk',0x198140,0x198150,0x14a018,0x2000},
            {'gcc_qupv3_wrap3_s2_clk',0x19827c,0x19828c,0x14a018,0x4000},
            {'gcc_qupv3_wrap3_s3_clk',0x1983b8,0x1983c8,0x14a018,0x8000},
            {'gcc_qupv3_wrap3_s4_clk',0x1984f4,0x198504,0x14a018,0x10000},
            {'gcc_qupv3_wrap3_s5_clk',0x198630,0x198640,0x14a018,0x20000},
        }
        local masks={0,0,0}
        for _,b in ipairs(branches) do
            if b[2]<0x200000 then
                b[2]=b[2]+0x10000
                if b[3]~=0 then b[3]=b[3]+0x10000 end
            end
            if b[4]~=0 then local i=(b[4]-0x14a008)//8+1 masks[i]=masks[i]|b[5] end
        end
        map('dcp_clock_votes',0x15a008,0x14,{moduletype='qcom,clock-vote-bank-v1',
            mask0=masks[1],mask1=masks[2],mask2=masks[3]})
        for _,b in ipairs(branches) do
            if not reserved(b[2]) then
                local _,module=owner(b[2],4)
                if not module then
                    local source=reference
                    if b[3]~=0 then source=assert(owner(b[3],4)) end
                    _,module=map('dcp_branch_'..b[2],b[2],4,
                        {moduletype='qcom,clock-branch-v1',source_path=source,hardware_control=true})
                end
                if module.moduletype=='qcom,retained-boot-control-v1' then
                    assert((module.reset_value or 0)==0, 'A branch policy must start disabled')
                    module.moduletype='qcom,clock-branch-v1'
                    module.retained_mask=module.writable_mask & ~3
                    module.writable_mask=nil
                    module.reset_value=nil
                    module.hardware_control=true
                    module.source_path=b[3]~=0 and assert(owner(b[3],4)) or reference
                end
                assert(module.moduletype=='qcom,clock-branch-v1', 'Incompatible DCP branch owner: '..b[1]..' '..module.moduletype)
                if b[4]~=0 then
                    append_vote(module,'dcp_clock_votes',
                        b[4]-0x14a008,b[5],b[2])
                end
            end
        end
    end
    for _,offset in ipairs({0,0x2000,0x6000}) do
        local address=0x09baa000+offset
        if not owner(address,4) then map('dcp_display_reset_'..offset,address,4,
            {moduletype='qcom,retained-boot-control-v1',writable_mask=1}) end
    end
    -- Branch descriptors identify these BCR words. Bit 0 retains the reset request.
    local gcc_resets={0x7000,0x8000,0xe000,0x17000,0x98000}
    if soc=='sm8850' then gcc_resets[#gcc_resets+1]=0x99000 end
    for _,offset in ipairs(gcc_resets) do
        local address=0x110000+offset
        if not owner(address,4) then map('dcp_gcc_reset_'..offset,address,4,
            {moduletype='qcom,retained-boot-control-v1',writable_mask=1}) end
    end
    map('dcp_display_int2_gdsc',0x09bad000,4,{moduletype='qcom,gdsc-v1',virtual_supply=true})
    for _,offset in ipairs({0x1ac,0x1c8}) do
        map('dcp_display_divider_'..offset,0x09baa000+offset,4,
            {moduletype='qcom,clock-integer-divider-v1',pll_path='dcp_display_plls'})
    end
    -- The bootstrap owns the DCP core pages. These roots and PLLs feed their branches.
    for param,address in pairs({ahb_root_path=0x09baa378,vsync_root_path=0x09baa17c,
            timer_root_path=0x09bb0044}) do
        local name=owner(address,4)
        assert(name, string.format('The DCP clock parent at 0x%x is missing',address))
        p.dcp_bootstrap[param]=name
    end
    p.dcp_bootstrap.display_pll_path='dcp_display_plls'
    return p
end
