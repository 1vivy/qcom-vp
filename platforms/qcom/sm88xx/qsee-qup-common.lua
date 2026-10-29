-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- QSEE QUPV3 common wrappers and serial engines that the selected
-- QUPV3_HW_PROP and devcfg permission sets declare.
-- SM8850 lists QUP_1C..QUP_4C and one i2chub common block. SM8845 lists
-- QUP_1C..QUP_3C only.
-- The XBL console and firmware composition already owns the wrappers in its
-- ac_fw records: 1 through 3 on SM8850, and 1 and 3 on SM8845.
-- QSEE reads the 32-bit GENI common version at QUP wrapper+4 for every
-- declared wrapper and requires all values to match.
-- The SE-config prologue also sets bit zero at +0x118, +0x120, and +0x21c on
-- every common block it receives, including the i2chub common block.
-- The existing qcom,geni-common-v1 model owns those controls and the version
-- word with its declared virtual revision.
-- Engine bases come from the selected tz_qti_config hardware arrays. Only
-- devcfg permission entries with the enable bit are declared.
local profiles={
    sm8850={
        common={{name='qup_common_4',base=0x001ac0000},
            {name='i2chub_common',base=0x0009c0000}},
        engines={
            {qup=1,se=0,base=0x00a80000},{qup=1,se=1,base=0x00a84000},
            {qup=1,se=2,base=0x00a88000},{qup=1,se=4,base=0x00a90000},
            {qup=1,se=5,base=0x00a94000},
            {qup=2,se=0,base=0x00880000},{qup=2,se=1,base=0x00884000},
            {qup=2,se=4,base=0x00890000},
            {qup=3,se=1,base=0x01984000},{qup=3,se=2,base=0x01988000},
            {qup=3,se=3,base=0x0198c000},{qup=3,se=4,base=0x01990000},
            {qup=3,se=5,base=0x01994000},
            {qup=4,se=0,base=0x01a80000},{qup=4,se=1,base=0x01a84000},
            {qup=4,se=2,base=0x01a88000},{qup=4,se=3,base=0x01a8c000},
        },
        -- Selected i2chub_perms IDs 0xa8 and 0xa9.
        hubs={{name='i2chub_se0',base=0x00980000},
            {name='i2chub_se1',base=0x00984000}},
    },
    sm8845={
        common={{name='qup_common_2',base=0x0008c0000}},
        engines={
            {qup=1,se=0,base=0x00a80000},{qup=1,se=1,base=0x00a84000},
            {qup=1,se=2,base=0x00a88000},{qup=1,se=4,base=0x00a90000},
            {qup=2,se=0,base=0x00880000},{qup=2,se=1,base=0x00884000},
            {qup=2,se=2,base=0x00888000},{qup=2,se=3,base=0x0088c000},
            {qup=2,se=4,base=0x00890000},{qup=2,se=6,base=0x00898000},
            {qup=3,se=0,base=0x01980000},{qup=3,se=1,base=0x01984000},
            {qup=3,se=2,base=0x01988000},{qup=3,se=3,base=0x0198c000},
        },
        hubs={},
    },
}
return function(p,oem)
    local profile=assert(profiles[oem], 'A selected QSEE QUP common profile is required.')
    assert(p.apss_router, 'The APSS router is required for the QSEE QUP common wrappers.')
    local bind='&apss_router.initiator_socket'
    local function owner_at(base,size)
        for owner,module in pairs(p) do
            if type(module)=='table' then
                for _,socket in pairs(module) do
                    if type(socket)=='table' and socket.bind==bind and
                        type(socket.address)=='number' and type(socket.size)=='number' and
                        base<socket.address+socket.size and socket.address<base+size then
                        return owner,module,socket
                    end
                end
            end
        end
    end
    for _,entry in ipairs(profile.common) do
        assert(p[entry.name]==nil, 'Duplicate QUP common owner: '..entry.name)
        local owner=owner_at(entry.base,0x1000)
        if owner then
            error(string.format('QSEE QUP common 0x%x overlaps %s',entry.base,owner))
        end
        p[entry.name]={moduletype='qcom,geni-common-v1',
            target_socket={address=entry.base,size=0x1000,bind=bind}}
    end
    local function engine(name,base)
        local owner,module,socket=owner_at(base,0x3000)
        if owner then
            assert(module.moduletype=='qcom,geni-se-v1' and socket.address==base,
                string.format('QSEE SE 0x%x overlaps %s',base,owner))
            return
        end
        assert(p[name]==nil, 'Duplicate GENI SE owner: '..name)
        p[name]={moduletype='qcom,geni-se-v1',
            se_socket={address=base,size=0x3000,bind=bind}}
    end
    for _,entry in ipairs(profile.engines) do
        engine('qup'..entry.qup..'_se'..entry.se,entry.base)
    end
    for _,entry in ipairs(profile.hubs) do
        engine(entry.name,entry.base)
    end
    return p
end
