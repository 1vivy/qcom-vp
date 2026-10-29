-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Selected ELF program headers and source hashes: models/qcom/rpmh/evidence.md.
local directory=assert(debug.getinfo(1,'S').source:match('^@(.*/)'))
local profiles={
    sm8845={
        '3 1000 1e 0b100660 2e1e9729f2dc736165cb557a3ebc0f2d1639212dd0298e9122c5c065110ff30c 0b100408 258 65e4690cf7443f149bd471903d78d0e6e4e5b98c804c6fda848df0ecee5da437',
        '3 1000 f 0b100950 851b3d628f3be9ff091b4b22f0cc140a38d9d1b668ae8940f29ccbbc81a07f5b 0b100930 1e 6c02f96d361d9f7382aa057e6fd48aba3733d7eeb147d71319049058b135a6b6',
        '5 1000 1e 0b100ba8 2af8848d70d3379930b51e3448d63238b8151dece0e39fe7105693a148928b7a 0b100ab8 f0 f82c5c9dcc988e34128a10feca24f754fc0a3640e025f968e759868bdce4605b',
        '4 100 3350 81c80000 6d6425951b6331cc7d55dc8b205530abdd9c418cb2e3fbd691045b13e9ba7bd1 81c84000 4af 4c612853b70e4cfdaf3d9ac7d1a2ede3250c4237493073d44a03d3ad71866e0c',
    },
    sm8850={
        '3 1000 1e 0b100a68 e8e030d741cb112f7bdadf4a520d854aceebc80ac8477aea4180aac8cec486f3 0b100810 258 7625fbe2a6cbbe270b9c56ec3d5d6905133baa0b27c5290807710a62f2086921',
        '3 1000 f 0b100d58 851b3d628f3be9ff091b4b22f0cc140a38d9d1b668ae8940f29ccbbc81a07f5b 0b100d38 1e 3736b70b9e7dc5c9b58fb7a8dc011772c64e0cc2a0a9d15685f37e9415d70c76',
        '5 1000 20 0b100510 9564042bbe1bb913a6b9c7b80626c115df8b64dd488d354d13f491e432ec9458 0b100410 100 adc8f6184012cf6526e1a099cd2f14be5ddffafa1eda460481027ad5f75f73df',
        '4 100 3720 81c80000 f8a2a5f5a7c88c8e824116e75e35c7c98820d65c4a2af89aa6800479e1265c34 81c84000 55e 85a4f70a6f371c4e128338fe635e1a59ff826b034f44d388eb733f500544e39d',
    },
}
local discovery={
    sm8845={
        '0b092860 bc bb21c3fbfad7a44fdf50af474fdd78f031fa0bf8677130754eddcda91a72b9e7',
        '81c3ae7a 2e0 20f320273918eaea4f7da5922f83bc866a8983084a87d613d1e9c93556f0ba79',
        '0b100208 a2 23cf18b1d8bd14baec865afeb321f67ef193dd4f928d95346ca3e7e31d027148',
    },
    sm8850={
        '0b092944 bc ea32fc2d34e21abc0c680142092a27b829ab509c0bb1935dfcb3bf439a556feb',
        '81c3b8a2 2e0 20f320273918eaea4f7da5922f83bc866a8983084a87d613d1e9c93556f0ba79',
        '0b100210 a2 23cf18b1d8bd14baec865afeb321f67ef193dd4f928d95346ca3e7e31d027148',
    },
}
return function(p, o)
    assert(type(o)=='table' and profiles[o.profile], 'An exact selected AOP profile is required.')
    assert(type(o.observer_path)=='string' and o.observer_path~='', 'The handoff observer path is required.')
    assert(p.apss_router and p.ufs_ram_router and p.sequencer_dma_router and p.tme_router,
        'The APSS, TME, and image DMA routers are required.')
    assert(p.aop_configuration and p.aop_configuration.target_socket.size==0x400,
        'The coherent cold AOP header must exist before resource composition.')
    assert(not p.aop_resource_service, 'The AOP resource service already exists.')
    dofile(directory..'aop-lifecycle.lua')(p)
    local additions={}
    local function insert(name, module)
        assert(not p[name] and not additions[name], 'Duplicate AOP module: '..name)
        additions[name]=module
    end
    local function overlap(router, address, size)
        local found
        for name,module in pairs(p) do
            if type(module)=='table' then
                for socket_name,socket in pairs(module) do
                    if type(socket)=='table' and socket.bind=='&'..router..'.initiator_socket' and
                        type(socket.address)=='number' and type(socket.size)=='number' and
                        address<socket.address+socket.size and socket.address<address+size then
                        assert(not found and socket.address<=address and socket.address+socket.size>=address+size,
                            'Partial or duplicate AOP mapping: '..name..'.'..socket_name)
                        found={name=name,module=module,socket=socket,socket_name=socket_name}
                    end
                end
            end
        end
        return found
    end
    local function backing(name, address, size)
        local existing=overlap('apss_router',address,size)
        if existing then
            assert((existing.module.moduletype=='gs_memory' or
                existing.module.moduletype=='qcom,shared-executable-memory-v1' or
                existing.module.moduletype=='qcom,llcc-dare-port-v1') and
                not existing.module.read_only, 'AOP load destination is not writable memory.')
            return existing.name,existing.socket_name,existing.socket.address
        end
        insert(name,{moduletype='gs_memory',init_mem=true,
            target_socket={address=address,size=size,bind='&apss_router.initiator_socket'}})
        return name,'target_socket',address
    end
    -- These are the two non-DDR/non-IMEM PT_LOAD p_paddr/p_memsz spans in BOTH selected ELFs.
    -- Preserve the existing DDR and IMEM objects. Do not replace them with a second backing.
    for _,region in ipairs({{'aop_image_code',0x0b080000,0x18000},
            {'aop_image_data',0x0b100000,0xc000}}) do
        local name,socket,base=backing(region[1],region[2],region[3])
        -- TME reads the same SRAM through its local address window.
        -- SM8850 run67 reads physical0b08d000 at local0f08d000 during DDR initialization.
        for _,view in ipairs({{'ufs_ram_router',0},{'sequencer_dma_router',0},
                {'tme_router',0x04000000}}) do
            local router,address=view[1],region[2]+view[2]
            local existing=overlap(router,address,region[3])
            if existing then
                assert(existing.module.moduletype=='addrtr' and
                    existing.module.initiator_socket.bind=='&'..name..'.'..socket and
                    existing.module.mapped_base_addr==existing.socket.address-view[2]-base and
                    existing.socket.relative_addresses==false,
                    'AOP DMA must preserve APSS backing identity.')
            else
                -- addrtr forwards mapped_base_addr + (guest address - alias base).
                -- Direct memory sockets receive offsets relative to their own backing.
                insert(router..'_'..region[1],{moduletype='addrtr',mapped_base_addr=region[2]-base,
                    target_socket={address=address,size=region[3],relative_addresses=false,
                        bind='&'..router..'.initiator_socket'},
                    initiator_socket={bind='&'..name..'.'..socket}})
            end
        end
    end
    -- The other PT_LOAD destinations are already covered by coherent DDR and IMEM.
    -- A missing backing is a composition error, not permission to invent another image range.
    for _,region in ipairs({{0x81c00000,0x16000},{0x81c16000,0x8000},
            {0x81c1e000,0x42000},{0x14685000,0x5000},{0x1468a000,0x1000}}) do
        assert(overlap('apss_router',region[1],region[2]), 'A selected AOP ELF destination is unmapped.')
    end
    -- Reuse DDR for the database if present. Otherwise map only the evidenced output span.
    backing('aop_command_database',0x81c60000,0x2000)
    insert('aop_resource_service',{moduletype='qcom,aop-resource-service-v1',
        observer_path=o.observer_path,startup_policy='synchronous-virtual',
        tables=table.concat(profiles[o.profile],';'),
        discovery=table.concat(discovery[o.profile],';'),
        memory_socket={bind='&apss_router.target_socket'}})
    -- Original TME 0x428650/0x4285a0 submits MC0, SH0, SN0, and CN0 through TCS1.
    -- The TME requester shares resources but does not share APSS TCS or IRQ state.
    assert(not overlap('tme_router',0x02210000,0x2000), 'The TME RSC window already has an owner.')
    assert(p.tme_clic, 'The TME resource controller requires the existing CLIC.')
    -- The virtual platform stays awake. The RSC mask controls its actual completion interrupt.
    for i,address in ipairs({0x00c00214,0x00c00218}) do
        assert(not overlap('tme_router',address,4), 'A TME RSC control already has an owner.')
        insert('tme_rsc_configuration_'..i,{moduletype='qcom,retained-boot-control-v1',
            writable_mask=0xffffffff,reset_value=0,
            target_socket={address=address,size=4,bind='&tme_router.initiator_socket'}})
    end
    insert('tme_rpmh_rsc',{moduletype='qcom,rpmh-rsc-v1',banks='0 6 3',
        requester_base=3,resource_owner_path='rpmh_rsc',
        irq_0={bind='&tme_clic.source_24'},
        target_socket={address=0x02210000,size=0x2000,bind='&tme_router.initiator_socket'}})
    for name,module in pairs(additions) do p[name]=module end
    return p
end
