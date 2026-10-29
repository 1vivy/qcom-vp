-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- The supplied PBL and TME ROMs establish this reset composition.
local directory = assert(debug.getinfo(1, 'S').source:match('^@(.*/)'))
local reset = dofile(directory .. '../reset.lua')
return function(options)
    local platform = reset(options)
    if options.tme_runtime_api_map then
        dofile(directory .. 'runtime.lua')(platform, options)
    end
    return platform
end
