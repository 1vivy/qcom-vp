-- SPDX-License-Identifier: BSD-3-Clause-Clear
local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
platform={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'},
    apss_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','AARCH64'},accel='tcg',
        sync_policy='multithread-quantum',qemu_args='-d in_asm -D '..root..'producer.log'},
    apss_router={moduletype='router'},
    apss_access={moduletype='AccessMonitor',initiator_socket={bind='&apss_router.target_socket'}},
    apss={moduletype='cpu_arm_cortexA53',args={'&apss_instance'},rvbar=0x300000,
        has_el3=true,has_el2=true,mem={bind='&apss_access.target_socket'}},
    rom={moduletype='gs_memory',read_only=true,
        target_socket={address=0x300000,size=0x2000,bind='&apss_router.initiator_socket'},
        load={{bin_file=root..'producer.bin',offset=0}}},
    pmic={moduletype='qcom,virtual-pmic-registers-v1',
        identities='0 0 1 1 0 2 0;0 2 1 1 0 2 0',
        exploratory_configuration=true,pon_peripherals='0 0 8',
        gpio_peripherals='0 0 0xc0 1',
        disconnected_irq_peripherals='0 0 0x24',protected_controls='0 0 0x2442 0xff',
        pbs_peripherals='0 0 0x16 0x0501 0 0x1000',registers='0 0 0x1650 0 0xff'},
    spmi={moduletype='qcom,spmi-arbiter-packed-v1',initial_bus_enable=1,
        initial_routes='0 0 0 0x24 0 0;0 1 0 0x16 0 0;0 2 0 1 0 0;'..
            '0 3 0 0x16 4 0;0 4 0 0x16 5 0;0 5 14 0x25 0 0;0 6 15 0x25 0 0;'..
            '0 7 0 0x25 0 0;0 8 1 0x25 0 0;0 9 2 0x25 0 0;0 10 3 0x25 0 0;'..
            '0 11 0 8 0 0;0 12 0 0xc0 0 0',
        secure_write_owner_mask=0x10,
        completion_policy='synchronous-virtual',
        target_socket={address=0xc400000,size=0x900000,bind='&apss_router.initiator_socket'},
        pmic_socket={bind='&pmic.target_socket'}}}
