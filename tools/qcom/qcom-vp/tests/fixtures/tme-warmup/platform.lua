-- SPDX-License-Identifier: BSD-3-Clause-Clear
local root=assert(os.getenv('QCOM_VP_PROBE_ROOT'))..'/'
local case=0
local file=io.open(root..'case.txt','r')
if file then case=tonumber(file:read('*l')) or 0 file:close() end
local sequencer={moduletype='qcom,tme-sequencer-v1',policy_selection=0,
    virtual_key_substitute=(case~=2),
    target_socket={address=0x02230000,size=0x168,bind='&apss_router.initiator_socket'},
    policy_socket={address=0x00c00200,size=4,bind='&apss_router.initiator_socket'},
    input_socket={address=0x02281000,size=0x1000,bind='&apss_router.initiator_socket'},
    output_socket={address=0x02289000,size=0x1000,bind='&apss_router.initiator_socket'}}
-- Case three omits the HWKM slave. The broadcast then has no destination.
if case~=3 then sequencer.transport_key_sink='gpce' end
platform={moduletype='Container',qemu_inst_mgr={moduletype='QemuInstanceManager'},
    apss_instance={moduletype='QemuInstance',args={'&qemu_inst_mgr','AARCH64'},accel='tcg',
        sync_policy='multithread-quantum'},
    apss_router={moduletype='router'},
    apss_access={moduletype='AccessMonitor',initiator_socket={bind='&apss_router.target_socket'}},
    apss={moduletype='cpu_arm_cortexA53',args={'&apss_instance'},rvbar=0x300000,
        has_el3=true,has_el2=true,mem={bind='&apss_access.target_socket'}},
    rom={moduletype='gs_memory',read_only=true,
        target_socket={address=0x300000,size=0x20000,bind='&apss_router.initiator_socket'},
        load={{bin_file=root..'producer.bin',offset=0}}},
    stack={moduletype='gs_memory',
        target_socket={address=0x401000,size=0x1000,bind='&apss_router.initiator_socket'}},
    sequencer=sequencer,
    -- The GPCE stream socket needs one endpoint. This BAM supplies it without a DMA path.
    crypto_bam={moduletype='qcom,bam-v2.0.0',num_pipes=20,num_ees=4,revision=0x20,
        address_bits=36,
        target_socket={address=0x01dc4000,size=0x20000,
            bind='&apss_router.initiator_socket'},
        stream_socket={bind='&gpce.stream_socket'}},
    gpce={moduletype='qcom,qce-v6',
        stream_ready={bind='&crypto_bam.stream_ready'},
        target_socket={address=0x01df0000,size=0x10000,
            bind='&apss_router.initiator_socket'}}}
