-- SPDX-License-Identifier: BSD-3-Clause-Clear
local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
local directory=assert(os.getenv('QCOM_VP_PLATFORM_DIR'))..'/'
platform={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'},
    apss_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','AARCH64'},accel='tcg',
        sync_policy='multithread-quantum',qemu_args='-d in_asm -D '..root..'producer.log'},
    apss_router={moduletype='router'},physical_router={moduletype='router'},
    marker_router={moduletype='router'},
    apss={moduletype='cpu_arm_cortexA53',args={'&apss_instance'},rvbar=0x300000,
        has_el3=true,has_el2=true,mem={bind='&apss_router.target_socket'}},
    -- Expected DMA errors reach the guest's synchronous-abort handler.
    -- Only terminal markers use AccessMonitor and the existing harness protocol.
    markers={moduletype='AccessMonitor',
        target_socket={address=0x60000000,size=0x60000000,relative_addresses=false,
            bind='&apss_router.initiator_socket'},
        initiator_socket={bind='&marker_router.target_socket'}},
    marker_guard={moduletype='gs_memory',init_mem=true,
        target_socket={address=0,size=4,bind='&marker_router.initiator_socket'}},
    rom={moduletype='gs_memory',read_only=true,
        target_socket={address=0x300000,size=0x20000,bind='&apss_router.initiator_socket'},
        load={{bin_file=root..'producer.bin',offset=0}}},
    ram={moduletype='gs_memory',dmi_allow=false,init_mem=true,
        target_socket={address=0x14800000,size=0x20000,bind='&physical_router.initiator_socket'}},
    cpu_ram={moduletype='addrtr',mapped_base_addr=0x14800000,
        target_socket={address=0x14800000,size=0x20000,relative_addresses=false,
            bind='&apss_router.initiator_socket'},
        initiator_socket={bind='&physical_router.target_socket'}},
    gpu_smmu={moduletype='arm,smmu-v2-v1',context_count=25,global_span=0x20000,
        target_socket={address=0x03da0000,size=0x40000,bind='&apss_router.initiator_socket'},
        global_irq={bind='&apss_gic.spi_in_674'},
        context_irq_24={bind='&apss_gic.spi_in_700'}},
    smmu={moduletype='arm,smmu-v2-v1',
        target_socket={address=0x15000000,size=0x100000,bind='&apss_router.initiator_socket'},
        global_irq={bind='&apss_gic.spi_in_65'},
        context_irq_0={bind='&apss_gic.spi_in_97'},
        context_irq_7={bind='&apss_gic.spi_in_104'}},
    -- Synthetic native bus master aperture. Offset is the actual SMMU input VA.
    smmu_port={moduletype='arm,smmu-stream-port-v1',args={'&smmu'},stream_id=0x60,
        target_socket={address=0x1000000000,size=0x2000000000,bind='&apss_router.initiator_socket'},
        initiator_socket={bind='&physical_router.target_socket'}}}
dofile(directory..'qcom/apss-gic.lua')(platform,{cpu='apss',instance='apss_instance',router='apss_router'})
