-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Evidence: Canoe DT /soc/interrupt-controller@17000000 and ufshc@1d84000.
-- Production exposes eight redistributors with the firmware's 0x40000-byte stride.
return function(p, o)
    assert(type(p) == 'table' and type(o) == 'table', 'GIC configuration is required.')
    assert(type(o.router) == 'string', 'The APSS MMIO router name is required.')
    local cpus = o.cpus or {o.cpu or 'apss'}
    local instance = o.instance or 'apss_instance'
    assert(p[instance] and #cpus > 0, 'The APSS CPUs and QEMU instance must exist.')
    assert(not p.apss_gic, 'The APSS GIC is already configured.')
    local grouped = o.group ~= nil
    if grouped then
        assert(p[o.group], 'The GIC execution group must exist.')
    end
    p.apss_gic = {
        moduletype = grouped and 'qcom,apss-gic-v1' or 'arm_gicv3',
        args = grouped and {'&' .. instance, '&' .. o.group} or {'&' .. instance},
        num_cpus = #cpus, first_cpu_index = 0, num_spi = 864,
        revision = grouped and 4 or 3, has_security_extensions = true, has_lpi = grouped,
        redist_region = {#cpus},
        dist_iface = {address = 0x17000000, size = 0x10000,
                      bind = '&' .. o.router .. '.initiator_socket'}
    }
    if grouped then
        -- QSEE programs fault routes and 64-bit controls in the same GIC owner.
        p.apss_gic.fault_socket={address=0x17020000,size=0x10000,
            bind='&'..o.router..'.initiator_socket'}
        -- Kaanapali DT and QHEE map the ITS at 0x17040000.
        -- The native device uses the parent GIC's DMA address space.
        p.apss_its = {
            moduletype = 'qcom,apss-its-v1',
            args = {'&' .. instance, '&apss_gic'},
            mem = {address = 0x17040000, size = 0x20000,
                   bind = '&' .. o.router .. '.initiator_socket'}
        }
    end
    for index,cpu in ipairs(cpus) do
        assert(p[cpu], 'The APSS CPU must exist.')
        local slot = index - 1
        local redist = grouped and 'redist_slot_' .. slot or 'redist_iface_0'
        p.apss_gic[redist] = {address = 0x17080000 + slot * 0x40000,
                             size = grouped and 0x40000 or 0x20000,
                             bind = '&' .. o.router .. '.initiator_socket'}
        for _,kind in ipairs({'irq','fiq','virq','vfiq'}) do
            p.apss_gic[kind .. '_out_' .. slot] = {bind = '&' .. cpu .. '.' .. kind .. '_in'}
        end
        for _,irq in ipairs({{'phys',30},{'virt',27},{'hyp',26},{'sec',29}}) do
            local port = 'irq_timer_' .. irq[1] .. '_out'
            assert(not p[cpu][port], 'An APSS interrupt output is already configured.')
            p[cpu][port] = {bind = '&apss_gic.ppi_in_cpu_' .. slot .. '_' .. irq[2]}
        end
        if grouped then
            p[cpu].gicv3_maintenance_interrupt = {bind = '&apss_gic.ppi_in_cpu_' .. slot .. '_25'}
            p[cpu].pmu_interrupt = {bind = '&apss_gic.ppi_in_cpu_' .. slot .. '_23'}
        end
    end
    return '&apss_gic.spi_in_265'
end
