-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Selected XBL getter 0x1484ba20 returns settings 0x14915458.
-- Controller pointers: 0x149145a0, ten entries. Rail pointers: 0x14914a30.
-- Each selected thread has ID zero. Firmware supplies all targets and masks.
local controllers = {
    {name='mss', base=0x2b580000, sensor_count=29, thread_count=1},
    {name='cx', base=0x00628000, sensor_count=50, thread_count=1},
    {name='gfx', base=0x03d9a000, sensor_count=41, thread_count=1},
    {name='mxg', base=0x03d8a000, sensor_count=39, thread_count=1},
    {name='nsp2', base=0x00634000, sensor_count=8, thread_count=1},
    {name='lpi_cx', base=0x06e70000, sensor_count=4, thread_count=1},
    {name='mm', base=0x00630000, sensor_count=34, thread_count=1},
    {name='mx', base=0x00638000, sensor_count=74, thread_count=1},
    {name='mxc', base=0x0062c000, sensor_count=39, thread_count=1},
    {name='nsp', base=0x0063c000, sensor_count=10, thread_count=1},
}

return function(p)
    assert(p.apss_router, 'The APSS router is required for CPR controllers.')
    local size = 0x4000
    local bind = '&apss_router.initiator_socket'
    local pending = {}
    -- Check the complete group before adding any controller.
    for _, row in ipairs(controllers) do
        local name = 'cpr_'..row.name
        assert(p[name] == nil, 'Duplicate CPR controller module: '..name)
        for _, other in ipairs(pending) do
            assert(row.base >= other.base + size or other.base >= row.base + size,
                'Overlapping CPR controller inventory: '..name)
        end
        for owner, module in pairs(p) do
            if type(module) == 'table' then
                for _, target in pairs(module) do
                    if type(target) == 'table' and target.bind == bind
                        and type(target.address) == 'number' and type(target.size) == 'number'
                        and row.base < target.address + target.size
                        and target.address < row.base + size then
                        error(string.format('CPR controller 0x%x overlaps %s in apss_router', row.base, owner))
                    end
                end
            end
        end
        pending[#pending + 1] = row
    end
    for _, row in ipairs(controllers) do
        p['cpr_'..row.name] = {
            moduletype='qcom,cpr3-controller-v1',
            sensor_count=row.sensor_count,
            thread_count=row.thread_count,
            target_socket={address=row.base, size=size, bind=bind},
        }
    end
    -- The RPMH CPRF block is at AOSS+0x01200000.
    -- AOSS starts at 0x0b000000, so the block is 0x0c200000.
    -- UEFI CPRDxe RVA 0xc1b0 and 0xc1b4 write a rail's voltage limits and its
    -- safe voltage. RVA 0xc22c sets CTRL_INTERACTION_EN, RVA 0xc474 writes the
    -- VRM resource, and RVA 0xc49c sets ENABLE. The block keeps these words and
    -- applies no voltage change, because CPRDxe meets ideal CPR sensors and no
    -- PMIC regulator on this platform.
    local cprf_base, cprf_size = 0x0c200000, 0x10000
    assert(p.cprf == nil, 'Duplicate CPRF module')
    for owner, module in pairs(p) do
        if type(module) == 'table' then
            for _, target in pairs(module) do
                if type(target) == 'table' and target.bind == bind
                    and type(target.address) == 'number' and type(target.size) == 'number'
                    and cprf_base < target.address + target.size
                    and target.address < cprf_base + cprf_size then
                    error(string.format('CPRF 0x%x overlaps %s in apss_router', cprf_base, owner))
                end
            end
        end
    end
    p.cprf = {moduletype='qcom,rpmh-cprf-v1',
        target_socket={address=cprf_base, size=cprf_size, bind=bind}}
    return p
end
