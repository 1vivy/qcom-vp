-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- CPUCP post-ROM clock-gating overrides. See CPUCP subsystem evidence.
return function(p,o)
    assert(o and o.routers and #o.routers>0, 'CPUCP clock gating requires the CPU routers.')
    assert(not p.cpucp_clock_gating_router, 'CPUCP clock gating already exists.')
    p.cpucp_clock_gating_router={moduletype='router'}
    -- CPUCP 0x1d80ad6c clears these 32-bit override fields. No catalog defines
    -- their layout, so the reset value is declared virtual zero and all bits are retained.
    local word='0:0:ffffffff:4'
    local banks={
        {0x17c06040,8,word..' 4:0:ffffffff:4'},
        {0x17906024,4,word},
        {0x17c02144,8,word..' 4:0:ffffffff:4'},
        {0x17912000,0x10,word..' 4:0:ffffffff:4 8:0:ffffffff:4 c:0:ffffffff:4'}}
    for _,bank in ipairs(banks) do
        local name=string.format('cpucp_clock_gating_%08x',bank[1])
        p[name]={moduletype='qcom,clock-gating-overrides-v1',words=bank[3],
            target_socket={address=bank[1],size=bank[2],
                bind='&cpucp_clock_gating_router.initiator_socket'}}
        for _,router in ipairs(o.routers) do
            p[router..'_'..name]={moduletype='addrtr',mapped_base_addr=bank[1],
                target_socket={address=bank[1],size=bank[2],relative_addresses=false,
                    bind='&'..router..'.initiator_socket'},
                initiator_socket={bind='&cpucp_clock_gating_router.target_socket'}}
        end
    end
end
