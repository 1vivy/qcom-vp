-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- CPUCP configures these words during each cluster's cold watchdog sequence.
-- The virtual profile retains configuration but creates no expiry or reset.
return function(p,o)
    assert(o and o.router, 'The cluster watchdog requires the CPUCP router.')
    local bus='&'..o.router..'.initiator_socket'
    local words={
        0x17c30000,0x17c30004,0x17c30008,0x17c3000c,
        0x17c30018,0x17c3001c,0x17c30020,0x17c30024,
        0x17c30028,0x17c3002c,0x17c30030,0x17c30034,
        0x17c30038,0x17c3003c,0x17c30040,0x17c30044,
        0x17c2e050,0x17c2e054,0x17c2e058,0x17c2e05c,
        0x17c2480c,0x17c2580c,0x17c2b000,0x17c2b008,
        0x17c2c000,0x17c2c008,
        0x17c31010,0x17c31014}
    for _,address in ipairs(words) do
        local name='cpucp_cluster_watchdog_'..string.format('%08x',address)
        assert(not p[name], 'The cluster watchdog word already has an owner.')
        p[name]={moduletype='qcom,retained-boot-control-v1',
            writable_mask=0xffffffff,reset_value=0,
            target_socket={address=address,size=4,bind=bus}}
    end
end
