-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Select virtual reset-branch input through the existing shared fuse owner.
return function(p, policy)
    policy = policy or 'below3'
    assert(policy == 'below3' or policy == 'atleast3', 'Unknown virtual CPUCP revision policy.')
    assert(p.cpucp_boot and p.cpucp_boot.moduletype == 'qcom,cpucp-boot-control-v1',
        'CPUCP extended reset requires the existing V1 boot controller.')
    assert(p.fuses and p.fuses.moduletype == 'qcom,sm8850-fuse-controller',
        'CPUCP revision input requires the existing shared fuse controller.')
    local target = p.fuses.target_socket
    assert(target and target.address == 0x021c8000 and target.size == 0x2000
        and target.bind == '&tme_router.initiator_socket', 'Unexpected fuse owner mapping.')
    for _, router in ipairs({'apss_router', 'cpucp_router'}) do
        local alias = p[router .. '_fuses']
        assert(alias and alias.moduletype == 'addrtr' and alias.mapped_base_addr == 0x021c8000
            and alias.target_socket.address == 0x221c8000 and alias.target_socket.size == 0x2000
            and alias.target_socket.relative_addresses == false
            and alias.target_socket.bind == '&' .. router .. '.initiator_socket'
            and alias.initiator_socket.bind == '&tme_router.target_socket',
            'CPUCP revision input requires the existing coherent fuse aliases.')
    end
    -- Offset 0xcf8 already reads patch_version. Do not create an overlapping revision word.
    -- This policy also changes the ROM patch-version input. It is not a physical silicon ID.
    p.fuses.patch_version = policy == 'atleast3' and 3 or 0
    p.cpucp_boot.moduletype = 'qcom,cpucp-boot-control-v2'
    return p
end
