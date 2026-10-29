local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
platform={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'},
    apss_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','AARCH64'},accel='tcg',
        sync_policy='multithread-quantum',qemu_args='-d in_asm -D '..root..'producer.log'},
    apss_router={moduletype='router'},
    apss_access={moduletype='AccessMonitor',initiator_socket={bind='&apss_router.target_socket'}},
    apss={moduletype='cpu_arm_cortexA53',args={'&apss_instance'},rvbar=0x300000,
        has_el3=true,has_el2=true,mem={bind='&apss_access.target_socket'}},
    rom={moduletype='gs_memory',read_only=true,
        target_socket={address=0x300000,size=0x1000,bind='&apss_router.initiator_socket'},
        load={{bin_file=root..'producer.bin',offset=0}}},
    timer={moduletype='qcom,sm8850-qtimer-v1',clock_hz=19200000,enabled_at_reset=false,
        control_socket={address=0x0c210000,size=4,bind='&apss_router.initiator_socket'},
        config_socket={address=0x17810000,size=0x1000,bind='&apss_router.initiator_socket'},
        frame0_socket={address=0x17811000,size=0x1000,bind='&apss_router.initiator_socket'},
        frame0_el0_socket={address=0x17812000,size=0x1000,bind='&apss_router.initiator_socket'},
        frame1_socket={address=0x17813000,size=0x1000,bind='&apss_router.initiator_socket'},
        frame2_socket={address=0x17815000,size=0x1000,bind='&apss_router.initiator_socket'},
        frame3_socket={address=0x17817000,size=0x1000,bind='&apss_router.initiator_socket'},
        frame4_socket={address=0x17819000,size=0x1000,bind='&apss_router.initiator_socket'},
        frame6_socket={address=0x1781d000,size=0x1000,bind='&apss_router.initiator_socket'}}}
local directory=assert(os.getenv('QCOM_VP_PLATFORM_DIR'))..'/'
dofile(directory..'qcom/apss-gic.lua')(platform,{router='apss_router'})
platform.timer.irq_0={bind='&apss_gic.spi_in_8'}
platform.timer.irq_1={bind='&apss_gic.spi_in_9'}
platform.timer.irq_2={bind='&apss_gic.spi_in_10'}
platform.timer.irq_3={bind='&apss_gic.spi_in_11'}
platform.timer.irq_4={bind='&apss_gic.spi_in_12'}
