-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- The clock branch model has four fixed vote slots.
-- The count matches QcomClockBranch::vote_slot_count.
-- Composition appends one independent owner to the next free slot.
local slots=4

return function(branch,path,offset,mask,address)
    assert(type(branch)=='table' and branch.moduletype=='qcom,clock-branch-v1',
        'A clock vote requires an existing clock branch')
    assert(type(address)=='number',
        'A clock vote requires the branch target address')
    assert(type(path)=='string' and path~='',
        string.format('Clock branch 0x%x requires a vote bank',address))
    assert(type(offset)=='number' and offset>=0 and offset<=24 and offset%8==0,
        string.format('Clock branch 0x%x has an unsupported vote offset',address))
    assert(type(mask)=='number' and mask>0,
        string.format('Clock branch 0x%x requires a non-zero vote mask',address))
    -- A repeated identical owner is idempotent.
    for slot=0,slots-1 do
        local owner=branch['vote_path_'..slot]
        if owner==nil then
            branch['vote_path_'..slot]=path
            branch['vote_offset_'..slot]=offset
            branch['vote_mask_'..slot]=mask
            return branch
        end
        local existing_offset=branch['vote_offset_'..slot] or 0
        local existing_mask=branch['vote_mask_'..slot] or 0
        if owner==path and existing_offset==offset then
            if existing_mask==mask then return branch end
            assert((existing_mask & mask)==0,
                string.format('Clock branch 0x%x already has vote owner %s offset 0x%x',
                    address,path,offset))
        end
    end
    error(string.format('Clock branch 0x%x has no free vote slot',address))
end
