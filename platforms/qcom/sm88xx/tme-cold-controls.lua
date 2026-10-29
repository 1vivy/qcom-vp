-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- OEM cold configuration evidence: models/qcom/sm88xx/boot_control/evidence.md.
return function(p)
    assert(p.tme_router and p.tme, 'The TME router and CPU are required.')
    assert(not p.tme_low_power_control, 'The TME low-power control already exists.')
    -- Migrate the queue diagnostic into the same firmware-owned diagnostic bank.
    p.runtime_queue_status=nil
    p.tme_low_power_control={moduletype='qcom,tme-low-power-control-v1',args={'&tme'},
        policy='cold-no-external-inhibit',
        inhibit_socket={address=0x00c0a000,size=4,bind='&tme_router.initiator_socket'},
        vector_socket={address=0x00c00208,size=4,bind='&tme_router.initiator_socket'},
        diagnostic_socket={address=0x00c04fe0,size=0x20,bind='&tme_router.initiator_socket'}}
    return p
end
