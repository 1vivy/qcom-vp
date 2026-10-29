local directory=assert(os.getenv('QCOM_VP_PLATFORM_DIR'))..'/'
dofile(directory..'../tools/qcom/qcom-vp/tests/fixtures/cpucp-reset/platform.lua')
platform.cpucp_ram={moduletype='gs_memory',init_mem=true,
    target_socket={address=0x18b30000,size=0x40000,bind='&consumer_router.initiator_socket'}}
-- One immutable owner serves both CPU views. Nonzero cache policy tests polarity.
platform.apss_identity={moduletype='qcom,apss-boot-identity-v1',hardware_version=0x10020003,
    profile_selector=2,variant_selector=1,cpu_present_mask=0x033f,llc_group_disable_mask=0x21,
    target_socket={address=0x17651000,size=0x28,bind='&producer_router.initiator_socket'}}
platform.consumer_apss_identity={moduletype='addrtr',mapped_base_addr=0x17651000,
    target_socket={address=0x17651000,size=0x28,relative_addresses=false,
        bind='&consumer_router.initiator_socket'},
    initiator_socket={bind='&producer_router.target_socket'}}
platform.qsee_ticket_guard={moduletype='qcom,smem-mutex-v1',
    target_socket={address=0x18860004,size=4,bind='&producer_router.initiator_socket'}}
dofile(directory..'qcom/sm88xx/cpucp-subsystem.lua')(platform,
    {router='consumer_router',cpu='consumer',instance='consumer_instance'})
platform.boot.legacy_rnmi=true
-- Only this synthetic two-sided fixture declares a same-channel peer route.
platform.cpucp_mailbox_ns.peer_path='peer_mailbox'
platform.peer_mailbox={moduletype='qcom,cpucp-mailbox-v1',peer_path='cpucp_mailbox_ns',
    tx_socket={address=0x18830000,size=0x300,bind='&producer_router.initiator_socket'},
    rx_socket={address=0x17620000,size=0x4c08,bind='&producer_router.initiator_socket'},
    irq={bind='&cpucp_subsystem.source_29'}}
platform.consumer_boot_publication={moduletype='addrtr',mapped_base_addr=0x17c40000,
    target_socket={address=0x17c40000,size=0x64,relative_addresses=false,
        bind='&consumer_router.initiator_socket'},
    initiator_socket={bind='&producer_router.target_socket'}}
-- Software writes to the real BEU accrued register exercise both mask outputs.
-- These fixture-only connections do not invent a production NMI source.
platform.cpucp_bus_error_0.platform_irq={bind='&cpucp_subsystem.source_30'}
platform.cpucp_bus_error_0.local_irq={bind='&cpucp_subsystem.source_31'}
for lock=0,9 do
    if lock~=1 then
        local address=0x18860000+4*lock
        platform['producer_hwmutex_'..lock]={moduletype='addrtr',mapped_base_addr=address,
            target_socket={address=address,size=4,relative_addresses=false,
                bind='&producer_router.initiator_socket'},
            initiator_socket={bind='&consumer_router.target_socket'}}
    end
end
