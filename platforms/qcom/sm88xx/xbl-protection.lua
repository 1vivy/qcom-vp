-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Board cohorts identify controllers, not physical client apertures.
return function(platform, cohort, substitutes)
    assert(platform.runtime_abi and platform.apss_router and platform.tme_router and platform.sequencer_dma_router,
        'XBL protection requires the existing reconstructed runtime platform.')
    assert(not platform.xbl_protection_1, 'XBL protection is already configured.')
    substitutes=substitutes or {}
    platform.xbl_protection_configuration={moduletype='router'}
    local controllers = {}
    local dma_targets = {}
    local kinds = {
        ['qcom,sm8850-tme-protection']=true,
        ['qcom,xpu-fixed-resources-v1']=true}
    for name,module in pairs(platform) do
        if type(module)=='table' then
            local socket=module.target_socket
            if kinds[module.moduletype] then
                assert(socket and socket.bind=='&tme_router.initiator_socket',
                    'Protection controller requires the shared TME configuration bus.')
                assert(not controllers[socket.address], 'Duplicate protection controller state.')
                controllers[socket.address]={name=name,module=module}
            end
            if socket and socket.bind=='&sequencer_dma_router.initiator_socket' then
                assert(not dma_targets[socket.address], 'Duplicate sequencer DMA target.')
                dma_targets[socket.address]=module
            end
        end
    end
    local function local_address(address)
        if address<0x1ef00000 then return address+0x04000000 end
        if address>=0x20000000 and address<0x2230c000 then return address-0x20000000 end
        return address
    end
    -- SPMI does not decode this XPU range. Keep one arbiter behind two views.
    local spmi=platform.xbl_spmi
    assert(spmi and spmi.moduletype=='qcom,spmi-arbiter-packed-v1',
        'XBL protection requires the existing packed SPMI arbiter.')
    local spmi_socket=spmi.target_socket
    assert(spmi_socket and spmi_socket.address==0x0c400000
        and spmi_socket.size==0x00900000
        and spmi_socket.bind=='&apss_router.initiator_socket',
        'Unexpected packed SPMI configuration aperture.')
    local spmi_xpu=controllers[local_address(0x0c41c000)]
    assert(spmi_xpu and spmi_xpu.module.target_socket.size==0x2000,
        'Unexpected SPMI XPU geometry.')
    spmi.target_socket=nil
    for index,range in ipairs({{0,0x1c000},{0x1e000,0x00900000-0x1e000}}) do
        platform['xbl_spmi_configuration_'..index]={
            moduletype='addrtr',mapped_base_addr=range[1],
            target_socket={address=spmi_socket.address+range[1],size=range[2],
                relative_addresses=false,bind='&apss_router.initiator_socket'},
            initiator_socket={bind='&xbl_spmi.target_socket'}}
    end
    local function no_overlap(router,address,size)
        for name,module in pairs(platform) do
            if type(module)=='table' then
                for socket_name,socket in pairs(module) do
                    if type(socket)=='table' and socket.bind=='&'..router..'.initiator_socket'
                        and socket.address and socket.size then
                        assert(socket.address>=address+size or address>=socket.address+socket.size,
                            string.format('XBL protection [%#x,%#x) overlaps %s.%s [%#x,%#x) on %s.',
                                address,address+size,tostring(name),tostring(socket_name),
                                socket.address,socket.address+socket.size,router))
                    end
                end
            end
        end
    end
    local identifiers, addresses = {}, {}
    for index,entry in ipairs(cohort) do
        local address=entry[2]
        assert(not identifiers[entry[1]] and not addresses[address],
            'Duplicate XBL resource identifier or controller address.')
        identifiers[entry[1]],addresses[address]=true,true
        local mapped=local_address(address)
        local controller=controllers[mapped]
        if not controller then
            local substitute=assert(substitutes[address],
                'Missing shared runtime protection controller.')
            -- Explicit virtual capacity, not measured silicon geometry.
            -- No identified virtual data client uses these substitute controllers.
            local name='xbl_virtual_protection_'..entry[1]
            no_overlap('tme_router',mapped,0x11000)
            platform[name]={moduletype=substitute.fixed and 'qcom,xpu-fixed-resources-v1'
                    or 'qcom,sm8850-tme-protection',
                region_count=1024,address_bits=40,alignment_bits=0,
                qad_count=11,revision=0x40000000,
                config_owner_mask=4,config_secure_unspecified=true,
                target_socket={address=mapped,size=0x11000,bind='&tme_router.initiator_socket'}}
            controller={name=name,module=platform[name]}
            controllers[mapped]=controller
        end
        local size=controller.module.target_socket.size
        assert(not controller.module.apss_config_socket,
            'Protection controller already has an APSS configuration view.')
        controller.module.apss_config_owner_mask=0xc0000001
        controller.module.apss_config_socket={address=mapped,size=size,
            bind='&xbl_protection_configuration.initiator_socket'}
        no_overlap('apss_router',address,size)
        -- One controller owns the region array, locks, and permission-change event.
        platform['xbl_protection_'..index]={moduletype='addrtr',mapped_base_addr=mapped,
            target_socket={address=address,size=size,relative_addresses=false,
                bind='&apss_router.initiator_socket'},
            initiator_socket={bind='&xbl_protection_configuration.target_socket'}}
        -- Command 0x25 uses system DBGAR addresses without APSS configuration authority.
        -- Evidence: models/qcom/sm88xx/tme_protection/namespace-platform-evidence.md.
        local dma_address=address+0x304
        local dma=dma_targets[dma_address]
        if dma then
            assert(dma.moduletype=='addrtr' and dma.mapped_base_addr==mapped+0x304
                and dma.target_socket.size==4 and dma.target_socket.relative_addresses==false
                and dma.initiator_socket and dma.initiator_socket.bind=='&tme_router.target_socket',
                'Unexpected sequencer DBGAR owner.')
        else
            no_overlap('sequencer_dma_router',dma_address,4)
            dma={moduletype='addrtr',mapped_base_addr=mapped+0x304,
                target_socket={address=dma_address,size=4,relative_addresses=false,
                    bind='&sequencer_dma_router.initiator_socket'},
                initiator_socket={bind='&tme_router.target_socket'}}
            platform['xbl_protection_debug_dma_'..index]=dma
            dma_targets[dma_address]=dma
        end
    end
    return platform
end
