-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Selected CPH2749/CPH2771 AC policy: models/qcom/sm88xx/boot_control/evidence.md.
return function(platform)
    assert(platform.runtime_abi and platform.xbl_protection_1,
        'XBL boot policy requires the reconstructed runtime and XBL protection aliases.')
    assert(not platform.xbl_boot_mode, 'XBL boot policy is already configured.')
    local function no_overlap(address)
        for name,module in pairs(platform) do
            if type(module)=='table' then
                for socket_name,socket in pairs(module) do
                    if type(socket)=='table' and socket.bind=='&apss_router.initiator_socket'
                        and socket.address and socket.size then
                        assert(socket.address>=address+4 or address>=socket.address+socket.size,
                            string.format('XBL boot policy %#x overlaps %s.%s.',
                                address,tostring(name),tostring(socket_name)))
                    end
                end
            end
        end
    end
    local kinds={['qcom,sm8850-tme-protection']=true,['qcom,xpu-fixed-resources-v1']=true}
    local controllers={}
    for name,module in pairs(platform) do
        if type(module)=='table' and kinds[module.moduletype] then
            local socket=module.target_socket
            assert(socket and socket.bind=='&tme_router.initiator_socket',
                'XBL boot policy requires shared TME configuration owners.')
            assert(not controllers[socket.address], 'Duplicate shared protection owner.')
            controllers[socket.address]={name=name,module=module}
        end
    end
    local owner=assert(controllers[0x05f00000], 'The TCSR protection owner is required.').module
    local mode=owner.mode_socket
    assert(mode and mode.address==0x05fd0000 and mode.size==4
        and mode.bind=='&tme_router.initiator_socket', 'Unexpected shared TCSR DCR view.')
    -- ROM 0x20d0ca writes 0x0a here. APSS must see that write, not another cold word.
    no_overlap(0x01fd0000)
    platform.xbl_boot_mode={moduletype='addrtr',mapped_base_addr=0x05fd0000,
        target_socket={address=0x01fd0000,size=4,relative_addresses=false,
            bind='&apss_router.initiator_socket'},
        initiator_socket={bind='&tme_router.target_socket'}}
    -- AC table resources 0x84, 0x96, 0xb5 identify these existing region owners.
    -- Only these four-byte control words are added. No permission state is copied.
    for _,entry in ipairs({{0x01de4000,0x05de6000},
            {0x07f08000,0x0bf30000},{0x17d00008,0x1be08000}}) do
        local controller=assert(controllers[entry[2]], 'Missing AC policy controller.').module
        assert(not controller.policy_control_socket, 'AC policy control already has a view.')
        no_overlap(entry[1])
        controller.policy_control_reset=0 -- Declared cold state, not physical reset evidence.
        controller.policy_control_socket={address=entry[1],size=4,
            bind='&apss_router.initiator_socket'}
    end
    return platform
end
