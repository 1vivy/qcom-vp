-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- The selected DCP HAL initializes these interrupts before any panel request.
return function(p, soc)
    assert(soc=='sm8850' or soc=='sm8845', 'The selected DCP image is required.')
    assert(p.dcp_display_window, 'The DCP display window is required.')
    for index,base in ipairs({0x09ac0000,0x09ac3000}) do
        local name='dcp_dsi_interrupts_'..(index-1)
        assert(not p[name], 'The DSI interrupt owner already exists.')
        p[name]={moduletype='qcom,dsi-interrupts-v1',
            target_socket={address=base,size=0x1000,bind='&apss_router.initiator_socket'}}
    end
    -- Other controller registers fail until a model supplies their actual effects.
    return p
end
