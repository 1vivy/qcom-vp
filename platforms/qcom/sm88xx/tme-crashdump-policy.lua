-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- XBL debug-info query evidence: models/qcom/sm88xx/boot_control/tme_crashdump_evidence.md.
return function(p)
    assert(p.tme_router and p.apss_router and p.tme_external_router,
        'The existing APSS and protected TME routes are required.')
    assert(not p.tme_crashdump_policy and not p.apss_tme_crashdump_policy,
        'The TME crash-dump policy already exists.')
    p.tme_crashdump_policy={moduletype='qcom,tme-crashdump-policy-v1',policy='disabled',
        target_socket={address=0x00c02000,size=4,bind='&tme_router.initiator_socket'}}
    p.apss_tme_crashdump_policy={moduletype='addrtr',mapped_base_addr=0x00c02000,
        target_socket={address=0x20c02000,size=4,relative_addresses=false,
            bind='&apss_router.initiator_socket'},
        initiator_socket={bind='&tme_external_router.target_socket'}}
    return p
end
