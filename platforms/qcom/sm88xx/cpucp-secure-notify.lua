-- Declared virtual APSS secure notification latch. CPUCP executes the service.
-- This connection is separate from the secure hardware mailbox at source24.
return function(p,o)
    assert(o and o.apss_router, 'CPUCP secure notification requires the APSS router.')
    assert(p.cpucp_subsystem, 'CPUCP secure notification requires the CPUCP subsystem.')
    assert(not p.cpucp_secure_notify, 'CPUCP secure notification already exists.')
    p.cpucp_secure_notify={moduletype='addrtr',mapped_base_addr=0,
        target_socket={address=0x17960008,size=4,relative_addresses=false,
            bind='&'..o.apss_router..'.initiator_socket'},
        initiator_socket={bind='&cpucp_subsystem.secure_notify_socket'}}
end
