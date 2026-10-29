-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Shared reconstructed ROM composition. OEM tables remain in per-platform Lua data.
return function(platform, options, hardware)
    assert(type(options.tme)=='string', 'The reconstructed TME ROM path is required.')
    assert(type(options.tme_runtime_api_map)=='string', 'The verified runtime API map path is required.')
    assert(platform.runtime_abi==nil, 'The reconstructed runtime is already configured.')
-- The OEM security profile recommends OEM authorization and its gate.
-- Keep lifecycle and every other policy bit unchanged.
platform.fuses.oem_security_policy_lsb = platform.fuses.oem_security_policy_lsb | 0x832
platform.tme_rom.target_socket.size = 0x3c000
platform.tme_rom.load = {{bin_file=options.tme,
    offset=0,bin_file_offset=0,bin_file_size=0x3c000}}
local function memory(name, address, size, readonly, initial)
    platform[name] = {moduletype='gs_memory',init_mem=true,read_only=readonly or false,
        target_socket={address=address,size=size,bind='&tme_router.initiator_socket'}}
    if initial then platform[name].load={offset=0,data={['0']=initial}} end
end
memory('runtime_legacy_memory',0x460000,0x5000)
memory('runtime_private_memory',0x480000,0x20000)
memory('runtime_log_memory',0x474000,0x1000)
platform.runtime_abi = {moduletype='qcom,tme-runtime-abi-bridge-v1',
    legacy_base=0x416800,runtime_base=0x489000,enable_address=0x4a0000,
    legacy_channels=hardware.channels,runtime_channels=0x464200,
    mapping_path=options.tme_runtime_api_map,
    initiator_socket=platform.tme_access.initiator_socket}
platform.tme_access.initiator_socket = {bind='&runtime_abi.target_socket'}

-- Donor 0x213da0 maps the complete IMEM backing into this local view.
-- AOP image bytes and the former retained words must share that storage.
platform.runtime_imem = {moduletype='addrtr',mapped_base_addr=0,
    target_socket={address=0x18680000,size=0x2c000,relative_addresses=false,
        bind='&tme_router.initiator_socket'},
    initiator_socket={bind='&apss_imem.target_socket'}}
-- SM8845 run115 copies DCD bytes from local 0x04d22b00.
-- Preserve the complete existing SOCCP debug backing across both address views.
platform.runtime_soccp_debug_sram = {moduletype='addrtr',mapped_base_addr=0,
    target_socket={address=0x04d20000,size=0xc000,relative_addresses=false,
        bind='&tme_router.initiator_socket'},
    initiator_socket={bind='&soccp_debug_sram.target_socket'}}
-- Runtime 0x231670 translates CPUCP system SRAM by +0x04000000.
-- The XBL whitelist covers 0x40000 bytes, including secure SCMI slots above the ELF.
platform.runtime_cpucp_image = {moduletype='addrtr',mapped_base_addr=0,
    target_socket={address=0x21800000,size=0x40000,relative_addresses=false,
        bind='&tme_router.initiator_socket'},
    initiator_socket={bind='&cpucp_image.target_socket'}}

-- XBL 0x14850450 publishes the SMEM descriptor through the existing WONCE words.
-- The donor views reach the same publication and mutex, not separate storage.
platform.runtime_smem_info = {moduletype='addrtr',mapped_base_addr=0x01fd4000,
    target_socket={address=0x07ad4000,size=8,relative_addresses=false,
        bind='&tme_router.initiator_socket'},
    initiator_socket={bind='&apss_router.target_socket'}}
platform.runtime_smem_mutex = {moduletype='addrtr',mapped_base_addr=0x01f43000,
    target_socket={address=0x07a43000,size=4,relative_addresses=false,
        bind='&tme_router.initiator_socket'},
    initiator_socket={bind='&apss_router.target_socket'}}

-- The candidate scheduler uses CLIC and the architectural machine timer.
platform.tme.moduletype = 'qcom,tme-clic-cpu-v1'
platform.tme_clic = {moduletype='qcom,tme-clic-v1',priority_reset=1,
    pending_socket={address=0x01800000,size=0x41,bind='&tme_router.initiator_socket'},
    enable_socket={address=0x01800400,size=0x41,bind='&tme_router.initiator_socket'},
    priority_socket={address=0x01800800,size=0x41,bind='&tme_router.initiator_socket'},
    threshold_socket={address=0x01800c00,size=1,bind='&tme_router.initiator_socket'},
    source_socket={address=0x00c00100,size=0x20,bind='&tme_router.initiator_socket'},
    -- OEM QMP config supplies (-1,28,host7) to the direct IRQ registration.
    -- The virtual latch uses the guest's existing TMEC W1C acknowledgement.
    qmp_notify_socket={address=0x17960014,size=4,bind='&apss_router.initiator_socket'}}
for id=0,64 do platform.tme_clic['irq_' .. id]={bind='&tme.clic_irq_' .. id} end
-- CPH2771 0x41ef24 registers interrupt 23. No interrupt number is inferred from SM8850 offsets.
platform.tme_sequencer.irq = {bind='&tme_clic.source_23'}
platform.tme_sequencer.control_state_substitute = true
platform.tme_sequencer.debug_access_value = 0
platform.tme_sequencer.deny_debug_authorization = true
platform.tme_sequencer.deny_rom_patch_authorization = true
-- This profile binds policy inputs to real keys without physical root material.
platform.tme_sequencer.virtual_key_substitute = true
-- TME_KID_TP reaches the GPCE HWKM slave through the TPKEY receive window.
-- The GPCE object is resolved at end of elaboration, after qsee-gpce runs.
platform.tme_sequencer.transport_key_sink = 'gpce'
platform.tme_sequencer.ram_status_socket =
    {address=0x0228000c,size=4,bind='&tme_router.initiator_socket'}
-- Donor 0x2247a0 requests report-control retention, not an authorization grant.
platform.tme_sequencer.report_control_socket =
    {address=0x00c00210,size=4,bind='&tme_router.initiator_socket'}
-- Selected post-query summary reads both grant masks. Runtime policy denies both.
platform.tme_sequencer.grant_status_socket =
    {address=0x00c00224,size=8,bind='&tme_router.initiator_socket'}
platform.tme_timers.config_socket =
    {address=0x00c20000,size=0x1000,bind='&tme_router.initiator_socket'}
platform.runtime_machine_timer = {moduletype='riscv_aclint_mtimer',args={'&tme_instance'},
    construction_priority=1,hartid_base=0,num_harts=1,timecmp_base=0x4000,time_base=0xbff8,
    aperture_size=0x10000,timebase_freq=600000000,provide_rdtime=false,
    mem={address=0x01000000,size=0x10000,bind='&tme_router.initiator_socket'},
    timer_irq_0={bind='&tme.irq_in_7'}}

-- Candidate-only registers retain configuration. No external fault is injected.
for i,address in ipairs({0x00811000,0x008110d0,0x0f2d4600,0x0f2d4f00,
        0x0f2d4f14,0x0f2d4f28,0x0f2d4f3c,0x10210000,0x00c00204}) do
    memory('runtime_candidate_configuration_' .. i,address,4)
end
memory('runtime_configuration',0x0f2d4510,8)
memory('runtime_core_configuration',0x07ad2000,0x38)
for _, item in ipairs({{'debug_enable',0x102f0000},{'control',0x102f0004},{'status',0x102f1004}}) do
    platform['runtime_reset_' .. item[1]] = {moduletype='addrtr',mapped_base_addr=0,
        target_socket={address=item[2],size=4,relative_addresses=false,
            bind='&tme_router.initiator_socket'},
        initiator_socket={bind='&aoss_reset_fsm.' .. item[1] .. '_socket'}}
end
-- OEM 0x43c770 claims initialization. OEM 0x43c9ac publishes computed policy.
-- These words retain guest writes. They do not grant authorization.
for i,address in ipairs({0x04d42000,0x04d41000,0x04d50004,
        0x04d50008,0x04d5000c,0x04d5001c}) do
    local name = 'runtime_policy_publication_' .. i
    platform[name] = {moduletype='qcom,retained-boot-control-v1',
        writable_mask=0xffffffff,reset_value=0,
        target_socket={address=address,size=4,bind='&tme_router.initiator_socket'}}
    platform[name .. '_system'] = {moduletype='addrtr',mapped_base_addr=0,
        target_socket={address=address-0x04000000,size=4,relative_addresses=false,
            bind='&apss_router.initiator_socket'},
        initiator_socket={bind='&' .. name .. '.target_socket'}}
end
-- Both OEM maps reserve 0x1c..0x30 for selector two. Fixed groups end at 0x1b.
-- Virtual counts 21,5,5,5 keep all variable groups disjoint and below slot 64.
memory('runtime_resource_geometry',0x02237014,4,true,0x1c0294b5)
memory('runtime_resource_sizes',0x00c00300,0x10)
-- Both OEMs derive these CX/MX level indices from the published AOP command database.
-- SM8850 writes at 0x435e06/0x435ed8. SM8845 writes at 0x435e5e/0x435f30.
for i,address in ipairs({0x022370a8,0x022370ac}) do
    platform['runtime_voltage_levels_'..i]={moduletype='qcom,retained-boot-control-v1',
        writable_mask=0xffffffff,reset_value=0,
        target_socket={address=address,size=4,bind='&tme_router.initiator_socket'}}
end

-- PLL and branch addresses come from this package's code/data, not the SM8850 profile.
for i,address in ipairs(hardware.plls) do
    platform['runtime_pll_' .. i] = {moduletype='qcom,gcc-taycan-eko-t-pll-v1',reference_hz=19200000,
        target_socket={address=address,size=0x38,bind='&tme_router.initiator_socket'}}
end
platform.runtime_clock_reference = {moduletype='qcom,virtual-always-on-clock-v1'}
local branches = {}
for _,address in ipairs(hardware.branches) do branches[#branches+1]=address end
-- Candidate 0x222b9c and CPH2771 0x423156 use these four class-three branch pairs.
for _,address in ipairs({0x0c40105c,0x0c401054,0x0c40108c,0x0c401084,
        0x0b88105c,0x0b881054,0x0b88107c,0x0b881074}) do branches[#branches+1]=address end
for i,address in ipairs(branches) do
    platform['runtime_branch_' .. i] = {moduletype='qcom,clock-branch-v1',
        source_path='runtime_clock_reference',hardware_control=true,continuous_demand=true,
        target_socket={address=address,size=4,bind='&tme_router.initiator_socket'}}
end
-- CPH2771 0x42955e reads this part identifier. Its table includes 0x2b3 -> 0x294.
platform.runtime_part_identity = {moduletype='qcom,tcsr-soc-identity-v1',
    hardware_version=(platform.fuses.jtag_id >> 12) & 0xffff,
    emulation_type=0,sw_once_hardware_version=0,
    target_socket={address=0x131db014,size=4,bind='&tme_router.initiator_socket'}}
memory('runtime_tcsr_ownership',0x05fc7000,4)
memory('runtime_tcsr_violation_control',0x05fee000,4)
memory('runtime_tcsr_fault_masks',hardware.fault_masks[1],#hardware.fault_masks*4)

platform.runtime_protection = {moduletype='qcom,sm8850-tme-protection',
    region_count=64,address_bits=32,alignment_bits=12,mode_reset=0,qad_count=11,
    target_socket={address=0x10292000,size=0x2000,bind='&tme_router.initiator_socket'}}
for _,entry in ipairs({
    {'apss_protection_port','apss',0,0x0b000000},
    {'cpucp_protection_port','cpucp',0,0x0b000000},
    {'tme_protection_port','tme',1,0x0f000000},
    {'sequencer_dma_protection','tme',1,0x0b000000},
    {'ufs_dma_protection','apss',0,0x0b000000}}) do
    local name=entry[1] .. '_runtime'
    platform[name] = {moduletype='qcom,tme-protection-port-v1',
        args={'&runtime_protection','&' .. entry[2]},qad=entry[3],
        secure_unspecified=entry[2]=='tme',protected_base=entry[4],protected_size=0x04000000,
        initiator_socket=platform[entry[1]].initiator_socket}
    platform[entry[1]].initiator_socket = {bind='&' .. name .. '.target_socket'}
end
platform.tme_protection1.qad_count=11
platform.tme_protection1.region_count=1024
platform.tme_protection1.target_socket.size=0x11000
platform.tme_protection2.qad_count=11
platform.tme_protection2.region_count=64
platform.runtime_global_protection = {moduletype='addrtr',mapped_base_addr=0x05f00000,
    target_socket={address=0x07a00000,size=0x11000,relative_addresses=false,
        bind='&tme_router.initiator_socket'},initiator_socket={bind='&tme_router.target_socket'}}

local function local_address(address)
    if address < 0x1ef00000 then return address+0x04000000 end
    if address >= 0x20000000 and address < 0x2230c000 then return address-0x20000000 end
    return address
end
local controllers = {
    [0x05f00000]=platform.tme_protection1,
    [0x00c28000]=platform.tme_protection2,
    [0x10292000]=platform.runtime_protection}
local function protection(address, name)
    if not controllers[address] then
        platform[name] = {moduletype='qcom,sm8850-tme-protection',
            region_count=64,address_bits=32,alignment_bits=12,qad_count=11,revision=0x40000000,
            target_socket={address=address,size=0x2000,bind='&tme_router.initiator_socket'}}
        controllers[address] = platform[name]
    end
    return controllers[address]
end
for i,address in ipairs(hardware.clients) do
    protection(local_address(address),'runtime_client_protection_' .. i)
end
-- Signed configuration: 31 permission descriptors, but only 30 DBGAR addresses.
-- These arrays differ. Do not zip their indices or read the adjacent count as an address.
local function configure_protection(record, name)
    local controller=protection(local_address(record.address),name)
    if record.fixed then controller.moduletype='qcom,xpu-fixed-resources-v1' end
    controller.region_count=math.max(controller.region_count,record.regions)
    controller.address_bits=record.address_bits
    controller.alignment_bits=record.alignment_bits
    if record.aperture_size then controller.aperture_size=record.aperture_size end
    controller.target_socket.size=math.max(controller.target_socket.size,
        0x1000+controller.region_count*0x40)
end
for i,record in ipairs(hardware.protection) do
    configure_protection(record,'runtime_descriptor_protection_'..i)
end
for i,record in ipairs(hardware.xbl_protection) do
    configure_protection(record,'xbl_descriptor_protection_'..i)
end
-- QSEE AC policy arrays select controller type and capacity. See tz-inventory-evidence.md.
for i,record in ipairs(assert(hardware.tz_protection, 'The TZ protection supplement is required.')) do
    configure_protection(record,'tz_descriptor_protection_'..i)
end
for i,address in ipairs(hardware.debug_addresses) do
    local mapped=local_address(address)
    protection(mapped-0x304,'runtime_debug_protection_' .. i)
    platform['runtime_debug_dma_' .. i] = {moduletype='addrtr',mapped_base_addr=mapped,
        target_socket={address=address,size=4,relative_addresses=false,
            bind='&sequencer_dma_router.initiator_socket'},
        initiator_socket={bind='&tme_router.target_socket'}}
end
-- OEM RGACR getters 0x427616 and 0x427552 derive configuration capability four.
for _,controller in pairs(controllers) do
    controller.config_owner_mask=4
    controller.config_secure_unspecified=true
end
platform.runtime_global_protection.target_socket.size=platform.tme_protection1.target_socket.size
for _,name in ipairs({'tme_protection_port','tme_local_protection','tme_protection_port_runtime',
        'sequencer_dma_protection','sequencer_local_protection','sequencer_dma_protection_runtime'}) do
    platform[name].runtime_qad=2
    platform[name].runtime_phase={bind='&runtime_abi.runtime_active'}
end

-- CPH2771 OEM state registers. Configuration storage has no invented completion effect.
assert(hardware.external_fault_descriptor[1]==0x00c0040c and
    hardware.external_fault_descriptor[2]==0x00c00410 and
    hardware.external_fault_descriptor[3]==0x00c00408)
memory('runtime_external_fault',0x00c00408,0xc)
memory('runtime_fault_enable',0x05e12060,4)
for i,address in ipairs({0x3119c008,0x3111e008}) do
    memory('runtime_startup_control_' .. i,address,4)
end
platform.runtime_watchdog = {moduletype='qcom,tme-watchdog-v1',clock_hz=32000,
    target_socket={address=0x00c24000,size=0x14,bind='&tme_router.initiator_socket'},
    bark={bind='&tme_clic.source_27'}}
memory('runtime_watchdog_secondary_control',0x10264004,4)
-- OEM policy row and selected FEC/access checks share the authoritative fuse state.
platform.fuses.corrected_policy_socket =
    {address=0x02302090,size=0x18,bind='&tme_router.initiator_socket'}
for _,field in ipairs({'antirollback0','antirollback17','antirollback18','antirollback22','antirollback23'}) do
    assert(platform.fuses[field]==0,'The corrected view requires zero rollback minima')
end
memory('runtime_corrected_antirollback',0x021c2220,0x70,true)
memory('runtime_corrected_rollback_tail',0x021c2290,8,true)
for bank=0,1 do memory('runtime_pdc_config_' .. bank,0x0f2d0110+bank*0x10000,0x80) end
-- Both OEM descriptors447b0c use protocol0/local ordinal16, not a private ID word.
platform.runtime_tme_ipcc = {moduletype='addrtr',mapped_base_addr=0,
    target_socket={address=0x05110000,size=0x1000,relative_addresses=false,
        bind='&tme_router.initiator_socket'},
    initiator_socket={bind='&tme_ipcc.target_socket'}}
memory('runtime_ipcc_common_config',0x051cf008,4)
-- Initializer440a1a writes this distinct common-control word. Preserve one backing.
platform.runtime_ipcc_common_config_apss = {moduletype='addrtr',mapped_base_addr=0,
    target_socket={address=0x011cf008,size=4,relative_addresses=false,
        bind='&apss_router.initiator_socket'},
    initiator_socket={bind='&runtime_ipcc_common_config.target_socket'}}
memory('runtime_queue_status',0x00c04fe4,4)
    return platform
end
