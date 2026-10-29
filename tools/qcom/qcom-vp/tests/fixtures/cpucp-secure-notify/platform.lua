local directory=assert(os.getenv('QCOM_VP_PLATFORM_DIR'))..'/'
dofile(directory..'../tools/qcom/qcom-vp/tests/fixtures/cpucp-reset/platform.lua')
platform.cpucp_ram={moduletype='gs_memory',init_mem=true,
    target_socket={address=0x18b30000,size=0x40000,bind='&consumer_router.initiator_socket'}}
dofile(directory..'qcom/sm88xx/cpucp-subsystem.lua')(platform,
    {router='consumer_router',cpu='consumer',instance='consumer_instance'})
dofile(directory..'qcom/sm88xx/cpucp-secure-notify.lua')(platform,{apss_router='producer_router'})
