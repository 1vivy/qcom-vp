-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- PDP 0x81301454 reads the APSS header. Donor 0x2311ea reads the marker.
-- CPUCP 0x1d80d4b6 reads the same header and cookie, then the database pointer.
-- All views share one cold backing. See models/qcom/rpmh/evidence.md for export translation.
return function(p)
    assert(p.apss_router and p.tme_router and p.cpucp_router,
        'The APSS, TME, and CPUCP routers are required.')
    assert(not p.aop_configuration and not p.tme_aop_configuration and not p.cpucp_aop_configuration,
        'The AOP configuration memory or alias already exists.')
    for _,region in ipairs({{'apss_router',0x0c3f0000},{'tme_router',0x103f0000},
        {'cpucp_router',0x0c3f0000}}) do
        for name,module in pairs(p) do
            if type(module)=='table' then
                for _,socket in pairs(module) do
                    if type(socket)=='table' and socket.bind=='&'..region[1]..'.initiator_socket' and
                        type(socket.address)=='number' and type(socket.size)=='number' and
                        region[2]<socket.address+socket.size and socket.address<region[2]+0x400 then
                        error('AOP configuration overlaps target '..name)
                    end
                end
            end
        end
    end
    -- The actual producer clears 0x400 bytes. No unsupported export is seeded.
    -- Cold memory contains no published cookie, table pointer, or completion.
    p.aop_configuration={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x0c3f0000,size=0x400,bind='&apss_router.initiator_socket'}}
    p.tme_aop_configuration={moduletype='addrtr',mapped_base_addr=0x0c3f0000,
        target_socket={address=0x103f0000,size=0x400,relative_addresses=false,
            bind='&tme_router.initiator_socket'},
        initiator_socket={bind='&apss_router.target_socket'}}
    p.cpucp_aop_configuration={moduletype='addrtr',mapped_base_addr=0x0c3f0000,
        target_socket={address=0x0c3f0000,size=0x400,relative_addresses=false,
            bind='&cpucp_router.initiator_socket'},
        initiator_socket={bind='&apss_router.target_socket'}}
    return p
end
