-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Retained words for QSEE configuration writes that have no modeled consumer.
-- Each caller names the QSEE writer and the evidence document for its address list.
return function(p, label, addresses)
    assert(p.apss_router, 'The APSS router is required for QSEE retained controls.')
    assert(type(label)=='string' and label:match('^[a-z0-9]+$'), 'A QSEE control label is required.')
    assert(type(addresses)=='table' and #addresses>0, 'QSEE control addresses are required.')
    local bind='&apss_router.initiator_socket'
    for _,address in ipairs(addresses) do
        assert(type(address)=='number' and address%4==0, 'A QSEE control address is invalid.')
        local name=string.format('qsee_%s_control_%08x',label,address)
        assert(p[name]==nil, 'Duplicate QSEE control: '..name)
        for owner,module in pairs(p) do
            if type(module)=='table' then
                for _,target in pairs(module) do
                    if type(target)=='table' and target.bind==bind
                        and type(target.address)=='number' and type(target.size)=='number'
                        and address<target.address+target.size and target.address<address+4 then
                        error(string.format('QSEE control 0x%x overlaps %s',address,owner))
                    end
                end
            end
        end
        -- The word retains guest configuration. It does not report a hardware result.
        p[name]={moduletype='qcom,retained-boot-control-v1',writable_mask=0xffffffff,reset_value=0,
            target_socket={address=address,size=4,bind=bind}}
    end
    return p
end
