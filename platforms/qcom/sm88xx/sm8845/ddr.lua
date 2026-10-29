-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Functional LPDDR5 policy for CPH2771, not captured phone geometry.
-- XBL table 0x148f1488 maps 4 GiB into two disjoint APSS windows.
-- Shared ABI and virtual effects: models/qcom/sm88xx/ddr/evidence.md.
return function(p)
    assert(p.apss and p.apss_router, 'The APSS CPU and router are required for virtual DDR.')
    assert(p.apss_sram and p.shrm_iram and p.shrm_dmem, 'The authoritative SRAM mappings are required.')
    assert(p.ufs_ram_router and p.sequencer_dma_router, 'The DDR DMA routers are required.')
    assert(p.virtual_ddr == nil and p.virtual_shrm == nil, 'Virtual DDR or SHRM already exists.')
    local windows = {
        {name='ddr_low', address=0x80000000},
        {name='ddr_high', address=0x880000000},
    }
    local dma_routers={'ufs_ram_router','sequencer_dma_router'}
    if p.cpucp_router then dma_routers[#dma_routers+1]='cpucp_router' end
    if p.tme_router then dma_routers[#dma_routers+1]='tme_router' end
    for _, window in ipairs(windows) do
        assert(p[window.name] == nil, 'The DDR backing already exists: '..window.name)
        for _, router in ipairs(dma_routers) do
            assert(p[router..'_'..window.name] == nil, 'The DDR DMA alias already exists.')
        end
    end

    p.virtual_ddr = {
        moduletype='qcom,virtual-ddr-v1',
        shared_base=0x3104a800,
        startup_version_major=5,
        startup_version_minor=0x12,
        startup_version_patch=0,
        training_data_offset=0x38,
        dram_base=0x80000000,
        dram_lower_size=0x80000000,
        dram_upper_base=0x880000000,
        -- Two 512 MiB x8 lanes form each 1024 MiB x16 controller.
        -- XBL geometry table 0x14921096 selects 15/10/16 for MR8 0x10.
        rank0_mib=512,
        rank1_mib=0,
        rows=15,
        columns=10,
        banks=16,
        memory_socket={bind='&apss_router.target_socket'},
    }
    p.virtual_shrm = {
        moduletype='qcom,virtual-shrm-v1',
        backend_path='virtual_ddr',
        startup_policy='guest-release',
        completion_policy='synchronous-virtual',
        target_socket={address=0x31076000, size=0x70, bind='&apss_router.initiator_socket'},
        processor_control_socket={address=0x31079000, size=4, bind='&apss_router.initiator_socket'},
    }
    if p.tme_router then
        p.tme_shrm_processor_control = {
            moduletype='addrtr', mapped_base_addr=0,
            target_socket={address=0x31079000, size=4, relative_addresses=false,
                bind='&tme_router.initiator_socket'},
            initiator_socket={bind='&virtual_shrm.processor_control_socket'},
        }
    end

    for _, window in ipairs(windows) do
        -- Each byte has one backing. Allocation and cold-zeroing use 2 MiB blocks.
        -- The physical base must match APSS for DMA code invalidation.
        p[window.name..'_backing'] = {
            moduletype='qcom,shared-executable-memory-v1',
            args={'&apss_group'},
            init_mem=true,
            max_block_size=0x200000,
            target_socket={address=window.address, size=0x80000000},
        }
        p[window.name] = {
            moduletype='qcom,llcc-dare-port-v1',
            args={'&llcc','&apss_group'},
            physical_base=window.address,
            memory_size=0x80000000,
            target_socket={address=window.address, size=0x80000000,
                bind='&apss_router.initiator_socket'},
            initiator_socket={bind='&'..window.name..'_backing.target_socket'},
        }
        for _, router in ipairs(dma_routers) do
            -- DMA engines receive the same system-physical addresses as APSS.
            p[router..'_'..window.name] = {
                moduletype='addrtr', mapped_base_addr=0,
                target_socket={address=window.address, size=0x80000000,
                    relative_addresses=false, bind='&'..router..'.initiator_socket'},
                initiator_socket={bind='&'..window.name..'.target_socket'},
            }
        end
    end
    return p
end
