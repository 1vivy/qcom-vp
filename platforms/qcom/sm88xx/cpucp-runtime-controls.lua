-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Bounded virtual configuration-apply policy. See CPUCP cluster evidence.
return function(p,o)
    assert(o and o.boot and o.clock and o.routers and #o.routers>0,
        'CPUCP runtime controls require the existing boot owner, clock, and routers.')
    local boot=assert(p[o.boot], 'The CPUCP boot owner is required.')
    assert(not p.cpucp_runtime_router, 'CPUCP runtime controls already exist.')
    p.cpucp_runtime_router={moduletype='router'}
    local bus='&cpucp_runtime_router.initiator_socket'
    boot.runtime_clock_path=o.clock
    boot.runtime_lane_mask=o.lanes or 3
    boot.runtime_socket={address=0,size=0x14,bind=bus}
    boot.runtime_policy_socket={address=0x100,size=0x30,bind=bus}
    boot.runtime_clock_socket={address=0x200,size=0x34,bind=bus}
    -- Translate independent banks to relative interfaces in the same owner.
    -- Gaps remain errors. No mapped word is anonymous register RAM.
    local ranges={
        {0x18877800,0x14,0},
        {0x17c02158,4,0x100},{0x17903004,4,0x104},{0x1886800c,4,0x108},
        {0x181b2010,4,0x110},{0x183b2010,4,0x114},
        {0x18003018,8,0x120},{0x18203018,8,0x128},
        {0x17953000,0x34,0x200}}
    for _,router in ipairs(o.routers) do
        for _,range in ipairs(ranges) do
            p[router..'_cpucp_runtime_'..range[1]]={moduletype='addrtr',mapped_base_addr=range[3],
                target_socket={address=range[1],size=range[2],relative_addresses=false,
                    bind='&'..router..'.initiator_socket'},
                initiator_socket={bind='&cpucp_runtime_router.target_socket'}}
        end
    end
end
