-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Shared selected SM8850/SM8845 IORT topology. See models/qcom/sm88xx/sm8845/smmu/evidence.md.
-- Apply after the existing UFS monitor and physical protection path exist.
return function(p, o)
    assert(type(p)=='table' and type(o)=='table', 'SMMU wiring is required.')
    assert(type(o.router)=='string' and p[o.router], 'The APSS MMIO router is required.')
    assert(type(o.irq_bind)=='function', 'A GSIV-to-interrupt-socket function is required.')
    assert(not p.apps_smmu and not p.gpu_smmu, 'The selected SMMUs already exist.')
    assert(p.ufs_dma_access and p.ufs_dma_access.initiator_socket,
           'The existing UFS DMA monitor is required.')
    local physical=p.ufs_dma_access.initiator_socket.bind
    assert(type(physical)=='string' and physical=='&ufs_dma_protection.target_socket'
           and p.ufs_dma_protection and p.ufs_ram_router,
           'The existing UFS physical protection and RAM router are required.')
    local bind='&'..o.router..'.initiator_socket'
    -- Reject collisions before changing the UFS route or adding either instance.
    for _,window in ipairs({{0x15000000,0x100000},{0x03da0000,0x40000}}) do
        for owner,module in pairs(p) do
            if type(module)=='table' then
                for _,target in pairs(module) do
                    if type(target)=='table' and target.bind==bind
                        and type(target.address)=='number' and type(target.size)=='number'
                        and window[1]<target.address+target.size
                        and target.address<window[1]+window[2] then
                        error(string.format('SMMU 0x%x overlaps %s in %s',window[1],owner,o.router))
                    end
                end
            end
        end
    end
    local context_gsiv={
        129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,
        145,146,147,148,149,150,213,214,215,216,217,218,219,220,221,222,
        223,224,347,348,349,350,351,352,353,354,355,356,357,358,359,360,
        361,362,363,364,365,366,367,368,369,370,371,372,373,374,375,376,
        377,427,428,429,430,431,432,433,434,435,436,437,438,439,440,441,
        450,451,444,453,739,455,456,457,722,723,724,725,726,727,728,729,
        442,520,521,522,523,524,525,526,527,528,529,530,531,532,533,534,
    }
    local function irq(gsiv)
        local binding=o.irq_bind(gsiv)
        assert(type(binding)=='string' and binding:sub(1,1)=='&', 'An SMMU IRQ binding is invalid.')
        return {bind=binding}
    end
    local module={
        moduletype='arm,smmu-v2-v1',
        -- IORT Span is the global half. ID1 places contexts at +0x80000.
        target_socket={address=0x15000000,size=0x100000,
                       bind='&'..o.router..'.initiator_socket'},
        global_irq=irq(97),
    }
    for i,gsiv in ipairs(context_gsiv) do module['context_irq_'..(i-1)]=irq(gsiv) end
    p.apps_smmu=module
    p.ufs_smmu_port={moduletype='arm,smmu-stream-port-v1',args={'&apps_smmu'},
        stream_id=0x60,initiator_socket={bind=physical}}
    p.ufs_dma_access.initiator_socket.bind='&ufs_smmu_port.target_socket'
    local gpu_context_gsiv={
        710,711,712,713,714,715,716,717,718,719,720,454,508,
        606,607,608,609,692,694,697,698,699,701,702,732,
    }
    local gpu={
        moduletype='arm,smmu-v2-v1', context_count=25, global_span=0x20000,
        -- No GPU DMA producer exists. The GPU has no stream port.
        target_socket={address=0x03da0000,size=0x40000,
                       bind='&'..o.router..'.initiator_socket'},
        global_irq=irq(706),
    }
    for i,gsiv in ipairs(gpu_context_gsiv) do gpu['context_irq_'..(i-1)]=irq(gsiv) end
    p.gpu_smmu=gpu
    return p
end
