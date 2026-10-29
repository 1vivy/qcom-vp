-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- CPH2771 OEM runtime over the supplied QRD8850 compatibility ROM.
-- Caller policy, media and real image authentication remain unchanged.
local directory=assert(debug.getinfo(1,'S').source:match('^@(.*/)'))
return function(platform, options)
    -- Independent protocols: OEM SoC admission and signed sequencer selection.
    platform.fuses.chip_id=0xa0220100
    platform.tcsr_identity.hardware_version=0xa0220100
    platform.tme_boot_status.hardware_version=0xa01b0200
    local hardware=dofile(directory..'runtime-data.lua')
    assert(#hardware.clients==55 and #hardware.protection==31 and #hardware.debug_addresses==30,
        'The CPH2771 descriptor groups must remain independent.')
    dofile(directory..'../tme-runtime.lua')(platform,options,hardware)
    dofile(directory..'../tme-cold-controls.lua')(platform)
    dofile(directory..'../xbl-calibration-fuses.lua')(platform)
    dofile(directory..'cpr.lua')(platform)
    -- XBL watchdog control uses the same KPSS register layout and 32768 Hz clock.
    platform.apss_watchdog={moduletype='qcom,kpss-watchdog-v1',clock_hz=32768,
        security_socket={address=0x17908000,size=4,bind='&apss_router.initiator_socket'},
        target_socket={address=0x17908004,size=0x14,bind='&apss_router.initiator_socket'},
        bark={bind='&apss_gic.spi_in_26'}}
    -- The selected QSEE table also grants nonsecure mode at 0x17600000.
    platform.os_watchdog={moduletype='qcom,kpss-watchdog-v1',clock_hz=32768,
        security_socket={address=0x17600000,size=4,bind='&apss_router.initiator_socket'},
        target_socket={address=0x17600004,size=0x24,bind='&apss_router.initiator_socket'},
        bark={bind='&apss_gic.spi_in_0'},bite={bind='&apss_gic.spi_in_1'}}
    -- The selected QSEE table configures 192 PDC ownership bits.
    -- This virtual platform has no PDC capture source.
    platform.apss_pdc={moduletype='qcom,apss-pdc-v1',capture_slots=0,
        target_socket={address=0x0b200000,size=0x40000,bind='&apss_router.initiator_socket'}}
    -- XBL 0x14898754..0x14898820 is the Railway MX prechange callback.
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
    local function input(name,address,size,value)
        platform[name]={moduletype='gs_memory',init_mem=true,read_only=true,
            target_socket={address=address,size=size,bind='&apss_router.initiator_socket'}}
        if value then platform[name].load={offset=0,data={['0']=value}} end
    end
    -- XBL 0x14826268..0x14826290 checks CDBGPWRUPREQ bit 31.
    -- This external request policy grants no debug access.
    input('xbl_debug_power',0x0c2a9010,4,0x80000000)
    -- CPH2771 descriptors 0x148f0fb8..0x148f1048 and 0x148d2400.
    -- Zero is virtual feature policy, not a physical fuse measurement.
    input('xbl_cpu_binning',0x221c21a0,4)
    input('xbl_peripheral_binning',0x221c2418,0x10)
    input('xbl_feature_rows',0x221c2188,0x10)
    -- The reset platform already supplies cold-zero IMEM and SHRM backing.
    -- CPH2771 0x148e9f88..0x148e9fa0 confirms the SHRM base/size pairs.
    dofile(directory..'xbl-clocks.lua')(platform)
    dofile(directory..'../sm8850/ufs-xbl.lua')(platform)
    dofile(directory..'../sm8850/smmu.lua')(platform, {
        router='apss_router',
        irq_bind=function(gsiv) return '&apss_gic.spi_in_'..(gsiv-32) end,
    })
    dofile(directory..'ddr.lua')(platform)
    dofile(directory..'ddr-controls.lua')(platform)
    dofile(directory..'ddr-phy.lua')(platform)
    dofile(directory..'../xbl-ddr-interconnect.lua')(platform,
        dofile(directory..'xbl-ddr-interconnect.lua'))
    dofile(directory..'../xbl-eud.lua')(platform, {
        second_branch=0x139030, second_branch_hardware_control=false,
    })
    dofile(directory..'../xbl-power-domains.lua')(platform, {0x0958e334,0x03d99080,0x03d68024})
    dofile(directory..'tsens.lua')(platform)
    -- Selected Molokai DT declares 205 GPIOs. UART still uses pins62/63.
    dofile(directory..'../xbl-tlmm.lua')(platform, {gpio_count=205})
    dofile(directory..'../xbl-qup-firmware.lua')(platform, 'sm8845')
    platform.xbl_unused_domain={moduletype='qcom,gdsc-v1',
        target_socket={address=0x147170,size=8,bind='&apss_router.initiator_socket'}}
    -- XBL 0x1485d864 initializes version, count and ten guest-owned records.
    platform.xbl_aop_clock_records={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x0c3e0000,size=0x58,bind='&apss_router.initiator_socket'}}
    dofile(directory..'../aop-configuration.lua')(platform)
    dofile(directory..'xbl-pmic.lua')(platform)
    dofile(directory..'xbl-protection.lua')(platform)
    dofile(directory..'../xbl-boot-policy.lua')(platform)
    dofile(directory..'../xbl-vmid-mappers.lua')(platform, 'sm8845')
    -- QSEE writes all ones after VMIDMT setup, from 0x15759740..0x157598dc.
    -- The AC root 0x14 list adds 0x01fc50d0..0x01fc50dc.
    -- See models/qcom/vmidmt/tz-platform-evidence.md.
    dofile(directory..'../qsee-retained-controls.lua')(platform, 'tcsr',
        {0x01fc5040,0x01fc3040,0x01fc5050,0x01fc2050,0x01fc50d0,0x01fc50d4,0x01fc50d8,0x01fc50dc})
    -- TZ_QTI_Config RegisterSettings targets outside the SMMU windows, from 0x1571e348.
    -- The GPU writer at 0x156dacb0 follows them. See models/qcom/smmu/evidence.md.
    dofile(directory..'../qsee-retained-controls.lua')(platform, 'hsr', {
        0x151fc010,0x151fc030,0x151fc038,0x151fc040,0x151fc048,0x151fc050,0x151fc058,
        0x151fc060,0x151fc068,0x151fc080,0x151fc088,0x151fc090,0x151fc098,0x151fc0a0,
        0x151fc0a8,0x151fc0b0,0x151fc0b8,
        0x03de0010,0x03de0038,0x03de0040,0x03de8008,0x03de8020,
    })
    dofile(directory..'../qsee-retained-controls.lua')(platform, 'gmu',
        {0x03d60000,0x03d60004,0x03d60008,0x03d6000c})
    -- TZ AC root 0x1d policy-control words without an XBL owner.
    -- See models/qcom/sm88xx/tme_protection/tz-inventory-evidence.md.
    dofile(directory..'../qsee-retained-controls.lua')(platform, 'xpu',
        {0x03f24000,0x03f24004,0x09b2a000,0x01d10000,0x08b1f000})
    -- QSEE 0x1573f834 writes the MDSS software fuse words and checks bit zero.
    -- DT node mdss_mdp names this block swfuse_phys. See docs/qcom/sm88xx/stubs.md.
    dofile(directory..'../qsee-retained-controls.lua')(platform, 'mdss',
        {0x09aa0058,0x09aa0080,0x09aa0084,0x09aa0088})
    dofile(directory..'../qsee-retained-controls.lua')(platform, 'icb',
        dofile(directory..'qsee-icb.lua'))
    -- QSEE 0x1576d008 writes and verifies nine secure-camera protection words.
    -- The bank has no modeled consumer. See docs/qcom/sm88xx/boot-dependencies.md.
    dofile(directory..'../qsee-retained-controls.lua')(platform, 'camera',
        dofile(directory..'../qsee-camera-protection.lua'))
    dofile(directory..'../crypto-bam.lua')(platform)
    dofile(directory..'../ipa.lua')(platform)
    dofile(directory..'soccp.lua')(platform, {
        router='apss_router',memory_bind='&apss_router.target_socket',pll_path='gcc_plls',
        secure_irq_bind='&apss_gic.spi_in_229',
        tme_irq_bind='&tme_clic.source_38',
        irq_binds={'&apss_gic.spi_in_246','&apss_gic.spi_in_230','&apss_gic.spi_in_158',
            '&apss_gic.spi_in_715','&apss_gic.spi_in_70'},
    })
    dofile(directory..'rpmh.lua')(platform)
    dofile(directory..'cpucp-extended-reset.lua')(platform, 'below3')
    dofile(directory..'../dcp-runtime.lua')(platform,options,'sm8845')
    dofile(directory..'../dcp-bootstrap.lua')(platform)
    dofile(directory..'../qsee-fault-monitors.lua')(platform,'sm8845')
    dofile(directory..'../qsee-clock-dependencies.lua')(platform,'sm8845')
    dofile(directory..'../qsee-qup-common.lua')(platform,'sm8845')
    dofile(directory..'../dcp-clocks.lua')(platform,'sm8845')
    dofile(directory..'../qsee-gpce.lua')(platform)
    dofile(directory..'../tme-image-admission.lua')(platform, {
        current_image_context_address=0x448910,
    })
    dofile(directory..'../pdp-scmi.lua')(platform, {
        router='apss_router', observer_path='tme_image_admission', gic='apss_gic',
        cpu_domain='cpu_domains',
    })
    dofile(directory..'../aop-resource-service.lua')(platform, {
        profile='sm8845', observer_path='tme_image_admission',
    })
    dofile(directory..'../tme-power-state.lua')(platform)
    return platform
end
