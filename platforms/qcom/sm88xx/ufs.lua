-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- The ROM constructs 0x01d84020 at 0x3078dc and 0x3078e4.
-- The controller register window starts 0x20 bytes before that address.
return function(p, o)
    assert(type(p) == 'table' and type(o) == 'table', 'UFS configuration is required.')
    assert(type(o.luns) == 'table', 'Six mutable run LUN paths indexed 0 through 5 are required.')
    assert(type(o.router) == 'string', 'The CPU MMIO router name is required.')
    assert(type(o.ram_router) == 'string' and o.ram_router ~= o.router,
           'A separate RAM-only DMA router name is required.')
    assert(type(o.irq) == 'string', 'The interrupt sink reference is required.')
    assert(not p.ufs and not p.ufs_instance and not p.ufs_dma_access, 'UFS is already configured.')
    assert(o.virtual_phy == nil or type(o.virtual_phy) == 'boolean', 'The virtual PHY option must be Boolean.')
    assert(type(o.boot_enable) == 'boolean', 'The OEM boot capability is required.')
    assert(type(o.boot_lun_en) == 'number' and o.boot_lun_en % 1 == 0
           and o.boot_lun_en >= 0 and o.boot_lun_en <= 2, 'The initial boot LU ID is required.')
    assert(o.boot_enable or o.boot_lun_en == 0, 'Boot selection requires the OEM boot capability.')
    assert(type(o.boot_lun_ids) == 'table', 'The OEM boot LU IDs are required.')
    local boot_ids = {}
    local seen = {}
    for i = 0, 5 do
        local path = o.luns[i]
        assert(type(path) == 'string' and path ~= '' and not path:find('%z'), 'A LUN path is invalid.')
        assert(not seen[path], 'Each LUN requires a separate mutable file.')
        seen[path] = true
        assert(not p['ufs_lun' .. i], 'A UFS logical unit is already configured.')
        local id = o.boot_lun_ids[i]
        assert(type(id) == 'number' and id % 1 == 0 and id >= 0 and id <= 2,
               'An OEM boot LU ID is invalid.')
        assert(id == 0 or not boot_ids[id], 'Each nonzero boot LU ID must be unique.')
        if id ~= 0 then boot_ids[id] = true end
    end
    assert(o.boot_lun_en == 0 or boot_ids[o.boot_lun_en], 'The selected boot LU is absent.')
    assert(o.rpmb_size_kib == nil or (type(o.rpmb_size_kib) == 'number' and o.rpmb_size_kib % 128 == 0
           and o.rpmb_size_kib >= 0 and o.rpmb_size_kib <= 255 * 128),
           'The RPMB size must be a multiple of 128 KiB up to 32640 KiB.')
    assert(o.rpmb_state_file == nil or (type(o.rpmb_state_file) == 'string'
           and not o.rpmb_state_file:find('%z')), 'The RPMB state path is invalid.')
    p.ufs_instance = {
        moduletype = 'QemuInstance', args = {'&' .. (o.manager or 'qemu_inst_mgr'), 'AARCH64'},
        accel = 'tcg', sync_policy = 'multithread-quantum'
    }
    p.ufs_dma_access = {
        moduletype = 'AccessMonitor', allow_unspecified = true,
        initiator_socket = {bind = '&' .. o.ram_router .. '.target_socket'}
    }
    p.ufs = {
        moduletype = 'qcom,ufs-hci-v1', args = {'&ufs_instance'}, mcq = false,
        virtual_phy = o.virtual_phy == true,
        boot_enable = o.boot_enable, boot_lun_en = o.boot_lun_en,
        rpmb_size_kib = o.rpmb_size_kib or 0,
        rpmb_state_file = o.rpmb_state_file or '',
        target_socket = {address = 0x01d84000, size = 0x3000,
                         bind = '&' .. o.router .. '.initiator_socket'},
        -- GCC_UFS_PHY_BCR is defined at GCC offset 0x77000.
        reset_socket = {address = 0x00177000, size = 4,
                        bind = '&' .. o.router .. '.initiator_socket'},
        device_reset_socket = {address = 0x0f1e9000, size = 4,
                               bind = '&' .. o.router .. '.initiator_socket'},
        dma_socket = {bind = '&ufs_dma_access.target_socket'}, irq_out = {bind = o.irq}
    }
    -- Canoe GCC defines the PHY memory and logic domains at these addresses.
    p.ufs_mem_power = {
        moduletype = 'qcom,gdsc-v1',
        target_socket = {address = 0x0019e000, size = 8,
                         bind = '&' .. o.router .. '.initiator_socket'},
        power_socket = {bind = '&ufs.mem_power_socket'}
    }
    p.ufs_logic_power = {
        moduletype = 'qcom,gdsc-v1',
        target_socket = {address = 0x00177004, size = 8,
                         bind = '&' .. o.router .. '.initiator_socket'},
        power_socket = {bind = '&ufs.logic_power_socket'}
    }
    -- APSS 0x30725c enables this TCSR gate before PHY initialization.
    p.ufs_reference_clock = {
        moduletype = 'qcom,tcsr-clock-reference-v1',
        target_socket = {address = 0x01fd5054, size = 4,
                         bind = '&' .. o.router .. '.initiator_socket'},
        clock_socket = {bind = '&ufs.reference_clock_socket'}
    }
    if o.virtual_phy then
        p.ufs.phy_socket = {address = 0x01d80000, size = 0x2000,
                            bind = '&' .. o.router .. '.initiator_socket'}
    end
    for i = 0, 5 do
        -- QEMU's key-value parser represents a literal comma with two commas.
        local path = o.luns[i]:gsub(',', ',,')
        p['ufs_lun' .. i] = {
            moduletype = 'qcom,ufs-lu-v1', args = {'&ufs_instance', '&ufs'}, lun = i,
            boot_lun_id = o.boot_lun_ids[i],
            blkdev_str = 'file=' .. path .. ',if=none,format=raw,readonly=off,snapshot=off'
        }
    end
    return p
end
