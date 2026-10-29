-- SPDX-License-Identifier: BSD-3-Clause-Clear
local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
local p={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'}}
for name,arch in pairs({producer='AARCH64',owner='RISCV32',consumer='RISCV32'}) do
    local router=name=='owner' and 'apss' or name
    p[name..'_instance']={moduletype='QemuInstance',args={'&qemu_inst_mgr',arch},accel='tcg',
        sync_policy='multithread-quantum',qemu_args='-d in_asm -D '..root..name..'.log'}
    p[router..'_router']={moduletype='router'}
    p[name..'_access']={moduletype='AccessMonitor',allow_unspecified=arch~='AARCH64',
        initiator_socket={bind='&'..router..'_router.target_socket'}}
end
p.producer={moduletype='cpu_arm_cortexA53',args={'&producer_instance'},rvbar=0x300000,
    has_el3=true,has_el2=true,mem={bind='&producer_access.target_socket'}}
for _,name in ipairs({'owner','consumer'}) do
    p[name]={moduletype='cpu_riscv32',args={'&'..name..'_instance',0},resetvec=0x200000,
        mem={bind='&'..name..'_access.target_socket'}}
end
for name,address in pairs({producer=0x300000,owner=0x200000,consumer=0x200000}) do
    local router=name=='owner' and 'apss' or name
    p[name..'_rom']={moduletype='gs_memory',read_only=true,
        target_socket={address=address,size=0x1000,bind='&'..router..'_router.initiator_socket'},
        load={{bin_file=root..name..'.bin',offset=0}}}
end
p.shared={moduletype='qcom,shared-executable-memory-v1',args={'&owner'},
    execution_participant='consumer',init_mem=true,
    target_socket={address=0x80000000,size=0x3000}}
p.dare={moduletype='qcom,llcc-dare-port-v1',args={'&llcc','&owner'},
    execution_participant='consumer',physical_base=0x80000000,memory_size=0x3000,
    target_socket={address=0x80000000,size=0x3000,bind='&apss_router.initiator_socket'},
    initiator_socket={bind='&shared.target_socket'}}
p.consumer_ddr={moduletype='addrtr',mapped_base_addr=0,
    target_socket={address=0x80000000,size=0x3000,relative_addresses=false,
        bind='&consumer_router.initiator_socket'},
    initiator_socket={bind='&dare.target_socket'}}
p.producer_ddr={moduletype='addrtr',mapped_base_addr=0x80000000,
    target_socket={address=0x20400000,size=0x3000,relative_addresses=false,
        bind='&producer_router.initiator_socket'},
    initiator_socket={bind='&apss_router.target_socket'}}
platform=dofile(assert(os.getenv('QCOM_VP_PLATFORM_DIR'))..'/qcom/sm88xx/llcc.lua')(p,'sm8850')
