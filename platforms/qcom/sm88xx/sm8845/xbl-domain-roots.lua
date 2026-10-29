-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- CPH2771 XBL domain-root table 0x148f03d8. Reuse reset objects by address.
-- Rate and source evidence: models/qcom/sm88xx/tme_runtime_abi/evidence.md.
return function(p)
    -- Voted OffNoReCAL callbacks at 0x148f11d8, source descriptors 0x148ef108..0x148ef2c8.
    -- Keep STATUS in the shared PLL model. This is not a separate ready register.
    assert(p.gcc_plls and p.gcc_plls.moduletype=='qcom,gcc-taycan-eko-t-pll-v1',
        'The shared GCC PLL bank is required for CPH2771 domain roots.')
    p.gcc_plls.coarse_calibration_mask=0x7f6
    p.gcc_plls.virtual_coarse_calibration_code=0
    -- Descriptor address, command address, main selector, GCC PLL bank index.
    local roots={
        {0x148ef548,0x18206c,1,0},{0x148ef508,0x1826bc,5,4},
        {0x148ef588,0x141024,2,7},{0x148ef5c8,0x1a200c,1,0},
        {0x148ef5e8,0x1742d4,1,0},{0x148ef608,0x1741a8,1,0},
        {0x148ef648,0x17452c,1,0},{0x148ef6a0,0x174400,1,0},
        {0x148ef6c0,0x174658,5,4},{0x148ef6e0,0x148298,1,0},
        {0x148ef720,0x1484f0,1,0},{0x148ef740,0x148030,5,4},
        {0x148ef760,0x148168,1,0},{0x148ef880,0x14402c,1,10},
        {0x148ef8c0,0x11c0c8,4,1},{0x148ef8e0,0x11c1f8,4,1},
        {0x148ef900,0x183030,4,1},{0x148efe78,0x144158,5,4},
        {0x148efe98,0x176030,1,0},{0x148efeb8,0x176160,5,4},
    }
    local names={}
    for _,record in ipairs(roots) do
        local address=record[2]
        local name,root
        for candidate_name,candidate in pairs(p) do
            if type(candidate)=='table' and candidate.moduletype=='qcom,rcg-v1'
                and candidate.target_socket and candidate.target_socket.address==address
                and candidate.target_socket.bind=='&cpucp_clock_router.initiator_socket' then
                assert(root==nil,'Duplicate CPH2771 domain root')
                name,root=candidate_name,candidate
            end
        end
        if not root then
            -- This is the sole root absent from the compatibility reset composition.
            assert(address==0x1826bc,'Missing established CPH2771 domain root')
            name='sm8845_domain_root_'..address
            assert(p[name]==nil,'Duplicate CPH2771 root module')
            root={moduletype='qcom,rcg-v1',
                target_socket={address=address,size=0x14,bind='&cpucp_clock_router.initiator_socket'}}
            p[name]=root
            for _,router in ipairs({'apss_router','cpucp_router'}) do
                p[name..'_'..router]={moduletype='addrtr',mapped_base_addr=address,
                    target_socket={address=address,size=0x14,relative_addresses=false,
                        bind='&'..router..'.initiator_socket'},
                    initiator_socket={bind='&cpucp_clock_router.target_socket'}}
            end
        end
        root.pll_path='gcc_plls'
        root.crystal_selector=0
        root.primary_selector=record[3]
        root.primary_index=record[4]
        root.primary_even_selector=8
        root.secondary_selector=8
        root.calibration_selector=8
        root.require_root_enable=false
        names[address]=name
    end
    -- Nine rates at 0x148ef780 require six independent GCC main outputs.
    local multi_source=p[names[0x14402c]]
    multi_source.secondary_selector=2
    multi_source.secondary_index=6
    multi_source.main_sources='3 5;4 1;5 4;7 2'
    -- Branch descriptors 0x148f00d8 and 0x148f0398 explicitly name these roots.
    return {[0x144018]=names[0x14402c],[0x144024]=names[0x144158]}
end
