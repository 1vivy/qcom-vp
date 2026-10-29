-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- The selected AOP tables contain fifteen ARC levels and thirty BCM resources.
return function(p)
    assert(p.apss_router, 'RPMh requires the APSS router.')
    assert(p.rpmh_rsc==nil, 'Duplicate RPMh resource manager.')
    local address,size=0x18900000,0x30000
    for name,module in pairs(p) do
        if type(module)=='table' then
            for _,socket in pairs(module) do
                if type(socket)=='table' and socket.bind=='&apss_router.initiator_socket'
                    and type(socket.address)=='number' and type(socket.size)=='number'
                    and address<socket.address+socket.size and socket.address<address+size then
                    error('RPMh overlaps APSS target '..name)
                end
            end
        end
    end
    local resources={}
    for offset=0,0xe0,0x10 do resources[#resources+1]=string.format('0x%x',0x30000+offset) end
    for offset=0,0x7c,4 do
        if offset~=0x54 and offset~=0x64 then
            resources[#resources+1]=string.format('0x%x',0x50000+offset)
        end
    end
    p.rpmh_rsc={moduletype='qcom,rpmh-rsc-v1',banks='0 4 2;2 6 3',sequencer_words=64,
        resources=table.concat(resources,','),
        irq_0={bind='&apss_gic.spi_in_3'},irq_2={bind='&apss_gic.spi_in_5'},
        target_socket={address=address,size=size,bind='&apss_router.initiator_socket'}}
    return p
end
