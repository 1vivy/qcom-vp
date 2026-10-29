-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Selected GlinkDxe QMP endpoint. See models/qcom/aop/evidence.md.
-- The AOP firmware is not executed. This peer publishes the descriptor.
return function(p, o)
    assert(type(p)=='table' and type(o)=='table', 'QMP wiring is required.')
    assert(type(o.router)=='string' and p[o.router], 'The APSS MMIO router is required.')
    assert(type(o.ipcc_path)=='string' and p[o.ipcc_path], 'The APSS IPCC bank is required.')
    assert(not p.aop_qmp, 'The AOP QMP endpoint already exists.')
    local ipcc=p[o.ipcc_path]
    assert(ipcc.moduletype=='qcom,ipcc-local-v3' and ipcc.fabric_path==nil,
        'The AOP QMP doorbell needs the APSS protocol view.')
    assert(ipcc.remote_peer_path==nil, 'The APSS IPCC view already has a functional remote.')
    -- Selected config at PE RVA 0xdb18 declares a 0x400-byte area.
    -- The base is 0x0c300000. The pair is client AOP, signal 0.
    -- No other owner may claim this area.
    -- No other owner may claim the AOP mailbox area.
    for name,module in pairs(p) do
        if type(module)=='table' then
            for socket_name,socket in pairs(module) do
                -- The check uses only sockets on the APSS router.
                if type(socket)=='table' and socket.bind=='&'..o.router..'.initiator_socket' and
                    type(socket.address)=='number' and type(socket.size)=='number' and
                    0x0c300000<socket.address+socket.size and socket.address<0x0c300400 then
                    -- A partial or full overlap is a composition error.
                    -- The name identifies the conflicting socket.
                    error('AOP mailbox area overlap: '..name..'.'..socket_name)
                end
            end
        end
    end
    -- The new peer takes the checked range.
    p.aop_qmp={moduletype='qcom,aop-qmp-v1', ipcc_path=o.ipcc_path, ipcc_client=0,
        target_socket={address=0x0c300000,size=0x400,bind='&'..o.router..'.initiator_socket'}}
    -- The reply uses the same signal.
    ipcc.remote_peer_path='aop_qmp'
    ipcc.remote_peer_index=0
    ipcc.remote_signal=0
    return p
end
