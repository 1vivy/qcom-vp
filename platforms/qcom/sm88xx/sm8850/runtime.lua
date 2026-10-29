-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- SM8850 reconstructed-runtime composition. Register evidence appears beside each block.
-- Apply this helper once, after reset.lua constructs the reset platform.
-- options.tme names the reconstructed ROM. options.tme_runtime_api_map names its verified ABI map.
-- The caller retains trace, debugger, console, mutable LUN, boot, and authentication policy.
-- Helpers use installed sibling files. No private build tree or Python is required.
local directory = assert(debug.getinfo(1, 'S').source:match('^@(.*/)'))
return function(platform, options)
    dofile(directory..'../tme-runtime.lua')(platform, options,
        dofile(directory..'runtime-data.lua'))
    dofile(directory..'../tme-cold-controls.lua')(platform)
    dofile(directory..'../xbl-calibration-fuses.lua')(platform)
    dofile(directory..'cpr.lua')(platform)
    -- XBL 0x1482f024 pets the KPSS watchdog. Its timeout conversion uses 32768 Hz.
    platform.apss_watchdog={moduletype='qcom,kpss-watchdog-v1',clock_hz=32768,
        security_socket={address=0x17908000,size=4,bind='&apss_router.initiator_socket'},
        target_socket={address=0x17908004,size=0x14,bind='&apss_router.initiator_socket'},
        bark={bind='&apss_gic.spi_in_26'}}
    -- QSEE 0x157bc808 selects nonsecure mode for the Canoe watchdog.
    -- QHEE maps the whole 0x17600000 page and writes control words through +0x24.
    platform.os_watchdog={moduletype='qcom,kpss-watchdog-v1',clock_hz=32768,
        security_socket={address=0x17600000,size=4,bind='&apss_router.initiator_socket'},
        target_socket={address=0x17600004,size=0x24,bind='&apss_router.initiator_socket'},
        bark={bind='&apss_gic.spi_in_0'},bite={bind='&apss_gic.spi_in_1'}}
    -- Retain the QSEE system IMEM after every native stop.
    platform.qsee_imem_snapshot={moduletype='qcom,memory-snapshot-v1',
        address=0x14680000,size=0x2c000,capture_time_ms=25000,
        outfile=options.trace..'-qsee-imem.bin',
        initiator_socket={bind='&apss_router.target_socket'}}
    platform.tme_qmp_snapshot={moduletype='qcom,memory-snapshot-v1',
        address=0x22002000,size=0x1800,outfile=options.trace..'-tme-qmp.bin',
        initiator_socket={bind='&apss_router.target_socket'}}
    -- QSEE configures the PDC bitmap and BCM controls before clock initialization.
    -- This virtual platform has no PDC capture source.
    platform.apss_pdc={moduletype='qcom,apss-pdc-v1',capture_slots=0,
        target_socket={address=0x0b200000,size=0x40000,bind='&apss_router.initiator_socket'}}
    -- QHEE 0x6a86c0 reads the word at 0x17c02c, tests bit zero, sets it, and
    -- writes the word back. The read-modify-write asserts an APSS clock-root
    -- enable. No silicon capture gives the full register, so the model retains
    -- bit zero only and starts it clear. It creates no clock or completion.
    -- QHEE clears the enable bit, then polls bit 31 in a 10000-step loop
    -- (clear 0xa86e0, poll 0xa8a84, delay 0xb7358). Bit 31 is the branch-off
    -- status, so the model reports it as the inverse of the retained enable
    -- bit. It creates no clock, delay, or timed transition.
    platform.qhee_apss_root_enable={moduletype='qcom,retained-boot-control-v1',
        writable_mask=0x1,reset_value=0,status_source_mask=0x1,status_bit=31,
        target_socket={address=0x17c02c,size=4,bind='&apss_router.initiator_socket'}}
    -- The same helper and poll serve the next entry of the root array at
    -- 0x17c068. The measured stride between the two words is 0x3c.
    platform.qhee_apss_root_enable_2={moduletype='qcom,retained-boot-control-v1',
        writable_mask=0x1,reset_value=0,status_source_mask=0x1,status_bit=31,
        target_socket={address=0x17c068,size=4,bind='&apss_router.initiator_socket'}}
    -- GPUCC = GFX_BASE + 0x94000. The four hypervisor
    -- vote registers at GPUCC + 0x2000..0x200c are HYP_VOTE_GPU_SMMU_CLK,
    -- HYP_VOTE_GPU_SMMU_GDS, HYP_VOTE_HUB_CX_INT_CLK, and HYP_VOTE_CX_GMU_CLK.
    -- Each mask is 0x80000001, so bit zero is the vote and bit 31 is the branch
    -- status. QHEE sets or clears the vote, then polls bit 31 as CLK_OFF.
    -- The model reports that status from the guest's own vote and creates no clock.
    for offset=0,3 do
        platform['qhee_gpu_vote_'..offset]={moduletype='qcom,retained-boot-control-v1',
            writable_mask=0x1,reset_value=0,status_source_mask=0x1,status_bit=31,
            target_socket={address=0x3d96000+offset*4,size=4,
                bind='&apss_router.initiator_socket'}}
    end
    -- XBL 0x1484b900 publishes bits 6:4 from its PMIC plan-selection byte.
    -- Retain that configuration without creating a voltage or calibration result.
    platform.xbl_cpr_plan_selection={moduletype='qcom,retained-boot-control-v1',
        writable_mask=0xffffffff,reset_value=0,
        target_socket={address=0x19978178,size=4,bind='&apss_router.initiator_socket'}}
    -- XBL 0x14899ddc..0x14899ea8 is the Railway MX prechange callback.
    -- Retain its AOSS ACC overrides with explicit virtual cold-zero state.
    -- Masks follow native writes, including MXA HMX bit 30, not comparator status.
    -- No analog measurement, voltage transition, or completion is generated.
    for _,control in ipairs({
        {0x304,0x44000000}, -- MXA HMX: selector bit 30 and override enable bit 26.
        {0x308,0x0c000000}, -- MXC HMX: override value bit 27 and enable bit 26.
        {0x310,0x1c000000}, -- MXA LMX: override value bits 28:27 and enable bit 26.
        {0x314,0x1c000000}, -- MXC LMX: override value bits 28:27 and enable bit 26.
    }) do
        platform['xbl_railway_acc_'..control[1]]={moduletype='qcom,retained-boot-control-v1',
            writable_mask=control[2],reset_value=0,
            target_socket={address=0x0c260000+control[1],size=4,bind='&apss_router.initiator_socket'}}
    end
    -- The virtual debugger requests power without granting debug access.
    -- XBL 0x14828160 reads CDBGPWRUPREQ status before it starts the APSS watchdog.
    platform.xbl_debug_power={moduletype='gs_memory',init_mem=true,read_only=true,
        load={offset=0,data={['0']=0x80000000}},
        target_socket={address=0x0c2a9010,size=4,bind='&apss_router.initiator_socket'}}
    -- XBL descriptors 0x148f3600..0x148f3750 read these corrected feature rows.
    -- The virtual profile declares no fused CPU or peripheral defects.
    platform.xbl_cpu_binning={moduletype='gs_memory',init_mem=true,read_only=true,
        target_socket={address=0x221c21a0,size=4,bind='&apss_router.initiator_socket'}}
    platform.xbl_peripheral_binning={moduletype='gs_memory',init_mem=true,read_only=true,
        target_socket={address=0x221c2418,size=0x10,bind='&apss_router.initiator_socket'}}
    -- XBL diagnostic table 0x148d4400 declares two raw feature rows.
    -- The virtual profile declares both rows zero, not measured silicon values.
    platform.xbl_feature_rows={moduletype='gs_memory',init_mem=true,read_only=true,
        target_socket={address=0x221c2188,size=0x10,bind='&apss_router.initiator_socket'}}
    -- XBL 0x1484ecd4 and 0x1484ece0 select software control for the AOSS PLLs.
    -- The virtual profile retains these selections without an automatic hardware controller.
    platform.xbl_pll0_mode_select={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x0c2a901c,size=4,bind='&apss_router.initiator_socket'}}
    platform.xbl_pll1_mode_select={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x0c2a9024,size=4,bind='&apss_router.initiator_socket'}}
    -- XBL 0x1484edc8..0x1484ee0c programs five four-bit AOSS divider fields.
    -- The virtual profile retains these words without cycle-accurate divider timing.
    for _,offset in ipairs({0xb8,0xc0,0x1bc,0x1d8,0x1ec}) do
        platform['xbl_aoss_divider_'..offset]={moduletype='gs_memory',init_mem=true,
            target_socket={address=0x0c2a8000+offset,size=4,bind='&apss_router.initiator_socket'}}
    end
    -- XBL descriptors 0x148f13c8..0x148f1470 identify the AOSS and LPASS sources.
    platform.xbl_aoss_pll0={moduletype='qcom,gcc-taycan-eko-t-pll-v1',
        target_socket={address=0x0c2a0000,size=0x38,bind='&apss_router.initiator_socket'}}
    platform.xbl_aoss_pll1={moduletype='qcom,aoss-pongo-pll-v1',
        target_socket={address=0x0c2a1000,size=0x40,bind='&apss_router.initiator_socket'}}
    platform.xbl_lpass_plls={moduletype='qcom,gcc-taycan-eko-t-pll-v1',pll_count=2,
        target_socket={address=0x310a0000,size=0x1038,bind='&apss_router.initiator_socket'}}
    -- XBL 0x148f1810 selects PLL0 main. 0x148f18d0 selects its even output.
    platform.xbl_aoss_root0={moduletype='qcom,rcg-v1',pll_path='xbl_aoss_pll0',
        crystal_selector=0,primary_selector=4,require_root_enable=false,
        target_socket={address=0x0c2a8084,size=0x14,bind='&apss_router.initiator_socket'}}
    platform.xbl_aoss_root3={moduletype='qcom,rcg-v1',pll_path='xbl_aoss_pll0',
        crystal_selector=0,primary_even_selector=1,require_root_enable=false,
        target_socket={address=0x0c2a8180,size=0x14,bind='&apss_router.initiator_socket'}}
    -- XBL 0x148f1850, 0x148f1890, and 0x148f1910 select Pongo calibration, main, and even outputs.
    -- The calibration output uses the explicit virtual reference ratio of eight.
    platform.xbl_aoss_pll1.calibration_ratio=8
    platform.xbl_aoss_root1={moduletype='qcom,rcg-v1',pll_path='xbl_aoss_pll1',
        calibration_selector=2,require_root_enable=false,
        target_socket={address=0x0c2a80e8,size=0x14,bind='&apss_router.initiator_socket'}}
    platform.xbl_aoss_root2={moduletype='qcom,rcg-v1',pll_path='xbl_aoss_pll1',
        primary_selector=6,require_root_enable=false,
        target_socket={address=0x0c2a80a0,size=0x14,bind='&apss_router.initiator_socket'}}
    platform.xbl_aoss_root4={moduletype='qcom,rcg-v1',pll_path='xbl_aoss_pll1',
        primary_even_selector=3,require_root_enable=false,
        target_socket={address=0x0c2a8198,size=0x14,bind='&apss_router.initiator_socket'}}
    -- XBL 0x1484ee50..0x1484ee70 selects the AOSS PLL reference.
    -- This profile retains the selector and keeps the explicit 19.2 MHz virtual reference.
    platform.xbl_aoss_reference_select={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x0c2a900c,size=4,bind='&apss_router.initiator_socket'}}
    -- XBL 0x1484ee78..0x1484eef0 initializes three independent analog reference blocks.
    for index,address in ipairs({0x00ff1000,0x088e7000,0x00ff0000}) do
        platform['xbl_refgen_'..index]={moduletype='qcom,refgen-nxgen-v1',
            virtual_power=true,calibration_policy='bounded-virtual',calibration_delay_us=5,
            target_socket={address=address,size=0xac,bind='&apss_router.initiator_socket'}}
    end
    dofile(directory..'../xbl-boot-clocks.lua')(platform)
    -- XBL 0x14860684 collapses an unused domain. An unbound domain cannot power on.
    platform.xbl_unused_domain={moduletype='qcom,gdsc-v1',
        target_socket={address=0x147170,size=8,bind='&apss_router.initiator_socket'}}
    -- XBL 0x1485f320 initializes a version, a count, and ten AOP message records.
    -- The firmware writes all record contents. This memory generates no completion.
    platform.xbl_aop_clock_records={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x0c3e0000,size=0x58,bind='&apss_router.initiator_socket'}}
    dofile(directory..'../aop-configuration.lua')(platform)
    -- XBL descriptor 0x148f1748 uses the Taycan interface at 0x06e00000.
    -- Its configuration mask differs from the voted GCC sources. No vote input is connected.
    platform.xbl_external_pll={moduletype='qcom,gcc-taycan-eko-t-pll-v1',
        target_socket={address=0x06e00000,size=0x38,bind='&apss_router.initiator_socket'}}
    -- UEFI SPSSDxe RVA 0x2ae4 marks an SPSS load failure in SP_CNOC_SP_SCSR_RMB_GP0
    -- (POR zero). The package leaves spunvm unwritten, so the SPU image is absent.
    -- No SPU model consumes these words.
    platform.spss_load_status={moduletype='qcom,retained-boot-control-v1',writable_mask=1,
        target_socket={address=0x01881100,size=4,bind='&apss_router.initiator_socket'}}
    -- SPSSDxe RVA 0x2c98 then writes a 16-bit status word with the UFS flash-type bit 15
    -- to SP_SCSR_MB3_CL2SP_GP0, and RVA 0x2cb4 writes one byte to MB3_CL2SP_GP1.
    -- Both registers are RW with POR zero.
    for _,word in ipairs({{'spss_client_status',0x0188b028,0xffff},{'spss_client_byte',0x0188b02c,0xff}}) do
        platform[word[1]]={moduletype='qcom,retained-boot-control-v1',writable_mask=word[3],
            target_socket={address=word[2],size=4,bind='&apss_router.initiator_socket'}}
    end
    -- SPSSDxe RVA 0x30b8..0x30f0 (keymint_info_share_with_spu) writes a 32-byte image
    -- hash to TCSR SYSTEM_IMAGES_HASH_0..7 and all ones to HASH_8..15. The 16
    -- words at TCSR 0xa5004..0xa5040 are RW with POR zero.
    -- The SPU image is absent, so no model reads them.
    for index=0,15 do
        platform[string.format('spss_system_images_hash_%d',index)]={
            moduletype='qcom,retained-boot-control-v1',writable_mask=0xffffffff,
            target_socket={address=0x01fe5004+4*index,size=4,bind='&apss_router.initiator_socket'}}
    end
    dofile(directory..'../xbl-peripheral-clocks.lua')(platform)
    dofile(directory..'../xbl-tlmm.lua')(platform, {gpio_count=217})
    dofile(directory..'../xbl-qup-firmware.lua')(platform, 'sm8850')
    local pmic_identities = {
        {0,0,0x5c,2,0,1,0}, {0,1,0x5d,2,0,2,0},
        {0,3,0x61,2,1,2,0}, {0,5,0x61,2,1,1,0},
        {0,6,0x61,2,1,2,0}, {0,8,0x61,2,1,2,0},
        -- Selected PBS metadata at 148ee644 requires the flat, one-SID identity.
        {1,0,0x5c,2,0,1,0}, {1,7,0x56,2,1,1,0},
        {1,9,0x60,2,1,1,0}, {1,10,0x48,2,0,2,0},
        {1,12,0x41,1,3,2,0},
    }
    local pmic_identity_rows, pmic_pon_rows = {}, {}
    for _, identity in ipairs(pmic_identities) do
        pmic_identity_rows[#pmic_identity_rows+1] = table.concat(identity, ' ')
        for sid=identity[2],identity[2]+identity[6]-1 do
            pmic_pon_rows[#pmic_pon_rows+1] = string.format('%d %d 8', identity[1], sid)
        end
    end
    -- The live-phone record 20261026T180725Z reads type 0x14 and subtype 0x6b
    -- at peripherals 0x40..0x43 of bus 0, SID 1 (pmh0101).
    -- Type 20 is MISC, and the MISC subtype table has no 0x6b entry. The misc
    -- service reports each identity and the read-only zero span 0x08..0x1f.
    -- QSEE reads these blocks in eight-byte bursts from 0x08 through 0xc8.
    -- Offsets 0x40..0xcf stay with the exploratory service, which retains the
    -- guest writes at 0x4146, 0x4346, 0x4d46, and 0x4e46.
    -- The rows below answer the recorded zero bytes outside every owner.
    -- The warm flag 0xda and the trim pair 0xf0..0xf1 keep post-boot state on
    -- the record, so they read cold zero here.
    local phone_zero_span = {}
    local function zero_bytes(peripheral, spans)
        for _, span in ipairs(spans) do
            for offset=span[1],span[2] do
                phone_zero_span[#phone_zero_span+1] =
                    string.format('0 1 0x%02x%02x 0 0', peripheral, offset)
            end
        end
    end
    for peripheral=0x40,0x43 do
        zero_bytes(peripheral, {{0x20,0x3f},{0xd0,0xd9},{0xdb,0xef},
                                {0xf0,0xff}})
    end
    platform.xbl_pmic = {
        moduletype='qcom,virtual-pmic-registers-v1',
        exploratory_configuration=true,
        virtual_absent_discovery=true,
        identities=table.concat(pmic_identity_rows, ';'),
        pon_peripherals=table.concat(pmic_pon_rows, ';'),
        -- PMK8850 gen3 PON HLOS group. See models/qcom/spmi/pmic.h pon_hlos_peripherals.
        pon_hlos_peripherals='0 0 0x13',
        -- QSEE 0x15735970 dumps each present peripheral that the devcfg bitmap
        -- selects. PM_NEW_DUMP_FRAMEWORK reads one in devcfg at file offset 0x946c.
        -- PM_PERIPH_LIST_BITMAP resolves through the runtime blob registry, so the
        -- bitmap bytes are not in the image. The dump reads only groups whose
        -- offset-4 presence byte is nonzero, and it stores the bytes without a
        -- reader. The present profile therefore adds no read that QSEE omits.
        -- Run511 reads PON at 0x08d8, 0x1608, 0x1c08, 0x2208, and 0x5000.
        dump_peripherals='present',
        -- Run530 shows the same reader on an unmarked path: it reads eight-byte
        -- bursts from 0x08 to the end of each present block and stores the bytes.
        -- Bytes without an owner read cold zero and report one line each.
        -- Run539 shows the same burst on the PBS group at bus one, SID seven.
        -- That burst keeps the interface bytes and moves no transfer pointer.
        scan_block_reads=true,
        registers='0 0 0x9a5a 0 3;0 0 0xa245 0 0x80;' ..
            -- PSI conditions at 14912d40 select electrical configuration.
            -- These opaque inputs start clear in the virtual profile.
            '1 7 0x2580 0 0;1 7 0x3050 0 0;1 7 0x3051 0 0;' ..
            '1 7 0x0655 0 0;1 7 0xf270 0 0;0 1 0x3e09 0 0;' ..
            -- This option retains requests. No physical calibration is inferred.
            '0 6 0xb751 0 0xff;' ..
            -- DCDC POWER_PATH_STATUS: battery path, with no external input.
            '1 7 0x270b 2 0;' ..
            -- OEM address tables list this warm flag on peripherals 0x41 and
            -- 0x43 (0x148f63a8 and 0x148fd680), and that file already declares
            -- them. The guest owns the flag, and a cold boot has no warm flag.
            -- The document also reads it on 0x40 and 0x42.
            '0 1 0x40da 0 0xff;0 1 0x42da 0 0xff;' ..
            table.concat(phone_zero_span, ';'),
    }
    platform.xbl_spmi = {
        moduletype='qcom,spmi-arbiter-packed-v1', executing_owner=0,
        -- Secure loader configuration uses the owner map at XBL 0x148f3abe.
        secure_write_owner_mask=0x1ffe,
        -- QSEE 0x15738dc0 reads observer EE1 status at 0x0c500208.
        secure_observer_ee=1,
        initial_bus_enable=0, initial_routes='', completion_policy='synchronous-virtual',
        ibus_policy='disconnected',
        target_socket={address=0x0c400000,size=0x00900000,bind='&apss_router.initiator_socket'},
        pmic_socket={bind='&xbl_pmic.target_socket'},
    }
    -- XBL 0x1488d424..0x1488d42c sets AOSS bit 29 before IBUS configuration.
    -- The virtual cold value is zero. No external IBUS producer is connected.
    platform.xbl_ibus_aoss_control={moduletype='qcom,retained-boot-control-v1',
        writable_mask=0x20000000,reset_value=0,
        target_socket={address=0x0c260208,size=4,bind='&apss_router.initiator_socket'}}
    dofile(directory..'../xbl-pmic-peripherals.lua')(platform, {
        unassigned_manufacturing=true,xo_configuration_variant=1,
        sleep_clock_configuration_variant=0,
        -- PSI loads at 148f4ab8, 148f4cc8, 148f4ee4, and 148f5694.
        -- Revision 5.1 and these capacities are explicit virtual policy.
        pbs='0 0 0x1c 0x0501 0 0x1080;0 0 0x16 0x0501 0 0xc000;' ..
            '0 0 0x22 0x0501 0 0x1280;1 7 0x16 0x0501 0 0x0800',
    })
    dofile(directory..'xbl-pmic-controls.lua')(platform)
    -- Selected XBL 14885064 selects 32-byte discovery masks by PMIC model.
    -- Masks: 5d=148f3ee4, 61=148f3f44, 60=148f3f64,
    -- 48=148f40c4, 41=148f3fe4. Adjacent masks are not secondary SID banks.
    -- SMPS allocator 14882b50..14882bcc declares ten rails for model61,
    -- and four for model60. Table148f3db6 gives peripheral stride3.
    -- LDO allocator 1487fa7c..1487fb20 declares 18/4/7 rails for models5d/61/48,
    -- and seven for model41. Table148f3cea gives peripheral stride1.
    -- BOB allocator1487f098..1487f0d4 declares two rails for model5d.
    -- Table148f3cb0 gives stride2, and the model5d mask includes 0xe4..0xe7.
    -- The live-phone record 20261026T180725Z reads type29 subtype7 at
    -- peripherals0xe4 and0xe6 of bus0 SID1, so the BOB pair sits there.
    -- The same mask also includes 0x40..0x43, and the record reads type14
    -- subtype6b there. Those four blocks are MISC, not BOB controls.
    -- Run526 failed because it declared MISC at 0x40 and kept the second BOB
    -- slot empty. The regulator initialization clears OCP status for each
    -- allocated BOB slot. The power driver returns PM_ERR_FLAG_INVALID_PERIPHERAL
    -- (32) when a slot has no discovered type. The two hardware slots fill both.
    -- Selected DTB0 PMIC nodes at1804..19d0 and1b04..1d4c supply board names.
    -- The declared SID8 model61 bank follows the runtime identity policy.
    -- PMK8850(bus0/1 SID0) and PMIH0104(bus1 SID7) have no regulator bank here.
    -- Secondary identity SIDs do not gain regulator aliases.
    -- These complete virtual banks expose only regulator control peripherals.
    -- SMPS power-stage and frequency peripherals remain separate PSI controls.
    -- Revisions and electrical limits come from the existing virtual service.
    -- They are not physical revision, voltage, or reset measurements.
    local regulator_rows = {
        '0 1 0xc1 18 4',
        '0 1 0xe4 1 0x29', '0 1 0xe6 1 0x29',
        '0 3 0xc1 4 4', '0 5 0xc1 4 4',
        '0 6 0xc1 4 4', '0 8 0xc1 4 4',
        '1 10 0xc1 7 4', '1 12 0x40 7 4',
    }
    for _, bank in ipairs({{0,3,10}, {0,5,10}, {0,6,10}, {0,8,10}, {1,9,4}}) do
        for ordinal=0,bank[3]-1 do
            regulator_rows[#regulator_rows+1] = string.format('%d %d %d 1 3',
                bank[1], bank[2], 0x99 + 3 * ordinal)
        end
    end
    -- Native ordinal i maps to SMPS99+3*i, BOB e4+2*i, or LDOc1+i.
    -- PM8010 uses LDO40+i instead.
    -- DDR14871134..1487116c sets PWM on SID6/S5(a5) and SID8/S1(99).
    -- DDR1487118c..148711d4 requests 1052mV on SID3/S10(b4),
    -- and 900mV on SID8/S7(ab). The guest supplies all requests.
    -- Selected DTBs at xbl_config offsets 2ac/10e14 have 27 post-driver
    -- records. Board callbacks also configure SID1 pins10..17 and SID6 pins4..9.
    -- These are supported virtual banks, not physical revision claims.
    dofile(directory..'../xbl-pmic-post-init-inputs.lua')(platform, {
        gpios='0 0 0xd0 8;0 1 0x88 20;0 3 0x88 14;' ..
            '0 6 0x88 14;0 8 0x88 14;1 7 0x88 18',
        regulators=table.concat(regulator_rows, ';'),
        -- Live-phone identity at bus0 SID1: type14 subtype6b at 0x40..0x43,
        -- and type29 subtype8 (BOB monitoring) at 0xe5 and 0xe7.
        -- The service reports each revision and read-only zero bytes 08..1f.
        misc='0 1 0x40 0x14 0x6b;0 1 0x41 0x14 0x6b;' ..
            '0 1 0x42 0x14 0x6b;0 1 0x43 0x14 0x6b;' ..
            '0 1 0xe5 0x29 0x08;0 1 0xe7 0x29 0x08',
        clocks='0 0 0x50 1;0 0 0x54 8;0 0 0x55 8;' ..
            '0 0 0x56 8;0 0 0x57 8;0 0 0x64 15',
    })
    dofile(directory..'ddr.lua')(platform)
    dofile(directory..'ddr-controls.lua')(platform)
    dofile(directory..'ddr-phy.lua')(platform)
    dofile(directory..'../xbl-ddr-interconnect.lua')(platform,
        dofile(directory..'xbl-ddr-interconnect.lua'))
    dofile(directory..'../xbl-eud.lua')(platform, {
        second_branch=0x139090, second_branch_hardware_control=true,
    })
    dofile(directory..'../xbl-power-domains.lua')(platform)
    dofile(directory..'tsens.lua')(platform)
    dofile(directory..'rpmh.lua')(platform)
    dofile(directory..'soccp.lua')(platform, {
        router='apss_router',memory_bind='&apss_router.target_socket',pll_path='gcc_plls',
        secure_irq_bind='&apss_gic.spi_in_229',
        tme_irq_bind='&tme_clic.source_38',
        irq_binds={'&apss_gic.spi_in_246','&apss_gic.spi_in_230','&apss_gic.spi_in_158',
            '&apss_gic.spi_in_715','&apss_gic.spi_in_70'},
    })
    dofile(directory..'../aop-qmp.lua')(platform, {router='apss_router', ipcc_path='soccp_ipcc_0'})
    dofile(directory..'ufs-xbl.lua')(platform)
    dofile(directory..'smmu.lua')(platform, {
        router='apss_router',
        irq_bind=function(gsiv) return '&apss_gic.spi_in_'..(gsiv-32) end,
    })
    dofile(directory..'xbl-protection.lua')(platform)
    dofile(directory..'../xbl-boot-policy.lua')(platform)
    dofile(directory..'../xbl-vmid-mappers.lua')(platform, 'sm8850')
    -- QSEE writes all ones after VMIDMT setup, from 0x157665d4..0x15766770.
    -- The AC root 0x14 list adds 0x01fc50d0..0x01fc50dc, written at 0x157698c4.
    -- See models/qcom/vmidmt/tz-platform-evidence.md.
    dofile(directory..'../qsee-retained-controls.lua')(platform, 'tcsr',
        {0x01fc5040,0x01fc3040,0x01fc5050,0x01fc2050,0x01fc50d0,0x01fc50d4,0x01fc50d8,0x01fc50dc})
    -- QSEE 0x15682af0 reads the TME PRNG interrupt control.
    -- The Infiniti EL1 probe read value one at CPU virtual address 0x01fb300c.
    platform.qsee_tme_prng_irq_enable={moduletype='qcom,retained-boot-control-v1',
        writable_mask=1,reset_value=1,
        target_socket={address=0x01fb300c,size=4,bind='&apss_router.initiator_socket'}}
    -- QSEE 0x15682b08 writes bit zero when the TME control permits the interrupt.
    platform.qsee_prng_irq_enable={moduletype='qcom,retained-boot-control-v1',
        writable_mask=1,reset_value=0,
        target_socket={address=0x06800040,size=4,bind='&apss_router.initiator_socket'}}
    -- QSEE 0x15682b90 reads the second TME interrupt control.
    -- The Infiniti EL1 probe read zero at CPU virtual address 0x01fbc00c.
    platform.qsee_tme_secondary_irq_enable={moduletype='qcom,retained-boot-control-v1',
        writable_mask=1,reset_value=0,
        target_socket={address=0x01fbc00c,size=4,bind='&apss_router.initiator_socket'}}
    -- TZ_QTI_Config RegisterSettings targets outside the SMMU windows, from 0x15729940.
    -- The GPU writer at 0x156e5d38 follows them. See models/qcom/smmu/evidence.md.
    dofile(directory..'../qsee-retained-controls.lua')(platform, 'hsr', {
        0x151fc010,0x151fc030,0x151fc038,0x151fc040,0x151fc048,0x151fc050,0x151fc058,
        0x151fc060,0x151fc068,0x151fc080,0x151fc088,0x151fc090,0x151fc098,0x151fc0a0,
        0x151fc0a8,0x151fc0b0,0x151fc0b8,0x151fc0d0,0x151fc0d8,
        0x03de0010,0x03de0038,0x03de0040,0x03de8008,0x03de8020,
    })
    dofile(directory..'../qsee-retained-controls.lua')(platform, 'gmu',
        {0x03d60000,0x03d60004,0x03d60008,0x03d6000c})
    -- TZ AC root 0x1d policy-control words without an XBL owner, from 0x15769b3c.
    -- See models/qcom/sm88xx/tme_protection/tz-inventory-evidence.md.
    dofile(directory..'../qsee-retained-controls.lua')(platform, 'xpu',
        {0x03f24000,0x03f24004,0x09b2a000,0x01d10000})
    -- QSEE 0x1574cbd0 writes the MDSS software fuse words and checks bit zero.
    -- DT node mdss_mdp names this block swfuse_phys. See docs/qcom/sm88xx/stubs.md.
    dofile(directory..'../qsee-retained-controls.lua')(platform, 'mdss',
        {0x09aa0058,0x09aa0080,0x09aa0084,0x09aa0088})
    dofile(directory..'../qsee-retained-controls.lua')(platform, 'icb',
        dofile(directory..'qsee-icb.lua'))
    -- QSEE 0x15779c18 writes and verifies nine secure-camera protection words.
    -- The bank has no modeled consumer. See docs/qcom/sm88xx/boot-dependencies.md.
    dofile(directory..'../qsee-retained-controls.lua')(platform, 'camera',
        dofile(directory..'../qsee-camera-protection.lua'))
    -- QSEE 0x15731b20 publishes the SPSS SMEM allocation. See docs/qcom/sm88xx/stubs.md.
    dofile(directory..'../qsee-retained-controls.lua')(platform, 'spss',
        {0x01887000,0x01887008,0x0188700c,0x01887010})
    dofile(directory..'../crypto-bam.lua')(platform)
    dofile(directory..'../ipa.lua')(platform)
    dofile(directory..'../dcp-runtime.lua')(platform,options,'sm8850')
    dofile(directory..'../dcp-bootstrap.lua')(platform)
    dofile(directory..'../qsee-fault-monitors.lua')(platform,'sm8850')
    dofile(directory..'../qsee-clock-dependencies.lua')(platform,'sm8850')
    dofile(directory..'../qsee-qup-common.lua')(platform,'sm8850')
    dofile(directory..'../dcp-clocks.lua')(platform,'sm8850')
    dofile(directory..'../uefi-clock-dependencies.lua')(platform,'sm8850')
    dofile(directory..'../qsee-gpce.lua')(platform)
    dofile(directory..'../tme-image-admission.lua')(platform, {
        current_image_context_address=0x448930,
    })
    dofile(directory..'../aop-resource-service.lua')(platform, {
        profile='sm8850', observer_path='tme_image_admission',
    })
    dofile(directory..'../tme-power-state.lua')(platform)
    dofile(directory..'../pdp-runtime.lua')(platform, options)
    return platform
end
