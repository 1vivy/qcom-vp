-- SPDX-License-Identifier: BSD-3-Clause-Clear
local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
local directory=assert(os.getenv('QCOM_VP_PLATFORM_DIR'))..'/'
platform={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'},
    apss_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','AARCH64'},accel='tcg',
        sync_policy='multithread-quantum'},
    apss_router={moduletype='router'},
    apss_access={moduletype='AccessMonitor',initiator_socket={bind='&apss_router.target_socket'}},
    apss={moduletype='cpu_arm_cortexA53',args={'&apss_instance'},rvbar=0x300000,
        has_el3=true,has_el2=true,mem={bind='&apss_access.target_socket'}},
    rom={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x300000,size=0x20000,bind='&apss_router.initiator_socket'},
        load={{bin_file=root..'producer.bin',offset=0}}},
    stack={moduletype='gs_memory',
        target_socket={address=0x14000000,size=0x10000,bind='&apss_router.initiator_socket'}}}
dofile(directory..'qcom/apss-gic.lua')(platform,
    {cpu='apss',instance='apss_instance',router='apss_router'})
for protocol,clients in ipairs({17,18}) do
    local owner='ipcc_'..protocol
    local base=0x01100000+(protocol-1)*0x100000
    platform[owner]={moduletype='qcom,ipcc-local-v3',protocol=protocol-1,
        local_index=6,client_count=clients,fabric_client_count=18,signal_count=8,
        irq={bind='&apss_gic.spi_in_'..(245+protocol)},
        target_socket={address=base+0x6000,size=0x1000,bind='&apss_router.initiator_socket'}}
    platform[owner..'_secure']={moduletype='qcom,ipcc-local-v3',protocol=protocol-1,
        local_index=1,client_count=clients,signal_count=8,fabric_path=owner,
        irq={bind='&apss_gic.spi_in_'..(228+protocol)},
        target_socket={address=base+0x1000,size=0x1000,bind='&apss_router.initiator_socket'}}
end
platform.tme_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','RISCV32'},
    accel='tcg',sync_policy='multithread-quantum'}
platform.tme_router={moduletype='router'}
platform.tme_access={moduletype='AccessMonitor',allow_unspecified=true,
    initiator_socket={bind='&tme_router.target_socket'}}
platform.tme={moduletype='qcom,tme-clic-cpu-v1',args={'&tme_instance',0},
    resetvec=0x200000,mem={bind='&tme_access.target_socket'}}
platform.tme_rom={moduletype='gs_memory',read_only=true,
    target_socket={address=0x200000,size=0x1000,bind='&tme_router.initiator_socket'},
    load={{bin_file=root..'tme.bin',offset=0}}}
platform.shared={moduletype='gs_memory',init_mem=true,dmi_allow=false,
    target_socket={address=0x400000,size=0x1000,bind='&apss_router.initiator_socket'}}
platform.tme_shared={moduletype='addrtr',mapped_base_addr=0,
    target_socket={address=0x400000,size=0x1000,relative_addresses=false,
        bind='&tme_router.initiator_socket'},
    initiator_socket={bind='&shared.target_socket'}}
platform.clic={moduletype='qcom,tme-clic-v1',priority_reset=1,
    pending_socket={address=0x01800000,size=0x41,bind='&tme_router.initiator_socket'},
    enable_socket={address=0x01800400,size=0x41,bind='&tme_router.initiator_socket'},
    priority_socket={address=0x01800800,size=0x41,bind='&tme_router.initiator_socket'},
    threshold_socket={address=0x01800c00,size=1,bind='&tme_router.initiator_socket'},
    source_socket={address=0x00c00100,size=0x20,bind='&tme_router.initiator_socket'}}
for id=0,64 do platform.clic['irq_'..id]={bind='&tme.clic_irq_'..id} end
platform.tme_ipcc={moduletype='qcom,ipcc-local-v3',protocol=0,local_index=16,
    client_count=18,signal_count=8,fabric_path='ipcc_1',irq={bind='&clic.source_38'},
    target_socket={address=0x01110000,size=0x1000,bind='&apss_router.initiator_socket'}}
platform.tme_ipcc_alias={moduletype='addrtr',mapped_base_addr=0,
    target_socket={address=0x05110000,size=0x1000,relative_addresses=false,
        bind='&tme_router.initiator_socket'},
    initiator_socket={bind='&tme_ipcc.target_socket'}}
platform.extra_client={moduletype='qcom,ipcc-local-v3',protocol=0,local_index=17,
    client_count=18,signal_count=8,fabric_path='ipcc_1',irq={bind='&apss_gic.spi_in_231'},
    target_socket={address=0x01111000,size=0x1000,bind='&apss_router.initiator_socket'}}
platform.common={moduletype='gs_memory',init_mem=true,dmi_allow=false,
    target_socket={address=0x011cf008,size=4,bind='&apss_router.initiator_socket'}}
platform.common_alias={moduletype='addrtr',mapped_base_addr=0,
    target_socket={address=0x051cf008,size=4,relative_addresses=false,
        bind='&tme_router.initiator_socket'},
    initiator_socket={bind='&common.target_socket'}}
