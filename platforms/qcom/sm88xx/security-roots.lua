-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Apply declared key roots after the runtime composition creates their owners.
return function(platform, profile)
    assert(type(platform) == 'table', 'The platform table is required.')
    assert(type(profile) == 'table', 'The root profile table is required.')
    for _, item in ipairs({
        {'oem_hw_key_hex', 'gpce'},
        {'virtual_root_hex', 'tme_sequencer'},
    }) do
        local name, owner = item[1], item[2]
        local value = profile[name]
        if value ~= nil then
            assert(type(value) == 'string', 'The profile input ' .. name .. ' must be a string.')
            local module = platform[owner]
            if value ~= '' then
                assert(type(module) == 'table',
                    'The profile input ' .. name .. ' requires the ' .. owner .. ' model.')
            end
            if module then module[name] = value end
        end
    end
    return platform
end
