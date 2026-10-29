-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Selected xbl_config DTs declare one tile at 0x0f100000 with 0x1000 stride.
-- GPIO count: Kaanapali 217, Molokai 205. Pads226/227 are separate SPMI controls.
-- Inputs and eGPIO providers are disconnected. GPIO outputs have local digital readback.
return function(p, options)
    assert(p.apss_router and not p.xbl_tlmm and not p.xbl_tlmm_spmi_pads,
        'TLMM requires the APSS router and one instance.')
    local count=assert(options.gpio_count, 'TLMM requires the selected GPIO count.')
    assert(count==math.floor(count) and count>0 and count<=226, 'Invalid TLMM GPIO count.')
    -- Keep the TLMM identity view at 0x0f1db014 outside these GPIO mappings.
    for _,range in ipairs({{0x0f100000,count*0x1000},{0x0f1e2000,0x2000}}) do
        local address,size=range[1],range[2]
        for module_name,module in pairs(p) do
            if type(module)=='table' then
                for _,socket in pairs(module) do
                    if type(socket)=='table' and socket.bind=='&apss_router.initiator_socket'
                        and type(socket.address)=='number' and type(socket.size)=='number'
                        and address<socket.address+socket.size and socket.address<address+size then
                        error(string.format('TLMM overlaps %s',module_name))
                    end
                end
            end
        end
    end
    p.xbl_tlmm={moduletype='qcom,tlmm-v1',gpio_count=count,pad_first=226,pad_count=2,
        target_socket={address=0x0f100000,size=count*0x1000,bind='&apss_router.initiator_socket'}}
    p.xbl_tlmm_spmi_pads={moduletype='addrtr',mapped_base_addr=0xe2000,
        target_socket={address=0x0f1e2000,size=0x2000,relative_addresses=false,
            bind='&apss_router.initiator_socket'},
        initiator_socket={bind='&xbl_tlmm.target_socket'}}
    return p
end
