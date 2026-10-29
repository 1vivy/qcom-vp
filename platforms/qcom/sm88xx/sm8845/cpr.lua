-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Selected XBL getter 0x14849ea8 returns settings 0x14915eb8.
-- Controller pointers: 0x14915560, nine entries. Rail pointers: 0x149159a8.
-- Each selected thread has ID zero. Firmware supplies all targets and masks.
local controllers = {
    {name='mss', base=0x2b580000, sensor_count=29, thread_count=1},
    {name='cx', base=0x00628000, sensor_count=37, thread_count=1},
    {name='gfx', base=0x03d9a000, sensor_count=37, thread_count=1},
    {name='nsp2', base=0x00634000, sensor_count=4, thread_count=1},
    {name='lpi_cx', base=0x06e70000, sensor_count=4, thread_count=1},
    {name='mm', base=0x00630000, sensor_count=33, thread_count=1},
    {name='mx', base=0x00638000, sensor_count=34, thread_count=1},
    {name='mxc', base=0x0062c000, sensor_count=64, thread_count=1},
    {name='nsp', base=0x0063c000, sensor_count=9, thread_count=1},
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
    return p
end
