-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Append TZ AC controllers to an XBL cohort. Shared controllers keep the XBL order.
-- A shared address must use the same resource ID in both inventories.
return function(cohort, tz)
    assert(type(cohort)=='table' and type(tz)=='table', 'The XBL cohort and TZ inventory are required.')
    local by_address, by_resource = {}, {}
    for _,entry in ipairs(cohort) do
        by_address[entry[2]], by_resource[entry[1]] = entry[1], entry[2]
    end
    for _,entry in ipairs(tz) do
        local resource, address = entry[1], entry[2]
        if by_address[address] ~= nil or by_resource[resource] ~= nil then
            assert(by_address[address]==resource and by_resource[resource]==address,
                string.format('TZ controller 0x%x conflicts with XBL resource 0x%x.',
                    address, resource))
        else
            cohort[#cohort+1] = {resource, address}
            by_address[address], by_resource[resource] = resource, address
        end
    end
    return cohort
end
