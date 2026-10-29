-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Selected OEM layouts and QBox interfaces: models/qcom/sm88xx/dcp/evidence.md.
local directory=assert(debug.getinfo(1,'S').source:match('^@(.*/)'))
return function(p, options, soc)
    assert(soc=='sm8850' or soc=='sm8845', 'The selected DCP image is required.')
    assert(p.qemu_inst_mgr and p.ddr_low, 'DCP requires the existing manager and protected DDR owner.')
    assert(not p.dcp, 'The native DCP CPU already exists.')
    assert(type(options.trace)=='string', 'The native DCP trace prefix is required.')

    p.dcp_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','RISCV32'},
        accel='tcg',tcg_mode='MULTI',sync_policy='multithread-quantum',
        qemu_args='-d in_asm,int,guest_errors,unimp -D '..options.trace..'-dcp.log'}
    p.dcp_router={moduletype='router'}
    -- RISC-V has one address space. This monitor does not assign APSS authority.
    p.dcp_access={moduletype='AccessMonitor',allow_unspecified=true,
        initiator_socket={bind='&dcp_router.target_socket'}}
    -- DcpCpu holds reset until bootstrap supplies the guest-selected entry.
    p.dcp={moduletype='qcom,dcp-rv32-v1',args={'&dcp_instance'},hartid=0,
        pmp=true,mmu=false,mem={bind='&dcp_access.target_socket'}}
    -- The DCP joins protected DDR as an execution participant. DARE grants still bound its DMI.
    p.ddr_low.execution_participant='dcp'
    p.ddr_low_backing.execution_participant='dcp'

    -- The DCP tree exposes the same DTB word and remappers as XBL's CSR banks.
    for index,view in ipairs({
        {0x01ea0048,0x09ca0048,4},
        {0x01ea2080,0x09ca2080,0x30},
        {0x01ea2280,0x09ca2280,0x30},
    }) do
        p['dcp_csr_view_'..index]={moduletype='addrtr',mapped_base_addr=view[2],
            target_socket={address=view[1],size=view[3],relative_addresses=false,
                bind='&dcp_router.initiator_socket'},
            initiator_socket={bind='&apss_router.target_socket'}}
    end
    -- SM8850 0xa00ea574..0xa00ea58c and SM8845 0xa4116722..0xa4116736 store the next
    -- Zephyr power-state code at CSR +0x40000: ACTIVE 0, RUNTIME_IDLE 6, SUSPEND_TO_RAM 0xf.
    -- The word retains that hint. It does not select or report a power transition.
    p.dcp_csr_power_state={moduletype='qcom,retained-boot-control-v1',writable_mask=0xf,reset_value=0,
        target_socket={address=0x01e80000,size=4,bind='&dcp_router.initiator_socket'}}

    -- FDT DTIM bounds the shared guest-loaded image and its writable runtime.
    -- Preserve the existing DARE gate. Never bind directly to ddr_low_backing.
    local base=soc=='sm8850' and 0xa00d0000 or 0xa4100000
    p.dcp_dtim={moduletype='addrtr',mapped_base_addr=base-0x80000000,
        target_socket={address=base,size=0x300000,relative_addresses=false,
            bind='&dcp_router.initiator_socket'},
        initiator_socket={bind='&ddr_low.target_socket'}}
    if soc=='sm8850' then
        -- Cold code a0124652 copies DDR a00d1000 to DLS 401000 itself.
        -- DLS is distinct storage, not a pre-populated alias of the load segment.
        -- QEMU requires page-aligned RAM mappings for direct memory access.
        p.dcp_dls_prefix={moduletype='gs_memory',init_mem=true,dmi_allow=false,
            target_socket={address=0x00400200,size=0xe00,
                bind='&dcp_router.initiator_socket'}}
        p.dcp_dls={moduletype='gs_memory',init_mem=true,
            target_socket={address=0x00401000,size=0x3f000,
                bind='&dcp_router.initiator_socket'}}
    end

    -- The OEM tree assigns these private frames to local IRQs 27, 12, and 13.
    p.dcp_system_timer={moduletype='qcom,sm8850-qtimer-v1',clock_hz=19200000,
        enabled_at_reset=true,frame_access_reset=0x3f,
        frame0_socket={address=0x00e01000,size=0x1000,bind='&dcp_router.initiator_socket'},
        frame1_socket={address=0x00e02000,size=0x1000,bind='&dcp_router.initiator_socket'},
        frame2_socket={address=0x00e03000,size=0x1000,bind='&dcp_router.initiator_socket'},
        irq_0={bind='&dcp.irq_in_27'},
        irq_1={bind='&dcp.irq_in_12'},irq_2={bind='&dcp.irq_in_13'}}

    -- Both trees declare qcom,sifive-beu at 0x5000. SiFive U54 21G2 section 11
    -- defines six eight-byte registers. No modeled bus or ECC error can occur,
    -- so cause, value, and accrued stay zero unless the guest writes them.
    p.dcp_bus_error_unit={moduletype='gs_memory',init_mem=true,dmi_allow=false,
        target_socket={address=0x00005000,size=0x30,bind='&dcp_router.initiator_socket'}}

    -- The upper banks retain power-management selection. They do not gate CPU delivery.
    -- No current consumer reads local observation state, so local sources stay unbound.
    p.dcp_interrupts={moduletype='qcom,dcp-interrupt-conditioning-v1',
        mem={address=0x00e10000,size=0x284,relative_addresses=false,
            bind='&dcp_router.initiator_socket'},
        auxiliary={address=0x00e11104,size=4,relative_addresses=false,
            bind='&dcp_router.initiator_socket'}}

    -- CLINT uses separate QBox software-interrupt and machine-timer owners.
    -- The SWI component connects MSIP to hart zero internally.
    p.dcp_software_irq={moduletype='riscv_aclint_swi',args={'&dcp_instance','&dcp'},
        construction_priority=1,num_harts=1,
        mem={address=0x00100000,size=0x4000,bind='&dcp_router.initiator_socket'}}
    p.dcp_machine_timer={moduletype='riscv_aclint_mtimer',args={'&dcp_instance'},
        construction_priority=1,hartid_base=0,num_harts=1,
        timecmp_base=0,time_base=0x7ff8,aperture_size=0xc000,provide_rdtime=false,
        mem={address=0x00104000,size=0xc000,bind='&dcp_router.initiator_socket'},
        timer_irq_0={bind='&dcp.irq_in_7'}}

    -- The three contiguous FDT ranges describe this single PLIC owner.
    -- QEMU connects its M context to hart zero's machine-external IRQ internally.
    p.dcp_plic={moduletype='plic_sifive',args={'&dcp_instance'},
        construction_priority=1,num_sources=53,num_priorities=7,
        priority_base=0,pending_base=0x1000,enable_base=0x2000,enable_stride=0x80,
        context_base=0x200000,context_stride=0x1000,aperture_size=0x400000,hart_config='M',
        mem={address=0x00800000,size=0x400000,bind='&dcp_router.initiator_socket'}}

    -- Each DCP tree declares four IPCC protocol views and their PLIC sources.
    -- Window one maps DCP 0x41xxxxxx to system 0x01xxxxxx. The view address selects
    -- the local client, and the client-mapping length gives the client count.
    local mproc_clients=soc=='sm8850' and 17 or 18
    for _,view in ipairs({
        {0,0x0110e000,14,mproc_clients,8,4},
        {1,0x011cb000,11,16,32,2},
        {2,0x0128b000,11,16,32,3},
        {4,0x0140b000,11,25,32,1},
    }) do
        local fabric='soccp_ipcc_'..view[1]
        assert(p[fabric], 'The DCP IPCC view requires protocol fabric '..fabric..'.')
        p['dcp_ipcc_'..view[1]]={moduletype='qcom,ipcc-local-v3',protocol=view[1],
            local_index=view[3],client_count=view[4],signal_count=view[5],fabric_path=fabric,
            irq={bind='&dcp_plic.irq_in_'..view[6]},
            target_socket={address=view[2],size=0x1000,bind='&apss_router.initiator_socket'}}
    end

    -- DCP 0x10000000 reaches the display subsystem at system 0x09800000.
    -- DCP 0x103b4030..0x103b4040 receive the same control writes as XBL's 0x09bb4030..0x09bb4040.
    p.dcp_display_window={moduletype='addrtr',mapped_base_addr=0x09800000,
        target_socket={address=0x10000000,size=0x400000,relative_addresses=false,
            bind='&dcp_router.initiator_socket'},
        initiator_socket={bind='&apss_router.target_socket'}}
    dofile(directory..'dcp-display.lua')(p, soc)
    -- The tree's power_rpmh node declares DRV0 at DCP 0x103c2000: global DRV 10,
    -- three TCSs with one active TCS, twelve commands, and PLIC source 32.
    -- Votes join the shared AOP resource owner as requester one.
    assert(p.rpmh_rsc, 'The DCP RSC requires the shared RPMh resource owner.')
    -- SM8850 0xa00e8b48 clears TCS IRQs at +0x528, 0xa00e8b94 enables them at +0x520,
    -- and 0xa00e8b7a sets AMC mode at +0x544. The common TCS block starts at +0x520.
    -- 0xa00e8b16 and 0xa00e97a0 use a 0x150-byte TCS stride and 0x18-byte commands.
    p.dcp_rpmh_rsc={moduletype='qcom,display-rsc-v1',banks='0 3 1',commands=12,
        tcs_offset=0x520,tcs_stride=0x150,
        requester_base=1,resource_owner_path='rpmh_rsc',
        irq_0={bind='&dcp_plic.irq_in_32'},
        target_socket={address=0x09bc2000,size=0x2000,bind='&apss_router.initiator_socket'}}
    -- Global DRV 10 selects the display PDC DRV at system 0x0b2a0000.
    -- rpmh_pdc_init reads its TCS commands through the 0x0b5f9810 table.
    p.dcp_display_pdc={moduletype='qcom,display-pdc-v1',
        target_socket={address=0x0b2a0000,size=0x6000,bind='&apss_router.initiator_socket'},
        command_socket={address=0x0b5f9810,size=0xbb8,bind='&apss_router.initiator_socket'}}
    -- The DCP config row '+RPMH_BCM_BCM_STATUS' maps system 0x0bbf0000. ICB reads its
    -- version word and per-VCD status. Resource slots map to the cmd-db VCD numbers.
    local vcds={0,1,1,1,3,3,3,3,3,3,4,4,5,5,6,7,8,8,8,9,10,11,12,0,8,8,13,0,1,0,1,0}
    local rows={}
    for slot,vcd in ipairs(vcds) do
        if soc=='sm8850' or (slot-1~=21 and slot-1~=25) then
            rows[#rows+1]=string.format('0x%x %d',0x50000+4*(slot-1),vcd)
        end
    end
    p.dcp_bcm_status={moduletype='qcom,bcm-status-v1',resource_owner_path='rpmh_rsc',
        resource_vcds=table.concat(rows,';'),
        target_socket={address=0x0bbf0000,size=0x1f00,bind='&apss_router.initiator_socket'}}
    -- The tree's five SDE CESTA banks, seen by the DCP through the display window.
    -- The lists name the registers that the selected DCP program writes.
    local cesta={moduletype='qcom,cesta-v1',
        crmb_registers='0 0x68:12',
        crmb_pt_registers='4:5:0x14 8:5:0x14 0x10:5:0x14',
        crmc_registers='0xc 0x28 0x30 0x48:2 0x60:2 0xb4:2 0xc4:3 0xd8:7 0x100 0x108:5 '..
            '0x128 0x130:5 0x150 0x158:5 0x178 0x180:5 0x1a0 0x1a8:5 0x1c8 0x1d0:5 0x1f0 '..
            '0x1f8:5 0x218 0x220:5 0x240 0x248:5 0x268 0x274 0x28c:2 0x298:3 0x2c8:2 0x340 '..
            '0x350:3 0x368:2 0x378:3 0x390:2 0x3a0:3 0x3b8:2 0x3c8:3 0x3e0:2 0x3f0:3 0x408:2 '..
            '0x418:3 0x430:2 0x440:3 0x458:2 0x468:3 0x480:2 0x490:3 0x4a8:2 0x4b8:3 '..
            '0x4ec:27 0x56c:98',
        crmv_registers='0:5:0x40 0x38:5:0x40',
        common_registers='0xc'}
    for _,bank in ipairs({
        {'crmb_socket',0x09bcc000,0x400},{'crmb_pt_socket',0x09bcc400,0x400},
        {'crmc_socket',0x09bcc800,0x2000},{'crmv_socket',0x09bce800,0x600},
        {'common_socket',0x09bcee00,0x200},
    }) do
        cesta[bank[1]]={address=bank[2],size=bank[3],bind='&apss_router.initiator_socket'}
    end
    p.dcp_display_cesta=cesta
    -- SM8850 0xa00ef798..0xa00ef7ba writes a timeout to 0xe07008 and enables 0xe07000.
    -- 0xa00ef778 and 0xa00ef7d8 pet through 0xe05000, and the handler uses local IRQ 30.
    -- The trees name no counter clock or expiry action, so this watchdog never expires.
    p.dcp_watchdog_pet={moduletype='gs_memory',init_mem=true,dmi_allow=false,
        target_socket={address=0x00e05000,size=4,bind='&dcp_router.initiator_socket'}}
    p.dcp_watchdog_control={moduletype='gs_memory',init_mem=true,dmi_allow=false,
        target_socket={address=0x00e07000,size=0xc,bind='&dcp_router.initiator_socket'}}
    -- Remapped DCP system accesses keep the protected SPMI XPU hole on the APSS path.
    -- The rest of the arbiter uses its EE 11 endpoint. See models/qcom/spmi/dcp-evidence.md.
    assert(p.xbl_spmi, 'The DCP SPMI view requires the existing arbiter.')
    p.dcp_system_router={moduletype='router'}
    p.dcp_spmi_router={moduletype='router'}
    p.xbl_spmi.dcp_socket={address=0,size=0x900000,bind='&dcp_spmi_router.initiator_socket'}
    for index,range in ipairs({{0,0x0c400000},{0x0c41c000,0x2000},{0x0cd00000,0xf3300000}}) do
        p['dcp_system_view_'..index]={moduletype='addrtr',mapped_base_addr=range[1],
            target_socket={address=range[1],size=range[2],relative_addresses=false,
                bind='&dcp_system_router.initiator_socket'},
            initiator_socket={bind='&apss_router.target_socket'}}
    end
    for index,range in ipairs({{0,0x1c000},{0x1e000,0x8e2000}}) do
        p['dcp_spmi_view_'..index]={moduletype='addrtr',mapped_base_addr=range[1],
            target_socket={address=0x0c400000+range[1],size=range[2],relative_addresses=false,
                bind='&dcp_system_router.initiator_socket'},
            initiator_socket={bind='&dcp_spmi_router.target_socket'}}
    end
    -- No fallback bus or broad MMIO RAM hides missing peripheral owners.
    return p
end
