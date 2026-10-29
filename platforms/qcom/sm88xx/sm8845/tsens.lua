-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Selected CPH2771 xbl_config.elf DTBs at file 0x30c and 0xf52c agree on this group.
return function(p)
    assert(p.apss_router, 'TSENS requires the APSS router.')
    local counts = {5, 12, 7, 4, 5, 8, 8}
    local ranges = {{0x0c222000, 0xe000}, {0x221c2d18, 4}}
    for name, module in pairs(p) do
        if type(module) == 'table' then
            for _, socket in pairs(module) do
                if type(socket) == 'table' and socket.bind == '&apss_router.initiator_socket'
                    and type(socket.address) == 'number' and type(socket.size) == 'number' then
                    for _, range in ipairs(ranges) do
                        assert(socket.address >= range[1] + range[2]
                            or socket.address + socket.size <= range[1], 'TSENS overlaps APSS target ' .. name)
                    end
                end
            end
        end
    end
    for i, count in ipairs(counts) do
        local name = 'tsens_' .. (i - 1)
        assert(p[name] == nil, 'Duplicate TSENS controller.')
        p[name] = {
            moduletype = 'qcom,tsens-v2', sensor_count = count,
            conversion_us = 30, temperature_decic = 250,
            srot_socket = {address = 0x0c222000 + (i - 1) * 0x1000, size = 0x1000,
                bind = '&apss_router.initiator_socket'},
            tm_socket = {address = 0x0c229000 + (i - 1) * 0x1000, size = 0x1000,
                bind = '&apss_router.initiator_socket'},
        }
    end
    assert(p.tsens_calibration == nil, 'Duplicate TSENS calibration profile.')
    -- Selector zero means no calibration. XBL calculates coefficients from DT defaults.
    -- This is a virtual board profile, not a captured fuse value.
    p.tsens_calibration = {moduletype = 'qcom,tsens-calibration-profile-v1',
        target_socket = {address = 0x221c2d18, size = 4, bind = '&apss_router.initiator_socket'}}
    return p
end
