-- SPDX-License-Identifier: BSD-3-Clause-Clear
return function(p)
    assert(p.apss_router and p.crypto_bam and p.apps_smmu,
        'QCE requires the APSS router, crypto BAM, and APPS SMMU.')
    assert(not p.gpce, 'Duplicate QCE owner')
    assert(not p.gpce_register_router, 'Duplicate GPCE register router')
    assert(not p.crypto_smmu_port, 'The crypto SMMU port already exists.')
    local base=0x01df0000
    -- 0xc000 is GOPROC_OEM_KEY (QCE offset 0x2000, Linux regs-v5.h). QSEE RPMB key
    -- derivation starts its AES-256 CMAC request there.
    -- 0x0000 is the HWKM shared group: identity, shared status, and key-table
    -- size. QSEE 15703ac8 reads it for the key-manager info record.
    local banks={{0x0000,0x10},{0x1000,0x24},{0x2000,0x60},{0x5000,0x10},
        {0xa000,0x1000},{0xc000,4},{0xd000,0x80},{0xe000,0x1000},{0xf000,4},{0xf100,0x28}}
    local bind='&apss_router.initiator_socket'
    for _,bank in ipairs(banks) do
        local address,size=base+bank[1],bank[2]
        for owner,module in pairs(p) do
            if type(module)=='table' then
                for _,target in pairs(module) do
                    if type(target)=='table' and target.bind==bind and
                        type(target.address)=='number' and type(target.size)=='number' then
                        assert(address>=target.address+target.size or target.address>=address+size,
                            'QCE overlaps '..owner)
                    end
                end
            end
        end
    end
    p.gpce_register_router={moduletype='router'}
    for index,bank in ipairs(banks) do
        p['gpce_register_bank_'..index]={moduletype='addrtr',mapped_base_addr=bank[1],
            target_socket={address=base+bank[1],size=bank[2],relative_addresses=false,bind=bind},
            initiator_socket={bind='&gpce_register_router.target_socket'}}
    end
    -- Explicit virtual geometry, not selected-silicon fuse values.
    -- Destination selectors do not identify TME source keys or grant authority.
    p.gpce={moduletype='qcom,qce-v6',
        first_slot_count=8,total_slot_count=16,
        primary_destination=8,secondary_destination=9,
        selector_base0=0,selector_base1=2,selector_base2=4,
        stream_ready={bind='&crypto_bam.stream_ready'},
        target_socket={address=0,size=0x10000,bind='&gpce_register_router.initiator_socket'}}
    p.crypto_bam.command_base=0x01dc0000
    -- Both selected IORT tables map the secure CRYPTO client to stream 0xc2.
    p.crypto_smmu_port={moduletype='arm,smmu-stream-port-v1',args={'&apps_smmu'},
        stream_id=0xc2,initiator_socket={bind='&apss_router.target_socket'}}
    p.crypto_bam.dma_socket={bind='&crypto_smmu_port.target_socket'}
    p.crypto_bam.command_socket={bind='&apss_router.target_socket'}
    p.crypto_bam.stream_socket={bind='&gpce.stream_socket'}
    return p
end
