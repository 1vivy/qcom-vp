-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- UEFI ClockDxe clock records that RscDxe enables for the camera and PCIe RSCs.
-- Records, offsets, and the access path: models/qcom/sm88xx/cpucp/clock_roots_evidence.md,
-- sections "UEFI camera RSC clocks" and "UEFI PCIe RSCC clocks".
local directory=assert(debug.getinfo(1,'S').source:match('^@(.*/)'))
local append_vote=dofile(directory..'clock-branch-votes.lua')
return function(p, oem)
    assert(oem=='sm8850', 'Only the SM8850 ClockDxe records are decoded.')
    assert(p.cpucp_clock_router and p.apss_router and p.cpucp_router and p.gcc_plls,
        'The reset platform clock routers and the GCC PLL bank are required.')
    local routers={'cpucp_clock_router','apss_router','cpucp_router'}
    local function overlap(router,address,size)
        for name,module in pairs(p) do
            if type(module)=='table' then
                for _,socket in pairs(module) do
                    if type(socket)=='table' and socket.bind=='&'..router..'.initiator_socket'
                        and type(socket.address)=='number' and type(socket.size)=='number'
                        and address<socket.address+socket.size and socket.address<address+size then
                        return name
                    end
                end
            end
        end
    end
    local function map(name,address,size,module)
        for _,router in ipairs(routers) do
            local collision=overlap(router,address,size)
            if collision then
                error(string.format('UEFI clock target 0x%x overlaps %s in %s',address,collision,router))
            end
        end
        assert(p[name]==nil, 'Duplicate UEFI clock module: '..name)
        module.target_socket={address=address,size=size,bind='&cpucp_clock_router.initiator_socket'}
        p[name]=module
        for _,router in ipairs({'apss_router','cpucp_router'}) do
            p[name..'_'..router]={moduletype='addrtr',mapped_base_addr=address,
                target_socket={address=address,size=size,relative_addresses=false,
                    bind='&'..router..'.initiator_socket'},
                initiator_socket={bind='&cpucp_clock_router.target_socket'}}
        end
    end
    -- CAM_CC_CAM_CC_REG is camcc 0x0956d000 plus 0x20000.
    -- Domain records 0x28c20 (CMD_RCGR +0x100) and 0x28de0 (CMD_RCGR +0x134c).
    -- Reset CFG_RCGR selects XO, the ClockDxe safe configuration. No camcc PLL is required.
    for _,root in ipairs({{'uefi_cam_cc_ahb_root',0x0958d100},{'uefi_cam_cc_xo_root',0x0958e34c}}) do
        map(root[1],root[2],0x14,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',
            crystal_selector=0,require_root_enable=false,mnd_width=8})
    end
    -- Clock records 0x270d0 (cam_cc_drv_ahb_clk) and 0x27990 (cam_cc_drv_xo_clk).
    -- QSEE already owns cam_cc_core_ahb_clk at 0x0958e348 and the TITAN_TOP GDSC at 0x0958e334.
    map('uefi_cam_cc_drv_ahb_clk',0x0958e398,4,
        {moduletype='qcom,clock-branch-v1',source_path='uefi_cam_cc_ahb_root'})
    map('uefi_cam_cc_drv_xo_clk',0x0958e390,4,
        {moduletype='qcom,clock-branch-v1',source_path='uefi_cam_cc_xo_root'})
    -- HWIODxe places CAM_CC_PLL0..7 at camcc 0x0956d000 with a 0x1000 stride.
    -- Clock_InitCESTA enables and disables the CESTA_CONTROLLED sources once.
    -- Source records 0x29948, 0x29a08, and 0x29ac8 (PLL3..5) carry that flag.
    map('uefi_cam_cc_plls',0x0956d000,0x7038,{moduletype='qcom,gcc-taycan-eko-t-pll-v1',pll_count=8})
    -- ClockDxe CESTA record 0x3b728 (cam_cesta, /soc/cesta@9549e00) has the default-enable flag.
    -- HWIODxe places its five banks at TITAN 0x09000000 plus the listed offsets.
    -- The lists name every register that Clock_InitCESTAInstance writes.
    local cesta={moduletype='qcom,cesta-v1',
        crmb_registers='0x0 0x30:10 0x5c:2',
        crmc_registers='0x60 0x78:2 0x84:3 0x9c:2 0x150 0x158 0x160:5 0x178:5 0x190:5 0x1a8:5 '..
            '0x1c0:5 0x1d8:2 0x270 0x288:2 0x294:3 0x2ac:2 0x360 0x368 0x370:5 0x388:5 0x3a0:5 '..
            '0x3b8:5 0x3d0:5 0x3e8:2 0x480 0x498:2 0x4a4:3 0x4bc:2 0x570 0x578 0x580:5 0x598:5 '..
            '0x5b0:5 0x5c8:5 0x5e0:5 0x5f8:2 0x6e4:2 0x8f4:2 0xb04:2 0xcc0 0xce4:3 0xd88 0xdb0 '..
            '0xdb8 0xdc0:3 0xdd0 0xdd8:3 0xde8 0xdf0:3 0xe00 0xe08:2 0xed0 0xef4:3 0xfc0 0xfc8:5 '..
            '0xfe0:5 0xff8:5 0x1010:4 0x10e0 0x1104:3 0x11d0 0x11d8 0x11e0:3 0x11f0 0x11f8:3 '..
            '0x1208 0x1210:3 0x1220 0x1228:3 0x1238 0x1240:3 0x1250 0x1258:2 0x1300:22 0x1380:93',
        crmv_registers='0x0 0x38 0x40 0x78 0x80 0xb8 0xc0 0xf8',
        common_registers='0xc'}
    for _,bank in ipairs({
        {'crmb_socket',0x09549e00,0x400},{'crmb_pt_socket',0x0954a200,0x400},
        {'crmc_socket',0x0954a600,0x2000},{'crmv_socket',0x0954c600,0x600},
        {'common_socket',0x0954cc00,0x200},
    }) do
        local collision=overlap('apss_router',bank[2],bank[3])
        if collision then error(string.format('UEFI CESTA bank 0x%x overlaps %s',bank[2],collision)) end
        cesta[bank[1]]={address=bank[2],size=bank[3],bind='&apss_router.initiator_socket'}
    end
    assert(p.uefi_cam_cesta==nil, 'Duplicate UEFI camera CESTA')
    p.uefi_cam_cesta=cesta
    -- RscDxe reads the TITAN camera RSCC at 0x09545000 and uploads the camera
    -- sequence there. Binary RVAs: models/qcom/rpmh/evidence.md.
    -- The block size 0x3000 is HWIO_TITAN_RSCC_RSCC_RSC_REG_BASE_SIZE.
    -- The block ends before the CESTA banks at 0x09549e00.
    local rscc_address,rscc_size=0x09545000,0x3000
    for _,router in ipairs(routers) do
        local collision=overlap(router,rscc_address,rscc_size)
        if collision then
            error(string.format('UEFI camera RSCC overlaps %s in %s',collision,router))
        end
    end
    assert(p.uefi_cam_rscc==nil, 'Duplicate UEFI camera RSCC')
    p.uefi_cam_rscc={moduletype='qcom,titan-rscc-v1',
        target_socket={address=rscc_address,size=rscc_size,
            bind='&apss_router.initiator_socket'}}
    -- RscDxe then initializes the PCIe instance. The instance record at RVA
    -- 0x7228 names physical base 0x01d00000 (run742 read 0x01d00004).
    -- The block size 0x1000 is PCIE_RSCC_RSC_REG_BASE_SIZE.
    local pcie_address,pcie_size=0x01d00000,0x1000
    for _,router in ipairs(routers) do
        local collision=overlap(router,pcie_address,pcie_size)
        if collision then
            error(string.format('UEFI PCIe RSCC overlaps %s in %s',collision,router))
        end
    end
    assert(p.uefi_pcie_rscc==nil, 'Duplicate UEFI PCIe RSCC')
    p.uefi_pcie_rscc={moduletype='qcom,pcie-rscc-v1',
        target_socket={address=pcie_address,size=pcie_size,
            bind='&apss_router.initiator_socket'}}
    -- ClockDxe CESTA record 0x3b8c8 (pcie_cesta, /soc/cesta@1d03000) has no
    -- default-enable flag. Run745 wrote its sequence to CRMC +0x8b8 through the
    -- clock resource path at RVA 0x9614, which programs records without the flag.
    -- The device tree node at xbl_config.elf offset 0x10e14 gives the bank bases.
    -- The lists name every register that Clock_InitCESTAInstance writes.
    -- The instance has no clock dependency and no CRMB register.
    local pcie_cesta={moduletype='qcom,cesta-v1',
        crmb_registers='',
        crmb_pt_registers='0x4:2 0x10 0x18:2 0x24 0x2c:2 0x38 0x40:2 0x4c 0x54:2 0x60 0x68:2 0x74',
        crmc_registers='0x18 0x3c:3 0x6c:2 0xc0 0xd0:24 0x150:4 0x170 0x178:5 0x198 0x1a0:5 '..
            '0x1c0 0x1c8:5 0x1e8 0x1f0:5 0x210 0x218:5 0x238 0x240:5 0x31c:2 '..
            '0x838:28 0x8b8:122',
        crmv_registers='0x0 0x38 0x40 0x78 0x80 0xb8 0xc0 0xf8 0x100 0x138',
        common_registers='0xc'}
    for _,bank in ipairs({
        {'crmb_socket',0x01d03000,0x400},{'crmb_pt_socket',0x01d03400,0x400},
        {'crmc_socket',0x01d03800,0x2000},{'crmv_socket',0x01d05800,0x600},
        {'common_socket',0x01d05e00,0x200},
    }) do
        for _,router in ipairs(routers) do
            local collision=overlap(router,bank[2],bank[3])
            if collision then
                error(string.format('UEFI PCIe CESTA bank 0x%x overlaps %s in %s',
                    bank[2],collision,router))
            end
        end
        pcie_cesta[bank[1]]={address=bank[2],size=bank[3],bind='&apss_router.initiator_socket'}
    end
    assert(p.uefi_pcie_cesta==nil, 'Duplicate UEFI PCIe CESTA')
    p.uefi_pcie_cesta=pcie_cesta
    -- ClockDxe 0x10a54 votes 0x152010 bit 21 and polls CLK_OFF at 0x111008 (run741).
    -- PCIE_RSCC_CFG_AHB_CBCR is at GCC 0x11004 and PCIE_RSCC_XO_CBCR at
    -- GCC 0x11008. Linux gcc-canoe.c keeps both on through 0x52010 bits 20 and 21.
    local votes=p.xbl_peripheral_clock_votes
    assert(votes and votes.moduletype=='qcom,clock-vote-bank-v1' and votes.target_socket.address==0x152008,
        'The XBL peripheral vote bank at 0x152008 is required.')
    votes.mask1=votes.mask1 | 0x300000
    for _,branch in ipairs({{'uefi_pcie_rscc_cfg_ahb_clk',0x111004,0x100000},
        {'uefi_pcie_rscc_xo_clk',0x111008,0x200000}}) do
        map(branch[1],branch[2],4,{moduletype='qcom,clock-branch-v1',
            source_path='xbl_boot_clock_reference'})
        append_vote(p[branch[1]],'xbl_peripheral_clock_votes',8,branch[3],branch[2])
    end
    -- ClockDxe RVA 0x41ac programs the GPU CC FF root before a GPU power domain:
    -- FF_CFG_RCGR (GPU CC 0x5478) gets SRC_SEL 5 and divider field 5, then
    -- FF_CMD_RCGR (0x5474) gets UPDATE. HWIODxe maps that GPU CC block at 0x03d94000.
    -- Linux gpucc-canoe.c parent map 1 gives selector 5 as GPLL0_OUT_MAIN. The root has no M/N/D.
    map('uefi_gpu_cc_ff_root',0x03d99474,8,{moduletype='qcom,rcg-v1',pll_path='gcc_plls',
        crystal_selector=0,primary_selector=5,primary_index=0,require_root_enable=false})
    -- The same hook then sets CLK_ENABLE in two GX clock-control branches at 0x03d68000:
    -- GX_GFX3D_CBCR (+0x40) and CLK_CTL_XO_CBCR (+0x8). It enables
    -- the existing GX GDSC (+0x24) last. It programs no GX root, so the virtual XO
    -- reference is the source.
    for _,branch in ipairs({{'uefi_gx_gfx3d_clk',0x03d68040},{'uefi_gx_xo_clk',0x03d68008}}) do
        map(branch[1],branch[2],4,{moduletype='qcom,clock-branch-v1',
            source_path='xbl_boot_clock_reference'})
    end
    return p
end
