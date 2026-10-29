-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Selected UEFI/PDP evidence and virtual policy: models/qcom/sm88xx/pdp_scmi/evidence.md.
-- Apply after coherent DDR, the shared image observer, and the APSS GIC exist.
return function(p, o)
    assert(type(p)=='table' and type(o)=='table', 'PDP composition options are required.')
    local router=o.router or 'apss_router'
    local observer=o.observer_path or 'tme_image_admission'
    local gic=o.gic or 'apss_gic'
    assert(type(router)=='string' and p[router], 'The APSS memory router is required.')
    assert(type(observer)=='string' and p[observer] and
        p[observer].moduletype=='qcom,authenticated-image-handoff-v1',
        'The shared authenticated image observer is required.')
    assert(type(gic)=='string' and p[gic] and
        (p[gic].moduletype=='arm_gicv3' or p[gic].moduletype=='qcom,apss-gic-v1'),
        'The existing APSS GIC is required.')
    local additions={}
    local function insert(name, module)
        assert(not p[name] and not additions[name], 'Duplicate PDP module: '..name)
        additions[name]=module
    end
    local function overlapping(bus, address, size)
        local found
        for name,module in pairs(p) do
            if type(module)=='table' then
                for socket_name,socket in pairs(module) do
                    if type(socket)=='table' and socket.bind=='&'..bus..'.initiator_socket' and
                        type(socket.address)=='number' and type(socket.size)=='number' and
                        address<socket.address+socket.size and socket.address<address+size then
                        assert(not found and socket.address<=address and socket.address+socket.size>=address+size,
                            'Partial or duplicate PDP mapping: '..name..'.'..socket_name)
                        found={name=name,module=module,socket=socket,socket_name=socket_name}
                    end
                end
            end
        end
        return found
    end
    -- Both slots use the existing DDR bytes. Never add a RAM overlay or a CPUCP slot alias.
    local memory=overlapping(router,0x81f08100,0x100)
    assert(memory and (memory.module.moduletype=='gs_memory' or
        memory.module.moduletype=='qcom,shared-executable-memory-v1' or
        memory.module.moduletype=='qcom,llcc-dare-port-v1') and
        not memory.module.read_only and memory.socket.relative_addresses~=false,
        'The selected PDP slots require one existing writable RAM owner.')
    insert('pdp_scmi_memory_router',{moduletype='router'})
    insert('pdp_scmi_memory_alias',{moduletype='addrtr',
        mapped_base_addr=0x81f08100-memory.socket.address,
        target_socket={address=0x81f08100,size=0x100,relative_addresses=false,
            bind='&pdp_scmi_memory_router.initiator_socket'},
        initiator_socket={bind='&'..memory.name..'.'..memory.socket_name}})
    -- The CPU-domain publication is an explicit composition option. The runtime
    -- composition passes the domain name. A PDP-only fixture omits it.
    local cpu_domain=o.cpu_domain
    if cpu_domain then
        assert(type(cpu_domain)=='string' and p[cpu_domain] and
            p[cpu_domain].moduletype=='qcom,cpu-domain-power-v1',
            'The configured CPU domain is required for the boot status blocks.')
        local status_owners={}
        for lane=0,1 do
            local address=0x81f5a000+lane*0x40000
            local owner=overlapping(router,address,0x100)
            assert(owner and (owner.module.moduletype=='gs_memory' or
                owner.module.moduletype=='qcom,shared-executable-memory-v1' or
                owner.module.moduletype=='qcom,llcc-dare-port-v1') and
                not owner.module.read_only and owner.socket.relative_addresses~=false,
                'The APSS boot status block requires one existing writable RAM owner.')
            status_owners[lane]=owner
        end
        insert('apss_boot_status_router',{moduletype='router'})
        for lane=0,1 do
            local address=0x81f5a000+lane*0x40000
            local owner=status_owners[lane]
            insert('apss_boot_status_alias'..lane,{moduletype='addrtr',
                mapped_base_addr=address-owner.socket.address,
                target_socket={address=address,size=0x100,relative_addresses=false,
                    bind='&apss_boot_status_router.initiator_socket'},
                initiator_socket={bind='&'..owner.name..'.'..owner.socket_name}})
        end
        p[cpu_domain].boot_status_enable=true
        p[cpu_domain].boot_status_base=0x81f5a000
        p[cpu_domain].boot_status_socket={bind='&apss_boot_status_router.target_socket'}
    end
    local function map(name, address, size, module)
        assert(not overlapping(router,address,size), 'A PDP MMIO aperture already has an owner.')
        module.target_socket={address=address,size=size,bind='&'..router..'.initiator_socket'}
        insert(name,module)
    end
    map('pdp_mailbox_tx0',0x19980000,0x300,{moduletype='qcom,mailbox-v1'})
    map('pdp_mailbox_tx1',0x1a980000,0x300,{moduletype='qcom,mailbox-v1'})
    -- arm_gicv3 names SPIs by index. Proposed absolute INTID66 is SPI34, not SPI66.
    -- The selected UEFI framework's final translation remains unproved until native capture.
    local irq_bind='&'..gic..'.spi_in_34'
    map('pdp_mailbox_rx',0x17610000,0x4c08,{moduletype='qcom,mailbox-rx-bank-v1',
        irq={bind=irq_bind}})
    -- Live DT cpucp node: reg tx 0x18830000+0x300 and rx 0x17620000+0x4c08,
    -- interrupts <0 0x1c 4> = SPI 28. This is a second RX bank with its own
    -- pending, MAP, and enable state, not an alias of the PDP bank at 0x17610000.
    map('cpucp_mailbox_tx',0x18830000,0x300,{moduletype='qcom,mailbox-v1'})
    map('cpucp_mailbox_rx',0x17620000,0x4c08,{moduletype='qcom,mailbox-rx-bank-v1',
        irq={bind='&'..gic..'.spi_in_28'}})
    insert('pdp_scmi',{moduletype='qcom,pdp-scmi-virtual-v1',observer_path=observer,
        slot0=0x81f08100,slot1=0x81f08180,
        tx0_path='pdp_mailbox_tx0',tx1_path='pdp_mailbox_tx1',rx_path='pdp_mailbox_rx',
        memory_socket={bind='&pdp_scmi_memory_router.target_socket'}})
    for name,module in pairs(additions) do p[name]=module end
    return p,{endpoint='pdp_scmi',performance_resource='pdp_scmi',
        tx0='pdp_mailbox_tx0',tx1='pdp_mailbox_tx1',rx='pdp_mailbox_rx',
        memory_router='pdp_scmi_memory_router',irq_intid=66,irq_bind=irq_bind}
end
