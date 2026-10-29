-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Selected /soc/ac_fw records from RADIO/xbl_config.img and selected TZ GSI
-- tables. SM8850 FDT file offset 0x10e14. SM8845 FDT file offset 0xf52c.
-- The protocol labels describe guest selection, not implemented transfers.
local profiles={
    sm8850={firmware_size=0x5248,gsis={1,2,3,4},engines={
        {qup=1,se=5,base=0x00a94000,protocol=3,is_axi=1},
        {qup=1,se=6,base=0x00a98000,protocol=0xd,is_axi=1},
        {qup=2,se=2,base=0x00888000,protocol=2,is_axi=0},
        {qup=1,se=3,base=0x00a8c000,protocol=3,is_axi=1},
        {qup=3,se=0,base=0x01980000,protocol=3,is_axi=0},
    }},
    sm8845={firmware_size=0x5230,gsis={1,2,3},engines={
        {qup=1,se=3,base=0x00a8c000,protocol=3,is_axi=0},
        {qup=1,se=5,base=0x00a94000,protocol=0xd,is_axi=0},
        {qup=1,se=6,base=0x00a98000,protocol=2,is_axi=0},
        {qup=3,se=4,base=0x01990000,protocol=3,is_axi=0},
    }},
}
local wrappers={
    [1]={common=0x00ac0000,gsi=0x00a04000},
    [2]={common=0x008c0000,gsi=0x00804000},
    [3]={common=0x019c0000,gsi=0x01904000},
    [4]={common=0x01ac0000,gsi=0x01a04000},
}
return function(p,soc)
    local profile=assert(profiles[soc], 'The selected QUP firmware image is required.')
    assert(p.apss_router and p.uart and p.qup_common_1, 'The console wrapper must exist before QUP firmware wiring.')
    local bind='&apss_router.initiator_socket'
    local selected={}
    for _,engine in ipairs(profile.engines) do
        local wrapper=wrappers[engine.qup]
        if not selected[engine.qup] then
            local common='qup_common_'..engine.qup
            if engine.qup==1 then
                assert(p[common].moduletype=='qcom,geni-common-v1' and
                    p[common].target_socket.address==wrapper.common and
                    p[common].target_socket.bind==bind, 'The console must share QUP1 common state.')
            else
                assert(p[common]==nil, 'Duplicate QUP common model.')
                p[common]={moduletype='qcom,geni-common-v1',
                    target_socket={address=wrapper.common,size=0x1000,bind=bind}}
            end
            selected[engine.qup]=true
        end
        local name='qup'..engine.qup..'_se'..engine.se
        assert(p[name]==nil, 'Duplicate GENI firmware engine.')
        p[name]={moduletype='qcom,geni-se-v1',
            se_socket={address=engine.base,size=0x3000,bind=bind}}
    end
    -- The selected TZ GSI table lists one instance per wrapper. SM8850
    -- 0x157f1ab8 lists four, and SM8845 0x157e5078 lists three. The SM8850
    -- QSI at 0x82461000 is 0x52b0 bytes and 0xa49 instruction words. The
    -- SM8845 QSI is 0x5298 bytes and 0xa46 instruction words. Control
    -- mapping ends before GPII.
    for _,qup in ipairs(profile.gsis) do
        local wrapper=wrappers[qup]
        local gsi='qup_gsi_'..qup
        assert(p[gsi]==nil, 'Duplicate QUP GSI model.')
        p[gsi]={moduletype='qcom,gsi-v1',firmware_size=profile.firmware_size,
            target_socket={address=wrapper.gsi,size=0x10000,bind=bind},
            firmware_socket={address=wrapper.gsi+0x4c000,size=0x6000,bind=bind}}
    end
    -- XBL resolves existing SE clocks itself. Programming needs no transfer IRQ.
    -- No firmware, protocol revision, access permission, or ready value is seeded.
    return p
end
