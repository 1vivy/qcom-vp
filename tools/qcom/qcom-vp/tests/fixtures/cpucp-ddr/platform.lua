-- Synthetic guests only. Exercise the production DDR helper without native firmware.
local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
local platforms=assert(os.getenv('QCOM_VP_PLATFORM_DIR'))..'/'
local soc=dofile(root..'policy.lua')
assert(soc=='sm8850' or soc=='sm8845')
local p={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'}}
for name,arch in pairs({apss='AARCH64',cpucp='RISCV64'}) do
    p[name..'_instance']={moduletype='QemuInstance',args={'&qemu_inst_mgr',arch},accel='tcg',
        sync_policy='multithread-quantum',qemu_args='-d in_asm -D '..root..name..'.log'}
    p[name..'_router']={moduletype='router'}
    p[name..'_access']={moduletype='AccessMonitor',allow_unspecified=arch~='AARCH64',
        initiator_socket={bind='&'..name..'_router.target_socket'}}
end
p.apss_group={moduletype='qcom,apss-execution-group-v1',args={'&apss_instance'},cpu_count=1}
p.apss={moduletype='qcom,apss-cpu-v1',args={'&apss_instance','&apss_group'},rvbar=0x300000,
    has_el3=true,has_el2=true,mem={bind='&apss_access.target_socket'}}
p.cpucp={moduletype='cpu_riscv64',args={'&cpucp_instance',0},resetvec=0x334000,
    mem={bind='&cpucp_access.target_socket'}}
for name,address in pairs({apss=0x300000,cpucp=0x334000}) do
    p[name..'_rom']={moduletype='gs_memory',read_only=true,
        target_socket={address=address,size=0x1000,bind='&'..name..'_router.initiator_socket'},
        load={{bin_file=root..name..'.bin',offset=0}}}
end
p.cpucp_ram={moduletype='gs_memory',init_mem=true,
    target_socket={address=0x18b30000,size=0x40000,bind='&cpucp_router.initiator_socket'}}
p.cpucp_image={moduletype='qcom,shared-executable-memory-v1',args={'&cpucp'},init_mem=true,
    target_socket={address=0x1d800000,size=0x40000,bind='&apss_router.initiator_socket'}}
p.cpucp_image_view={moduletype='addrtr',mapped_base_addr=0x1d800000,
    target_socket={address=0x1d800000,size=0x40000,relative_addresses=false,
        bind='&cpucp_router.initiator_socket'},
    initiator_socket={bind='&apss_router.target_socket'}}
p.cpucp_boot={moduletype='qcom,cpucp-boot-control-v1',args={'&cpucp'},hart_index=0,
    reset={bind='&cpucp.reset'},
    target_socket={address=0x17c40000,size=0x64,bind='&apss_router.initiator_socket'},
    reset_socket={address=0x17c42000,size=4,bind='&apss_router.initiator_socket'},
    setup_socket={address=0x18877600,size=0x90,bind='&apss_router.initiator_socket'}}
-- These names satisfy the SM8845 helper's SRAM prerequisites. No SRAM probe uses them.
for name,address in pairs({apss_sram=0x14800000,shrm_iram=0x31000000,shrm_dmem=0x31040000}) do
    p[name]={moduletype='gs_memory',init_mem=true,
        target_socket={address=address,size=0x1000,bind='&apss_router.initiator_socket'}}
end
for index,name in ipairs({'ufs_ram_router','sequencer_dma_router'}) do
    p[name]={moduletype='router'}
    -- The synthetic APSS guest drives each DMA router, not a fabricated DMA engine.
    p[name..'_probe']={moduletype='addrtr',mapped_base_addr=0x80000000,
        target_socket={address=index*0x100000000,size=0x80000000,relative_addresses=false,
            bind='&apss_router.initiator_socket'},
        initiator_socket={bind='&'..name..'.target_socket'}}
end
dofile(platforms..'qcom/sm88xx/llcc.lua')(p,soc)
platform=dofile(platforms..'qcom/sm88xx/'..soc..'/ddr.lua')(p)
