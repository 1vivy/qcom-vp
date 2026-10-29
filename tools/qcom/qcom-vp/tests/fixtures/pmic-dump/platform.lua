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
    -- PON group 8 declares type one and takes part in the dump. PBS group 0x16
    -- answers offset four with 0x16 and takes part. Absent group 4 reads zero at
    -- offset four, so its selection changes nothing.
    pmic={moduletype='qcom,virtual-pmic-registers-v1',
        identities='0 0 1 1 0 2 0',
        exploratory_configuration=true,
        pon_peripherals='0 0 8',
        pbs_peripherals='0 0 0x16 0x0501 0 0x1000',
        absent_peripherals='0 0 4',
        gpio_peripherals='0 0 0xc0 1',
        dump_peripherals='0 0 8;0 0 0x16;0 0 4',
        -- QSEE reads each present block again on an unmarked path. The scan
        -- contract answers bytes without a value and leaves transfers real.
        scan_block_reads=true,
        registers='0 0 0x1650 0 0xff'},
    spmi={moduletype='qcom,spmi-arbiter-packed-v1',initial_bus_enable=1,
        secure_observer_ee=1,
        initial_routes='0 0 0 8 0 0;0 1 0 0xc0 0 0;0 2 0 0x16 0 0;0 3 0 4 0 0',
        completion_policy='synchronous-virtual',
        target_socket={address=0xc400000,size=0x900000,bind='&apss_router.initiator_socket'},
        pmic_socket={bind='&pmic.target_socket'}}}
