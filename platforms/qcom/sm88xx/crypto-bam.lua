-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Crypto BAM register file. See models/qcom/bam/evidence.md.
-- Upstream Kaanapali DT: qcom,bam-v2.0.0 at 0x01dc4000+0x22000, 20 channels, 4 EEs.
-- The selected SM8850 and SM8845 vendor_boot DTBs declare the same window.
-- The 20 pipe pages end at +0x20000. XBL boot policy owns the AC control word at 0x01de4000.
return function(p)
    assert(p.apss_router, 'The APSS router is required for the crypto BAM.')
    assert(p.crypto_bam==nil, 'The crypto BAM already exists.')
    local bind='&apss_router.initiator_socket'
    local base,size=0x01dc4000,0x20000
    for owner,module in pairs(p) do
        if type(module)=='table' then
            for socket,target in pairs(module) do
                if type(target)=='table' and target.bind==bind
                    and type(target.address)=='number' and type(target.size)=='number'
                    and base<target.address+target.size and target.address<base+size then
                    error(string.format('Crypto BAM 0x%x overlaps %s.%s at 0x%x+0x%x',
                        base,owner,socket,target.address,target.size))
                end
            end
        end
    end
    -- The revision field is virtual. No selected source gives the silicon value.
    p.crypto_bam={moduletype='qcom,bam-v2.0.0',num_pipes=20,num_ees=4,revision=0x20,address_bits=36,
        target_socket={address=base,size=size,bind=bind}}
    return p
end
