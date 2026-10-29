local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
platform={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'},
    tme_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','RISCV32'},accel='tcg',
        sync_policy='multithread-quantum',qemu_args='-d in_asm,int -D '..root..'producer.log'},
    tme_router={moduletype='router'},
    tme_access={moduletype='AccessMonitor',allow_unspecified=true,
        initiator_socket={bind='&tme_router.target_socket'}},
    tme={moduletype='cpu_riscv32',args={'&tme_instance',0},resetvec=0x200000,
        mem={bind='&tme_access.target_socket'}},
    rom={moduletype='gs_memory',read_only=true,
        target_socket={address=0x200000,size=0x4000,bind='&tme_router.initiator_socket'},
        load={{bin_file=root..'producer.bin',offset=0}}},
    ram={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x400000,size=0x1000,bind='&tme_router.initiator_socket'}}}
