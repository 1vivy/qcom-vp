-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- One command engine shares the virtual DDR topology with SHRM.
return function(p)
    assert(p.apss_router and p.virtual_ddr and p.ddr_phy_llc,
        'The DDR router, backend, and LLC owner are required.')
    assert(p.virtual_ddr.moduletype=='qcom,virtual-ddr-v1', 'The virtual DDR backend is required.')
    assert(p.ddr_command==nil, 'The DDR command engine already exists.')
    local mapping=p.ddr_phy_llc.target_socket
    assert(mapping.address==0x34a00000 and mapping.size==0x10000
        and mapping.bind=='&apss_router.initiator_socket', 'The LLC mapping changed.')
    p.ddr_command={moduletype='qcom,ddr-command-engine-v1',backend_path='virtual_ddr',
        broadcast_socket=mapping,fallback_socket={bind='&ddr_phy_llc.target_socket'},
        and_socket={address=0x34e00000,size=mapping.size,bind='&apss_router.initiator_socket'}}
    p.ddr_phy_llc.target_socket={}
    p.ddr_phy_llc.configuration_banks=4
    p.ddr_phy_llc.retained_extent=mapping.size
    p.ddr_phy_llc.command_engine_path='ddr_command'
    for index,base in ipairs({0x31a00000,0x32a00000,0x31e00000,0x32e00000}) do
        local address,size=base,mapping.size
        for _,module in pairs(p) do
            if type(module)=='table' then
                for _,socket in pairs(module) do
                    if type(socket)=='table' and socket.bind=='&apss_router.initiator_socket'
                        and type(socket.address)=='number' and type(socket.size)=='number' then
                        assert(socket.address>=address+size or socket.address+socket.size<=address,
                            'A DDR command pair aperture already has an owner.')
                    end
                end
            end
        end
        p.ddr_command['pair'..(index-1)..'_socket']={address=address,size=size,
            bind='&apss_router.initiator_socket'}
    end
    return p
end
