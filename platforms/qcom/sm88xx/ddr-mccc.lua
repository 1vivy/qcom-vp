-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- The configuration declares four MCCC slave banks. Native helpers retain local controls.
return function(p)
    local owner = assert(p.ddr_global_configuration, 'The MCCC configuration owner is required.')
    owner.configuration_banks = 4
    owner.retained_extent = 0x800
    owner.target_socket.size = 0x800
    for index, address in ipairs({0x31bc0000,0x32bc0000,0x31fc0000,0x32fc0000}) do
        local name = 'ddr_mccc_bank'..(index-1)
        assert(not p[name], 'The MCCC local mapping already exists.')
        p[name] = {moduletype='addrtr',mapped_base_addr=index * 0x10000,
            target_socket={address=address,size=0x800,relative_addresses=false,
                bind='&apss_router.initiator_socket'},
            initiator_socket={bind='&ddr_global_configuration.target_socket'}}
    end
end
