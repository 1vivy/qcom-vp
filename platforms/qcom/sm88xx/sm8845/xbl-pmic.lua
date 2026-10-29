-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- CPH2771 XBL transport descriptors: 0x148f1520 and 0x148f1578.
-- Board topology: package vendor_boot.img Alor DTB and dtbo.img overlays.
-- Model IDs follow public Linux socinfo.c. Revisions/spans are virtual policy.
-- The base debug tree names PMK8850. The overlay retains a pmk8550 node name.
-- See models/qcom/sm88xx/tme_runtime_abi/evidence.md for this explicit selection.
local directory=assert(debug.getinfo(1,'S').source:match('^@(.*/)'))
return function(platform)
    assert(not platform.xbl_pmic and not platform.xbl_spmi, 'XBL PMIC is already configured.')
    -- One SID per declared node. Do not populate disabled overlay nodes 2 and 9.
    -- Bus one has no populated identity in this selected board inventory.
    platform.xbl_pmic={moduletype='qcom,virtual-pmic-registers-v1',
        exploratory_configuration=true,
        virtual_absent_discovery=true,
        identities='0 0 0x5c 1 0 1 0;0 1 0x49 1 0 1 0;' ..
            '0 3 0x46 1 0 1 0;0 4 0x57 1 0 1 0;0 5 0x46 1 0 1 0;' ..
            '0 6 0x46 1 0 1 0;0 7 0x4c 1 0 1 0;0 8 0x46 1 0 1 0;' ..
            '0 10 0x48 1 0 1 0;0 12 0x41 1 0 1 0;0 13 0x41 1 0 1 0',
        -- Only PMK's selected PON08 contract is enabled, not every populated SID.
        pon_peripherals='0 0 8',
        -- QSEE 0x15735970 dumps each present peripheral that the devcfg bitmap
        -- selects. PM_NEW_DUMP_FRAMEWORK reads one in devcfg at file offset 0x93cc.
        -- PM_PERIPH_LIST_BITMAP (index 0x14) resolves through the runtime blob
        -- registry, so the bitmap bytes are not in the image. SM8850 run511 reads
        -- PON at 0x08d8 and three more groups. The present profile adds no read.
        dump_peripherals='present',
        registers='0 0 0x9a5a 0 3;0 0 0xa245 0 0x80;' ..
            -- OEM 0x1483990c writes display/ibb-phase-select to SID4:f83d.
            -- Native caller 0x14839910 supplies byte 02 through APID86.
            -- Store the guest's phase request, not an analog result or readiness.
            -- Reset zero means no request. It is not a physical reset capture.
            '0 4 0xf83d 0 0xff;' ..
            -- PSI conditions at 14912fc8 select electrical configuration.
            -- No autonomous PBS producer changes these virtual cold inputs.
            '0 0 0x16cc 0 0;0 0 0x1ecc 0 0;0 7 0xf270 0 0;' ..
            '0 4 0x095c 0 0;0 4 0xf8d3 0 0;0 7 0xf8b4 0 0;' ..
            -- PBS instructions at 1490748c and 149074a0 also write this history.
            '0 4 0xf9cf 0 0xff;' ..
            -- DCDC POWER_PATH_STATUS: battery path, with no external input.
            '0 7 0x270b 2 0',
    }
    platform.xbl_spmi={moduletype='qcom,spmi-arbiter-packed-v1',executing_owner=0,
        -- Secure loader configuration uses the owner map at XBL 0x148f14c6.
        secure_write_owner_mask=0x0ffe,
        -- QSEE 0x1572ba14 reads observer EE1 status at 0x0c500508.
        secure_observer_ee=1,
        initial_bus_enable=0,initial_routes='',completion_policy='synchronous-virtual',
        target_socket={address=0x0c400000,size=0x00900000,bind='&apss_router.initiator_socket'},
        pmic_socket={bind='&xbl_pmic.target_socket'}}
    -- Exact first-bank masks from CPH2771 selector 0x14884724, table 0x148ddc20.
    -- Existing shared masks for 0x5c, 0x48 and 0x41 are byte-identical here.
    local discovery={
        [0x49]={{{0x1,0x1},{0x4,0x6},{0x8,0xb},{0xd,0xf},{0x13,0x15},{0x1c,0x1d},{0x36,0x36},{0x39,0x39},{0x3c,0x3c},{0x3e,0x3e},{0x40,0x41},{0x47,0x4a},{0x51,0x51},{0x5e,0x5e},{0x88,0x93},{0x98,0x99},{0xc1,0xd1},{0xe1,0xe2},{0xe4,0xeb},{0xee,0xef},{0xfe,0xff}}},
        [0x46]={{{0x1,0x1},{0x4,0x6},{0x8,0xb},{0xe,0xf},{0x1c,0x1d},{0x36,0x36},{0x39,0x39},{0x3c,0x3c},{0x51,0x51},{0x5e,0x5e},{0x88,0x8f},{0x99,0xb2},{0xc1,0xc3},{0xe1,0xe1},{0xfe,0xff}}},
        [0x57]={{{0x1,0x1},{0x4,0x6},{0x8,0xb},{0xe,0xe},{0x1c,0x1d},{0x4b,0x4d},{0x51,0x51},{0x5e,0x5e},{0x88,0x8b},{0x99,0x99},{0xf8,0xfa},{0xfe,0xff}}},
        [0x4c]={{{0x1,0x1},{0x4,0x6},{0x8,0xb},{0xe,0xf},{0x14,0x14},{0x16,0x19},{0x1c,0x1f},{0x21,0x21},{0x26,0x29},{0x2b,0x2e},{0x30,0x30},{0x36,0x36},{0x39,0x3a},{0x3c,0x3d},{0x47,0x48},{0x4e,0x4f},{0x51,0x51},{0x5e,0x5e},{0x68,0x68},{0x70,0x73},{0x88,0x8f},{0xdf,0xdf},{0xf8,0xfa},{0xfd,0xfe}}},
    }
    dofile(directory..'../xbl-pmic-peripherals.lua')(platform,{
        discovery=discovery,
        unassigned_manufacturing=true,xo_configuration_variant=1,
        sleep_clock_configuration_variant=0,
        -- CPH2771 0x14882da0 declares 72 banks for models 0x4a and 0x5c.
        -- Its 0x14882efc..0x14882fac loop requires type 0x2e at every bank.
        -- This is the selected virtual capacity, not a physical reset capture.
        sdam='0 0 0x70 72',
        -- PSI loads at 148f25bc, 148f26e8, 148f2838, and four SID7 variants.
        -- Revision 5.1 and these capacities are explicit virtual policy.
        pbs='0 0 0x1a 0x0501 0 0x1680;0 0 0x16 0x0501 0 0xc000;' ..
            '0 0 0x1e 0x0501 0 0x3200;0 7 0x16 0x0501 0 0x1000',
        -- The selected display branch also writes PMK9abf=0 and PMK7158=1.
        -- Both remain guest-written retained SDAM, not preset success bytes.
    })
    -- PON init 0x148311a0..0x148311fc also probes 0800/6 outside scan masks.
    -- The selected PM8010 mask at 0x148f19fc omits peripheral08.
    -- These two virtual PM8010 identities have no PON block. Only their six
    -- revision bytes report absence. Other bytes remain unsupported.
    platform.xbl_pmic.absent_peripherals =
        platform.xbl_pmic.absent_peripherals .. ';0 12 8;0 13 8'
    dofile(directory..'xbl-pmic-controls.lua')(platform)
    -- CPH2771 14884230..148846f0 keeps PMIC ordinals 0..13, including holes.
    -- Each selected identity spans one SID. Thus ordinal5 is SID5, not SID6.
    -- SMPS init 14882260..14882284 allocates eight rails for model46.
    -- Layout 148f17ce uses stride0300. The selected mask starts at9900.
    -- Declare control banks only, not their frequency/power-stage neighbors.
    -- These coherent rail types are virtual board policy, not captured silicon.
    local regulators={
        -- LDO init 1487e0f8..1487e124: model49=17, model46=3, model48=7.
        -- Layout148f16f2 uses stride0100. Preserve the complete PMIC1 bank.
        '0 1 0xc1 17 4','0 10 0xc1 7 4',
        -- Model41 has seven LDOs at4000..4600 on each selected PM8010.
        '0 12 0x40 7 4','0 13 0x40 7 4',
        -- Model49 allocates two BOB slots, with stride0200 at148f16b8.
        -- Select virtual controls3e/40 from mask148f191c, not captured types.
        -- Init1487d7d8..1487d814 maps these controls to slots0 and1.
        -- A lone control40 leaves slot1 absent and status14889c24 returns32.
        '0 1 0x3e 1 0x29','0 1 0x40 1 0x29',
    }
    -- Selected Alor DTB names PM8550VE instances D/F/G/I at SIDs3/5/6/8.
    -- Its RPMh resources corroborate the eight-SMPS and three-LDO capacities.
    -- The OEM discovery masks, not those resource names, select direct banks.
    for _, sid in ipairs({3,5,6,8}) do
        regulators[#regulators+1]=string.format('0 %d 0xc1 3 4',sid)
        for index=0,7 do
            regulators[#regulators+1]=string.format('0 %d 0x%x 1 3',sid,0x99+3*index)
        end
    end
    -- Selected DTBs at xbl_config offsets 30c/f52c have 22 post-driver
    -- records. Supply their GPIO/LDO requests through supported virtual banks.
    dofile(directory..'../xbl-pmic-post-init-inputs.lua')(platform, {
        gpios='0 0 0xd0 8;0 1 0x88 12;0 7 0x88 8',
        -- DDR init1486f848/1486f874 requests SMPS(5,0,3)/(5,5,3).
        -- These map to SID5 controls99/a8, unlike the SM8850 call arguments.
        -- Voltage requests also use (5,4), (6,2), and (6,3): a5/9f/a2.
        -- READY remains a result of the guest's rail enable state.
        regulators=table.concat(regulators,';'),
        clocks='0 0 0x50 1;0 0 0x64 15',
    })
    -- Unconditional OEM tail stores: 1483a538 and 1483a550. These bytes
    -- retain configuration requests. Neither byte is a hardware-ready result.
    platform.xbl_pmic.registers = platform.xbl_pmic.registers ..
        ';0 7 0x0945 0 0xff;0 7 0xfd46 0 0x80'
    return platform
end
