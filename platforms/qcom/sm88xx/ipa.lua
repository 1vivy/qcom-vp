-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Both selected DTs and QSEE routines use this IPA layout. See models/qcom/ipa/evidence.md.
return function(p)
    assert(p.apss_router, 'The APSS router is required for IPA.')
    assert(p.ipa==nil and p.ipa_bcr==nil, 'IPA already exists.')
    local bind='&apss_router.initiator_socket'
    local windows={{0x03e04000,0xa000},{0x03f48000,0x2000},{0x00189000,4}}
    for _,window in ipairs(windows) do
        local base,size=window[1],window[2]
        for owner,module in pairs(p) do
            if type(module)=='table' then
                for socket,target in pairs(module) do
                    if type(target)=='table' and target.bind==bind
                        and type(target.address)=='number' and type(target.size)=='number'
                        and base<target.address+target.size and target.address<base+size then
                        error(string.format('IPA 0x%x overlaps %s.%s at 0x%x+0x%x',
                            base,owner,socket,target.address,target.size))
                    end
                end
            end
        end
    end
    p.ipa={moduletype='qcom,ipa-v5.5',user_info_ptr=0x3fb,
        gsi_socket={address=windows[1][1],size=windows[1][2],bind=bind},
        cfg_socket={address=windows[2][1],size=windows[2][2],bind=bind}}
    -- Existing GCC BCR substitutes retain bit zero without a downstream reset signal.
    p.ipa_bcr={moduletype='qcom,retained-boot-control-v1',writable_mask=1,reset_value=0,
        target_socket={address=windows[3][1],size=4,bind=bind}}
    return p
end
