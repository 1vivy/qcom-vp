-- SPDX-License-Identifier: BSD-3-Clause-Clear
local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
local p={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'},
    apss_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','AARCH64'},accel='tcg',
        sync_policy='multithread-quantum',qemu_args='-d in_asm -D '..root..'apss.log'},
    execution={moduletype='qcom,apss-execution-group-v1',args={'&apss_instance'}},
    apss_router={moduletype='router'},
    cpu_router={moduletype='router'},
    apss_access={moduletype='AccessMonitor',
        target_socket={address=0,size=0x1000000000000,relative_addresses=false,
            bind='&cpu_router.initiator_socket'},
        initiator_socket={bind='&apss_router.target_socket'}},
    writer_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','AARCH64'},accel='tcg',
        sync_policy='multithread-quantum'},
    writer_router={moduletype='router'},
    writer_access={moduletype='AccessMonitor',initiator_socket={bind='&writer_router.target_socket'}},
    writer={moduletype='cpu_arm_cortexA53',args={'&writer_instance'},rvbar=0x400000,
        has_el3=true,mem={bind='&writer_access.target_socket'}},
    producer_rom={moduletype='gs_memory',read_only=true,
        target_socket={address=0x300000,size=0x1000,bind='&apss_router.initiator_socket'},
        load={{bin_file=root..'producer.bin',offset=0}}},
    writer_rom={moduletype='gs_memory',read_only=true,
        target_socket={address=0x400000,size=0x1000,bind='&writer_router.initiator_socket'},
        load={{bin_file=root..'writer.bin',offset=0}}},
    flags={moduletype='gs_memory',init_mem=true,
        target_socket={address=0x14800000,size=0x1000,bind='&apss_router.initiator_socket'}},
    code={moduletype='qcom,shared-executable-memory-v1',args={'&execution'},init_mem=true,
        target_socket={address=0x80000000,size=0x1000,bind='&apss_router.initiator_socket'}},
}
local affinities={0x00000,0x00100,0x00200,0x00300,0x00400,0x00500,0x10000,0x10100}
local cpus={}
for i=0,7 do
    cpus[i+1]='cpu'..i
    -- This fixture starts CPU7 directly to isolate sibling-only code invalidation.
    -- This fixture does not test the power-controller reset release.
    p['cpu'..i]={moduletype='qcom,apss-cpu-v1',args={'&apss_instance','&execution'},
        group_index=i,mp_affinity=affinities[i+1],rvbar=0x300000,
        initial_reset=i~=0 and i~=7,start_powered_off=false,psci_conduit='disabled',
        has_el3=true,has_el2=true,mem={bind='&cpu_router.target_socket'}}
end
for name,address in pairs({flags=0x14800000,code=0x80000000}) do
    p[name..'_external']={moduletype='addrtr',mapped_base_addr=address,
        target_socket={address=address,size=0x1000,relative_addresses=false,
            bind='&writer_router.initiator_socket'},
        initiator_socket={bind='&apss_router.target_socket'}}
end
dofile(assert(os.getenv('QCOM_VP_PLATFORM_DIR'))..'/qcom/apss-gic.lua')(p,
    {cpus=cpus,group='execution',instance='apss_instance',router='apss_router'})
platform=p
