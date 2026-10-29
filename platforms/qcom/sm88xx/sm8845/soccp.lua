-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Selected xbl_sc.elf a2011f59d74956ce3536c924ce9995dd0222b23492d713741b9397453540adc4.
-- See models/qcom/soccp/evidence.md. Apply after the APSS router, DDR, PLLs, and GIC exist.
return function(p, o)
    assert(type(p)=='table' and type(o)=='table', 'SOCCP wiring is required.')
    assert(type(o.router)=='string' and p[o.router], 'The APSS MMIO router is required.')
    assert(type(o.memory_bind)=='string', 'The shared APSS memory initiator binding is required.')
    assert(type(o.pll_path)=='string' and p[o.pll_path]
        and p[o.pll_path].moduletype=='qcom,gcc-taycan-eko-t-pll-v1',
        'The existing GCC PLL bank is required.')
    assert(type(o.irq_binds)=='table' and #o.irq_binds==5, 'Five IPCC IRQ bindings are required.')
    assert(type(o.secure_irq_bind)=='string', 'The secure IPCC IRQ binding is required.')
    assert(type(o.tme_irq_bind)=='string', 'The TME IPCC IRQ binding is required.')
    -- Selected rate table 0x148ef9a0 establishes selector1 = GPLL0 main.
    -- Resolve that connection from the existing peripheral root before adding any module.
    local parent
    for _,candidate in pairs(p) do
        if type(candidate)=='table' and candidate.moduletype=='qcom,rcg-dfs-v1'
            and candidate.target_socket and candidate.target_socket.address==0x118014 then
            assert(parent==nil, 'Duplicate selected peripheral clock root.')
            parent=candidate
        end
    end
    assert(parent and parent.pll_path==o.pll_path and parent.primary_selector==1
        and parent.primary_index==0, 'The selected GPLL0 main connection is required.')
    local additions={}
    local function insert(name, module)
        assert(p[name]==nil and additions[name]==nil, 'Duplicate SOCCP module: '..name)
        additions[name]=module
    end
    local function map(name, address, size, module, socket)
        socket=socket or 'target_socket'
        module[socket]={address=address,size=size,bind='&'..o.router..'.initiator_socket'}
        insert(name,module)
    end

    -- Deterministic virtual pack policy. No physical charger or fuel gauge is present.
    -- The pack is idle at 25 C. No external source, current, or charge transfer is claimed.
    insert('soccp',{
        moduletype='qcom,soccp-virtual-peer-v2',
        smem_base=0x81d00000,smem_size=0x200000,
        ipcc_path='soccp_ipcc_0',mutex_path='soccp_heap_mutex',clock_path='soccp_release_clock',
        voltage_mv=8800,design_mwh=11400,full_mwh=11400,remaining_mwh=5700,temperature_decic=250,
        memory_socket={bind=o.memory_bind},
        boot_socket={address=0x00d7000c,size=8,bind='&'..o.router..'.initiator_socket'},
        config_socket={address=0x00da2000,size=0x21c,bind='&'..o.router..'.initiator_socket'},
        dtb_socket={address=0x00da0048,size=4,bind='&'..o.router..'.initiator_socket'},
    })
    -- The selected QSEE uses the same TCSR bank and 0x1000-byte stride.
    -- SMEM lock3 keeps its shared owner. QSEE claims lock9 with owner8.
    for index=0,31 do
        local name=index==3 and 'soccp_heap_mutex' or 'tcsr_mutex_'..index
        map(name,0x01f40000+index*0x1000,4,{moduletype='qcom,smem-mutex-v1'})
    end

    -- Selected IPCCDxe BSP RVA0x7690, explicit virtual revision3 index layout.
    local banks={
        {0x01106000,278,8,6,18},
        {0x011c3000,262,32,3,17},
        {0x01283000,190,32,3,17},
        {0x01343000,747,32,3,11},
        {0x01403000,102,32,3,25},
    }
    for i,bank in ipairs(banks) do
        assert(type(o.irq_binds[i])=='string', 'Each IPCC IRQ needs an explicit binding.')
        local module={moduletype='qcom,ipcc-local-v3',protocol=i-1,signal_count=bank[3],
            local_index=bank[4],client_count=bank[5],irq={bind=o.irq_binds[i]}}
        if i==1 then
            module.peer_index=13; module.peer_path='soccp'; module.fabric_client_count=18
        end
        map('soccp_ipcc_'..(i-1),bank[1],0x1000,module)
    end
    -- QSEE descriptor0x157ebff8 selects protocol0/local ordinal1 and INTID261.
    map('secure_ipcc',0x01101000,0x1000,{
        moduletype='qcom,ipcc-local-v3',protocol=0,local_index=1,
        client_count=18,signal_count=8,fabric_path='soccp_ipcc_0',
        irq={bind=o.secure_irq_bind}})
    -- Both selected TME descriptors447b0c use local ordinal16 and CLIC source38.
    map('tme_ipcc',0x01110000,0x1000,{
        moduletype='qcom,ipcc-local-v3',protocol=0,local_index=16,
        client_count=18,signal_count=8,fabric_path='soccp_ipcc_0',
        irq={bind=o.tme_irq_bind}})

    -- XBL configure0x14898ff8 selects source1/HID1. Reuse GPLL0 main, never XO for source1.
    -- This is virtual parent wiring from the selected clock table, not a recovered physical netlist.
    map('soccp_clock_root',0x001a406c,0x14,{moduletype='qcom,rcg-v1',pll_path=parent.pll_path,
        crystal_selector=8,primary_selector=parent.primary_selector,primary_index=parent.primary_index,
        require_root_enable=false})
    -- Preserve established branch owners and aliases. Do not duplicate their state.
    for _,address in ipairs({0x001a400c,0x001a4004,0x001a4008,0x001a4040,0x001a402c,
            0x001a4054,0x001a4024,0x001a4010,0x001a4050}) do
        local existing
        for _,candidate in pairs(p) do
            if type(candidate)=='table' and candidate.moduletype=='qcom,clock-branch-v1' and
                candidate.target_socket and candidate.target_socket.address==address then
                assert(existing==nil, 'Duplicate established SOCCP clock branch.')
                existing=candidate
            end
        end
        if existing then
            assert(address~=0x001a402c, 'The SOCCP release branch already has another owner.')
        else
            local module={moduletype='qcom,clock-branch-v1',source_path='soccp_clock_root'}
            local name='soccp_branch_'..address
            -- XBL release0x148991dc pulses bit2. No physical CPU exists in this policy.
            if address==0x001a402c then module.retained_mask=4; name='soccp_release_clock' end
            map(name,address,4,module)
        end
    end

    -- No SMEM, image, ready flag, authentication result, or EFI result is supplied here.
    local function check_overlap(name, socket)
        for existing_name,module in pairs(p) do
            if type(module)=='table' then
                for _,existing in pairs(module) do
                    if type(existing)=='table' and existing.bind==socket.bind and
                        type(existing.address)=='number' and type(existing.size)=='number' and
                        socket.address<existing.address+existing.size and
                        existing.address<socket.address+socket.size then
                        error(string.format('SOCCP target %s overlaps %s at 0x%x',
                            name,existing_name,socket.address))
                    end
                end
            end
        end
    end
    for name,module in pairs(additions) do
        for _,socket in pairs(module) do
            if type(socket)=='table' and type(socket.address)=='number' then check_overlap(name,socket) end
        end
    end
    for name,module in pairs(additions) do p[name]=module end
    return p
end
