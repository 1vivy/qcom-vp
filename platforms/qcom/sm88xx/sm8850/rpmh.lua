-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- The selected AOP tables contain fifteen ARC levels and thirty-two BCM resources.
-- QSEE uses DRV0. UEFI uses DRV2. CPUCP table 0x1d82edc0 selects DRV1 and DRV3.
return function(p)
    assert(p.apss_router and p.cpucp_router, 'RPMh requires the APSS and CPUCP routers.')
    assert(p.rpmh_rsc==nil, 'Duplicate RPMh resource manager.')
    local address,size=0x18900000,0x40000
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
    for offset=0,0x7c,4 do resources[#resources+1]=string.format('0x%x',0x50000+offset) end
    -- CPUCP writes its sleep and wake TCSs at table bytes 3 and 4: DRV1 TCS 2..3 and DRV3 TCS 1..2.
    -- Display and TME already use requesters 1 and 3, so the CPUCP DRVs use requesters 4 and 5.
    -- VRM resources exist only after the AOP service publishes Command DB.
    p.rpmh_rsc={moduletype='qcom,rpmh-rsc-v1',banks='0 4 2;1 4 2 4;2 6 3;3 3 1 5',sequencer_words=64,
        resources=table.concat(resources,','),directory_path='aop_resource_service',
        irq_0={bind='&apss_gic.spi_in_3'},irq_2={bind='&apss_gic.spi_in_5'},
        target_socket={address=address,size=size,bind='&apss_router.initiator_socket'}}
    for _,drv in ipairs({1,3}) do
        local base=address+drv*0x10000
        p['cpucp_rpmh_drv'..drv]={moduletype='addrtr',mapped_base_addr=base,
            target_socket={address=base,size=0x10000,relative_addresses=false,
                bind='&cpucp_router.initiator_socket'},
            initiator_socket={bind='&apss_router.target_socket'}}
    end
    return p
end
