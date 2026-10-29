-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Exact DDR-stage policy words. No traffic timing or completion state is synthesized.
return function(p, records)
    assert(p.apss_router, 'The APSS router is required for DDR interconnect controls.')
    local words = {}
    for _, record in ipairs(records) do
        local address, operation, field_mask, register_mask = table.unpack(record)
        assert(operation == 2 or operation == 5 or operation == 8,
            'Unsupported DDR interconnect operation.')
        -- Type 5 reads an existing producer. Types 2 and 8 write guest-controlled fields.
        if operation ~= 5 then
            words[address] = (words[address] or 0) | (field_mask & register_mask)
        end
    end
    -- SM8850 0x148ab498 and SM8845 0x148a9b50 read bit zero before clock gating.
    -- Unwritten controls retain cold zero. These words do not report completion.
    for _, address in ipairs({
        0x31211028,0x31211050,0x31211078,0x312110a8,0x312110d0,
        0x312110f8,0x31211120,0x31211148,0x31211170,0x31211198,
        0x312111c0,0x31211208,0x31211230,0x31211258,0x31211280,
    }) do
        words[address] = (words[address] or 0) | 1
    end
    local function owner(address)
        local found
        for name, module in pairs(p) do
            if type(module) == 'table' then
                for _, socket in pairs(module) do
                    if type(socket) == 'table' and socket.bind == '&apss_router.initiator_socket'
                        and type(socket.address) == 'number' and type(socket.size) == 'number'
                        and address < socket.address + socket.size and socket.address < address + 4 then
                        assert(socket.address <= address and address + 4 <= socket.address + socket.size,
                            'Partial DDR interconnect overlap: '..name)
                        assert(found == nil, 'Multiple DDR interconnect owners at '..address)
                        found = name
                    end
                end
            end
        end
        return found
    end
    for address, mask in pairs(words) do
        if not owner(address) then
            local name = 'xbl_ddr_interconnect_'..address
            assert(p[name] == nil, 'Duplicate DDR interconnect control: '..name)
            p[name] = {moduletype='qcom,retained-boot-control-v1',
                writable_mask=mask, reset_value=0,
                target_socket={address=address, size=4, bind='&apss_router.initiator_socket'}}
        end
    end
    -- The read producer must remain owned. Do not replace absent producer state with zero.
    for _, record in ipairs(records) do
        if record[2] == 5 then
            assert(owner(record[1]), 'The DDR interconnect source requires an existing owner.')
        end
    end
    return p
end
