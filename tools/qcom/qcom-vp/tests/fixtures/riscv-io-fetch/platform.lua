-- SPDX-License-Identifier: BSD-3-Clause-Clear
local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
platform={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'},
    instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','RISCV32'},accel='tcg',
        sync_policy='multithread-quantum',qemu_args='-d in_asm -D '..root..'producer.log'},
    router={moduletype='router'},
    access={moduletype='AccessMonitor',allow_unspecified=true,
        initiator_socket={bind='&router.target_socket'}},
    cpu={moduletype='cpu_riscv32',args={'&instance',0},resetvec=0xa4100ff8,
        mem={bind='&access.target_socket'}},
    rom={moduletype='gs_memory',read_only=true,dmi_allow=false,
        target_socket={address=0xa4100000,size=0x2000,bind='&router.initiator_socket'},
        load={{bin_file=root..'producer.bin',offset=0}}}}
