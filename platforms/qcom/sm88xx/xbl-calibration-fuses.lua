-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Virtual CPR plan selectors are zero. Raw calibration uses the existing fuse owner.
return function(p)
    local address, size = 0x221c2550, 4
    for _, module in pairs(p) do
        if type(module) == 'table' then
            for _, socket in pairs(module) do
                if type(socket) == 'table' and
                    socket.bind == '&apss_router.initiator_socket' and
                    type(socket.address) == 'number' and type(socket.size) == 'number' then
                    assert(socket.address + socket.size <= address or address + size <= socket.address,
                        'The corrected CPR plan word already has an owner')
                end
            end
        end
    end
    p.xbl_calibration_fuses = {moduletype = 'gs_memory', init_mem = true, read_only = true,
        target_socket = {address = address, size = size, bind = '&apss_router.initiator_socket'}}
    return p
end
