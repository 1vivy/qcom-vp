-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Firmware consumers and functional policy: models/qcom/sm88xx/pdp/evidence.md.
return function(p, o)
    assert(p.qemu_inst_mgr and p.cpu_domains and p.ddr_low,
        'PDP execution requires the existing manager, CPU domain, and protected DDR.')
    assert(type(o.trace)=='string', 'PDP execution requires a trace prefix.')
    -- Priority one lets the PDP-private blocks inside a window take precedence.
    local function alias(name, bus, address, size, owner, base, priority)
        assert(not p[name], 'The PDP alias already exists: '..name)
        p[name]={moduletype='addrtr',mapped_base_addr=base or 0,
            target_socket={address=address,size=size,relative_addresses=false,
                priority=priority,bind='&'..bus..'.initiator_socket'},
            initiator_socket={bind='&'..owner}}
    end
    -- The CPUCP image loader fills both private SRAM owners through ACEHR.
    if not p.pdp1_ram then
        p.pdp1_ram={moduletype='gs_memory',init_mem=true,
            target_socket={address=0x1a930000,size=0x20000,
                bind='&cpucp_router.initiator_socket'}}
    end
    -- PDP 0x20005744 forms its configuration speed bin from corrected words
    -- 0x221c20d0[31:28] and 0x221c20d4[0]. 0x81324cb8 reads IDDQ fields at 0x221c20d8.
    -- The profile declares zero. This is virtual fuse policy, not a phone measurement.
    -- Zero IDDQ fields select the K_LKG_DEFAULT_VALUE configuration in 0x20016c44.
    assert(not p.pdp_pte_speed_bin, 'The PTE speed-bin row already has an owner.')
    p.pdp_pte_speed_bin={moduletype='gs_memory',init_mem=true,read_only=true,
        target_socket={address=0x221c20d0,size=0xc,bind='&apss_router.initiator_socket'}}
    -- PDP 0x81328f6a tests bit 8 of corrected word 0x221c24c8. Its meaning is open.
    p.pdp_corrected_24c8={moduletype='gs_memory',init_mem=true,read_only=true,
        target_socket={address=0x221c24c8,size=4,bind='&apss_router.initiator_socket'}}
    -- PDP 0x81324d64 programs IDDQ configuration for both rails. No consumer reads
    -- the values back, so the words retain guest writes without an effect.
    for _,address in ipairs({0x17913004,0x17913008,0x1791300c,
        0x17913104,0x17913108,0x1791310c,0x17915000}) do
        p['pdp_iddq_'..string.format('%08x',address)]={moduletype='qcom,retained-boot-control-v1',
            writable_mask=0xffffffff,reset_value=0,
            target_socket={address=address,size=4,bind='&apss_router.initiator_socket'}}
    end
    -- PDP code executes from protected DDR at 0x81320000. Each PDP instance joins
    -- the DDR owner as an execution participant, so it receives native RAM views
    -- and observes other writers. Without them, a Thumb instruction that crosses
    -- an M-profile 1 KiB page aborts QEMU (run573, PC 0x8132a3fe).
    for _,owner in ipairs({'ddr_low','ddr_low_backing'}) do
        local current=p[owner].execution_participant
        p[owner].execution_participant=(current and current~='' and current..' ' or '')..'pdp0 pdp1'
    end
    for lane=0,1 do
        local name='pdp'..lane
        assert(not p[name], 'The PDP CPU already exists.')
        p[name..'_instance']={moduletype='QemuInstance',args={'&qemu_inst_mgr','AARCH64'},
            accel='tcg',tcg_mode='MULTI',sync_policy='multithread-quantum',
            qemu_args='-d in_asm,int,guest_errors,unimp -D '..o.trace..'-'..name..'.log'}
        p[name..'_router']={moduletype='router'}
        p[name..'_access']={moduletype='AccessMonitor',allow_unspecified=false,
            initiator_socket={bind='&'..name..'_router.target_socket'}}
        -- PDP 0x20001fa4 asserts unless MPU_TYPE.DREGION is at least 13.
        -- Cortex-M7 offers an eight- or sixteen-region MPU configuration.
        p[name]={moduletype='qcom,pdp-cpu-v1',args={'&'..name..'_instance'},
            init_nsvtor=0x20000000,num_irq=90,pmsav7_dregion=16,
            clock_hz=19200000,reference_clock_hz=19200000,
            mem={bind='&'..name..'_access.target_socket'}}
        p.cpu_domains['pdp_reset'..lane]={bind='&'..name..'.reset'}
        p.cpu_domains['pdp_vector'..lane]={bind='&'..name..'.reset_vector'}
        alias(name..'_sram',name..'_router',0x20000000,0x20000,name..'_ram.target_socket')
        -- PDP local 0x40800000..0x40afffff is its NCC register space at system
        -- 0x19800000 + lane<<24. The cluster bank at +0x98000, the windows inventory
        -- at +0x190100, and the NCC bank table at 0x2000d1ec establish the rule.
        -- Only the existing exact-offset owners answer. Other offsets fail.
        alias(name..'_ncc',name..'_router',0x40800000,0x300000,'apss_router.target_socket',
            0x19800000+lane*0x01000000,1)
        -- PDP local 0x40000000..0x407fffff is the per-core NCC space at system
        -- 0x19000000 + lane<<24. NCC_CORE_DPMPPT at APSS_HM+0x020b0000 and the
        -- core DPM table at 0x2000d268 (local 0x400b0000 + core<<20) establish the rule.
        alias(name..'_core_ncc',name..'_router',0x40000000,0x800000,'apss_router.target_socket',
            0x19000000+lane*0x01000000,1)
        -- The PDP also uses system addresses directly, for example AUX PLL 0x19aa0000.
        alias(name..'_system',name..'_router',0,0x20000000,'apss_router.target_socket',0,1)
        -- The fuse page and the corrected rows use the existing APSS views.
        alias(name..'_fuse_space',name..'_router',0x22000000,0x01000000,'apss_router.target_socket',
            0x22000000,1)
        p[name..'_windows']={moduletype='qcom,pdp-address-windows-v1',
            registers={address=0x40990000,size=0x1d0c,bind='&'..name..'_router.initiator_socket'},
            memory={address=0x80000000,size=0x80000000,relative_addresses=false,
                bind='&'..name..'_router.initiator_socket'},
            output={bind='&'..name..'_system_router.target_socket'}}
        -- Device pdp_wdog@409c0000 uses driver wdt_bsa. Its bark handler is IRQ 6.
        -- WS0 interrupts the PDP. A second expiry stops the simulation.
        p[name..'_watchdog']={moduletype='arm,bsa-generic-watchdog-v1',clock_hz=19200000,
            refresh_socket={address=0x409c0000,size=0x1000,bind='&'..name..'_router.initiator_socket'},
            control_socket={address=0x409c1000,size=0x1000,bind='&'..name..'_router.initiator_socket'},
            ws0={bind='&'..name..'.irq_in_6'}}
        p[name..'_system_router']={moduletype='router'}
        alias(name..'_ddr',name..'_system_router',0x80000000,0x80000000,'ddr_low.target_socket')
        p[name..'_timer']={moduletype='qcom,ncc-timer-v1',frequency_hz=19200000,
            target_socket={address=0x408d0000,size=0x40,bind='&'..name..'_router.initiator_socket'}}
        -- The shared NCC window gives the PDP these system owners.
        -- Only recovered configuration words answer. No limit event comes from a write.
        p[name..'_dpm']={moduletype='qcom,pdp-dpm-v1',
            target_socket={address=0x198c0000+lane*0x01000000,size=0xc0,
                bind='&apss_router.initiator_socket'}}
        -- Function 0x81326c40 programs the weights of each present core.
        for core=0,(lane==0 and 5 or 1) do
            p[name..'_core'..core..'_dpm']={moduletype='qcom,pdp-core-dpm-v1',
                target_socket={address=0x190b0000+core*0x100000+lane*0x01000000,size=0x178,
                    bind='&apss_router.initiator_socket'}}
        end
        p[name..'_limiters']={moduletype='qcom,pdp-limiter-v1',
            target_socket={address=0x19a00000+lane*0x01000000,size=0x1d8,
                bind='&apss_router.initiator_socket'}}
        -- The ACD calibration table at link 0x8131d0c8 lists two instances: the NCC ACD at
        -- local 0x409d0000 (type 0) and an auxiliary ACD at 0x40aa2000 (type 1).
        -- Both use the same register layout. See models/qcom/sm88xx/pdp/acd_evidence.md.
        for _,acd in ipairs({{'acd',0x199d0000},{'aux_acd',0x19aa2000}}) do
            p[name..'_'..acd[1]]={moduletype='qcom,ncc-acd-v1',
                target_socket={address=acd[2]+lane*0x01000000,size=0x800,
                    bind='&apss_router.initiator_socket'}}
        end
    end
    -- Only guest PDP code publishes SCMI FREE words and the CPU boot status.
    p.cpu_domains.boot_status_enable=false
    p.pdp_apss_mailbox={moduletype='qcom,cpucp-mailbox-v1',
        peer_path='pdp0_apns_mailbox',auxiliary_peer_path='pdp1_apns_mailbox',
        tx_socket={address=0x19980000,size=0x300,bind='&apss_router.initiator_socket'},
        auxiliary_tx_socket={address=0x1a980000,size=0x300,bind='&apss_router.initiator_socket'},
        rx_socket={address=0x17610000,size=0x4c08,bind='&apss_router.initiator_socket'},
        irq={bind='&apss_gic.spi_in_34'}}
    p.cpucp_mailbox_pdp.peer_path='pdp0_cpucp_mailbox'
    p.cpucp_mailbox_pdp.auxiliary_peer_path='pdp1_cpucp_mailbox'
    for lane=0,1 do
        local name='pdp'..lane
        local bus='&'..name..'_router.initiator_socket'
        p[name..'_apns_mailbox']={moduletype='qcom,cpucp-mailbox-v1',
            peer_path='pdp_apss_mailbox',irq={bind='&'..name..'.irq_in_3'},
            rx_socket={address=0x40970000,size=0x4c08,bind=bus},
            tx_socket={address=0x17630000,size=0x300,bind=bus}}
        p[name..'_cpucp_mailbox']={moduletype='qcom,cpucp-mailbox-v1',
            peer_path='cpucp_mailbox_pdp',irq={bind='&'..name..'.irq_in_1'},
            rx_socket={address=0x40950000,size=0x4c08,bind=bus},
            tx_socket={address=0x18818000,size=0x300,bind=bus}}
        -- The third device has no recovered peer. Its sends must fail explicitly.
        p[name..'_aux_mailbox']={moduletype='qcom,cpucp-mailbox-v1',
            irq={bind='&'..name..'.irq_in_2'},
            rx_socket={address=0x40978000,size=0x4c08,bind=bus},
            tx_socket={address=0x17638000,size=0x300,bind=bus}}
    end
    -- This CPUCP bank is independent of the PDP bank and uses SPI 28.
    p.cpucp_apss_mailbox={moduletype='qcom,cpucp-mailbox-v1',peer_path='cpucp_mailbox_ns',
        irq={bind='&apss_gic.spi_in_28'},
        tx_socket={address=0x18830000,size=0x300,bind='&apss_router.initiator_socket'},
        rx_socket={address=0x17620000,size=0x4c08,bind='&apss_router.initiator_socket'}}
    p.cpucp_mailbox_ns.peer_path='cpucp_apss_mailbox'
    return p
end
