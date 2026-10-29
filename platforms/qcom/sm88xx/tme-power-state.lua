-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Retained power policy. Evidence: models/qcom/sm88xx/tme_power_state/evidence.md.
return function(p)
    assert(p.tme_router and p.tme_clic and p.tme_timers and p.tme_rpmh_rsc and
        p.tme_low_power_control, 'Compose the TME runtime, AOP service, and cold controls first.')
    assert(not p.tme_timers.irq_1, 'The TME frame-one interrupt already has an owner.')
    local additions={}
    local function mapped(address, size, reuse)
        local existing
        for module_name,module in pairs(p) do
            if type(module)=='table' then
                for socket_name,socket in pairs(module) do
                    if type(socket)=='table' and socket.bind=='&tme_router.initiator_socket' and
                        type(socket.address)=='number' and type(socket.size)=='number' and
                        address<socket.address+socket.size and socket.address<address+size then
                        assert(reuse and not existing and socket.address==address and socket.size==size and
                            socket_name=='target_socket' and module.moduletype=='gs_memory' and
                            not module.read_only,
                            'Conflicting TME power-state mapping: '..module_name..'.'..socket_name)
                        existing=true
                    end
                end
            end
        end
        return existing
    end
    local function retained(name, address, reuse)
        assert(not p[name], 'The TME power-state group already exists.')
        if mapped(address,4,reuse) then return end
        additions[name]={moduletype='qcom,retained-boot-control-v1',
            writable_mask=0xffffffff,reset_value=0,
            target_socket={address=address,size=4,bind='&tme_router.initiator_socket'}}
    end
    -- Both OEM tables at 0x446a14 save these words, then restore the same words.
    -- Preserve the two existing runtime configuration objects, including their state.
    for i,address in ipairs({0x00811000,0x0081101c,0x008110d0,0x008110e8}) do
        retained('tme_power_saved_configuration_'..i,address,true)
    end
    -- The power controller owns the request state and the inactive external wake inputs.
    for name,range in pairs({
        request_socket={0x02237144,4},control_socket={0x02237000,4},
        wake_mask_socket={0x00810000,0x10},wake_status_socket={0x00810010,0x10},
    }) do
        assert(not p.tme_low_power_control[name], 'The TME power socket already exists.')
        mapped(range[1],range[2],false)
        p.tme_low_power_control[name]={address=range[1],size=range[2],
            bind='&tme_router.initiator_socket'}
    end
    p.tme_low_power_control.external_wake_policy='inactive-no-external-producers'
    p.tme.retained_power=true
    -- Handler 22 re-arms frame one in both OEMs. This is an inferred virtual route.
    -- The existing timer generates the level only when its enabled deadline expires.
    p.tme_timers.irq_1={bind='&tme_clic.source_22'}
    for name,module in pairs(additions) do p[name]=module end
    return p
end
