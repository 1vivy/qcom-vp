-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Install only after the base platform creates the sole mailbox backing and aliases.
return function(platform, options)
    options=options or {}
    local context=options.current_image_context_address or 0
    if context==0 then return platform, nil end
    assert(type(context)=='number' and context%4==0 and context>0 and context<=0xfffffffc,
        'The image context must be an aligned TME word address.')
    assert(platform.tme_image_admission==nil, 'Image admission is already configured.')
    local mailbox=assert(platform.tme_mailbox, 'The base mailbox is required.')
    assert(mailbox.moduletype=='gs_memory', 'Image admission requires the original gs_memory backing.')
    local mapping=assert(mailbox.target_socket, 'The mailbox mapping is required.')
    local router=options.router or 'tme_router'
    assert(mapping.address==0x02000000 and mapping.size==0x3800 and
        mapping.bind=='&'..router..'.initiator_socket' and mapping.relative_addresses~=false,
        'Image admission requires the original relative TME mailbox aperture.')
    assert(platform.tme_boot_status and platform.tme_boot_status.mailbox_socket and
        platform.tme_boot_status.mailbox_socket.bind=='&tme_mailbox.target_socket',
        'The boot-status reader must retain its original backing binding.')
    -- Move the routing descriptor, not the memory. Existing aliases still reach this aperture.
    platform.tme_image_admission={moduletype='qcom,authenticated-image-handoff-v1',
        current_image_context_address=context,
        target_socket=mapping,
        backing_socket={bind='&tme_mailbox.target_socket'},
        memory_socket={bind='&'..router..'.target_socket'}}
    mailbox.target_socket={address=0,size=0x3800}
    mailbox.dmi_allow=false
    return platform, 'tme_image_admission'
end
