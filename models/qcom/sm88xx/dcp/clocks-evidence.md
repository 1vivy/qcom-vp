# DCP clock descriptors

## Scope and address translation

The selected CPH2749 and CPH2771 packages supply `RADIO/dcp.img`.
The ELF starts at image offset `0x16000`.
All addresses below are decoded values, not copied firmware bytes.
Both images register complete controller tables during clock initialization.
A descriptor proves an available operation, not execution of every rate selection.
The display-off path can select a subset after initialization.
No OEM run occurred in this slice.

The FDT assigns GCC_REG to DCP `0x40110000`, not `0x40100000`.
The latter address is GPLL0. GCC_REG reaches system `0x00110000`.
DISP_CC_REG uses DCP `0x103aa000`, which reaches system `0x09baa000`.
The three display PLLs reach system `0x09ba2000`, `0x09ba3000`, and `0x09ba4000`.

| Slot | SM8850 | SM8845 | System base |
| --- | --- | --- | --- |
| DISP_CC_REG | `a014f0ec` | `a417f0ec` | `09baa000` |
| Display PLL0 | `a014f0f0` | `a417f0f0` | `09ba2000` |
| Display PLL1 | `a014f0f4` | `a417f0f4` | `09ba3000` |
| Display PLL2 | `a014f0f8` | `a417f0f8` | `09ba4000` |
| GCC_REG | `a014f104` | `a417f104` | `00110000` |
| GPLL0 | `a014f108` | `a417f108` | `00100000` |
| GPLL7 | `a014f12c` | `a417f12c` | `00107000` |

## Access routines

| Operation | SM8850 PC | SM8845 PC | Contract |
| --- | --- | --- | --- |
| Branch enable | `a00d7b9c` | `a4104c64` | Set vote mask or CBCR bit 0 |
| Branch disable | `a00d7bb2` | `a4104c7a` | Clear vote mask or CBCR bit 0 |
| Branch running | `a00d7bf0` | `a4104cb8` | Require status bits 31,30,28 clear |
| Reset control | `a00d7c00` | `a4104cc8` | Reset word bit 0 follows request |
| Branch divider | `a00d7c60` | `a4104d28` | CBCR divider field |
| RCG update | `a00d7cbc` | `a4104d84` | Set CMD bit 0, poll until clear |
| RCG running | `a00d7d20` | `a4104de8` | CMD bit 31 clear |
| DFS enable | `a00d7d30` | `a4104df8` | Write `0x21` at CMD+`0x14` |
| DFS enabled | `a00d7d42` | `a4104e0a` | Read CMD+`0x14`, test bit 0 |
| DFS level write | `a00d7d56` | `a4104e1e` | CFG/M/N/D arrays at +1c/+5c/+9c/+dc |
| RCG program fields | `a00d7ebc` | `a4104f84` | Selector CFG[10:8], HID CFG[4:0], dual-edge CFG[13:12] |
| GDSC enable | `a00d7df2` | `a4104eba` | Clear collapse bit 0, wait for bit 31 set |
| GDSC disable | `a00d7e44` | `a4104f0c` | Set collapse bit 0 |
| Standalone divider | `a00d7e90` | `a4104f58` | Four-bit divisor-minus-one field |
| Taycan disable | `a00d82b2` | `a410538e` | Clear output bit 0, then OPMODE or reset |
| Taycan running | `a00d82ec` | `a41053c8` | MODE bits 31 and 2 set |

Normal RCG words are CMD+0, CFG+4, M+8, N+12, and D+16.
A rate record supplies selector, twice the divisor, M, and N.
The driver writes HID as twice-divisor minus one.
For a fractional rate, it writes M, `~(N-M)`, and `~N`.
It sets CFG mode 2 for that fractional rate.
The model completes updates only when the selected source supplies a clock.
The DFS model already supports the exact enable and level-array layout.

## Branch tables

Each row gives name, descriptor, slot, CBCR offset, reset offset, parent descriptor, vote offset, and vote mask.
Offsets use their controller base. `ffffffff` means no register.

| SoC | Name | Descriptor | Slot | CBCR | Reset | Parent | Vote | Mask |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 8850 | `disp_cc_dcp_sleep_clk` | `a0211350` | `a014f0ec` | `a00c` | `a000` | `a0212600` | `ffffffff` | `0` |
| 8850 | `disp_cc_debug_clk` | `a021138c` | `a014f0ec` | `5004` | `ffffffff` | `a0212600` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_spdm_debug_clk` | `a02113c8` | `a014f0ec` | `6020` | `6000` | `a0212600` | `ffffffff` | `0` |
| 8850 | `disp_cc_sleep_clk` | `a0211404` | `a014f0ec` | `6064` | `ffffffff` | `a0212600` | `ffffffff` | `0` |
| 8850 | `disp_cc_dcp_dbg_clk` | `a0211440` | `a014f0ec` | `a028` | `a000` | `a0212668` | `ffffffff` | `0` |
| 8850 | `disp_cc_dcp_f_clk` | `a021147c` | `a014f0ec` | `a02c` | `a000` | `a0212668` | `ffffffff` | `0` |
| 8850 | `disp_cc_dcp_proc_h_clk` | `a02114b8` | `a014f0ec` | `a030` | `a000` | `a0212668` | `ffffffff` | `0` |
| 8850 | `disp_cc_dcp_ss_h_clk` | `a02114f4` | `a014f0ec` | `a040` | `a000` | `a0212668` | `ffffffff` | `0` |
| 8850 | `disp_cc_dcp_dbg_apb_clk` | `a0211530` | `a014f0ec` | `a05c` | `a000` | `a02126b4` | `ffffffff` | `0` |
| 8850 | `disp_cc_dcp_dbg_atb_clk` | `a021156c` | `a014f0ec` | `a094` | `a000` | `a0212700` | `ffffffff` | `0` |
| 8850 | `disp_cc_dcp_dbg_qts_clk` | `a02115a8` | `a014f0ec` | `a078` | `a000` | `a021274c` | `ffffffff` | `0` |
| 8850 | `disp_cc_esync0_clk` | `a02115e4` | `a014f0ec` | `cc` | `0` | `a0212798` | `ffffffff` | `0` |
| 8850 | `disp_cc_esync1_clk` | `a0211620` | `a014f0ec` | `d0` | `0` | `a02127e4` | `ffffffff` | `0` |
| 8850 | `disp_cc_dcp_ahb_clk` | `a021165c` | `a014f0ec` | `a004` | `a000` | `a0212830` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_ahb1_clk` | `a0211698` | `a014f0ec` | `2028` | `2000` | `a0212830` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_ahb_clk` | `a02116d4` | `a014f0ec` | `c4` | `0` | `a0212830` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_ahb_swi_clk` | `a0211710` | `a014f0ec` | `c0` | `0` | `a0212830` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_non_gdsc_ahb_clk` | `a021174c` | `a014f0ec` | `4004` | `4000` | `a0212830` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_rscc_ahb_clk` | `a0211788` | `a014f0ec` | `400c` | `4000` | `a0212830` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_byte0_clk` | `a0211800` | `a014f0ec` | `44` | `0` | `a021287c` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_byte0_intf_clk` | `a021183c` | `a014f0ec` | `48` | `0` | `a021287c` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_byte1_clk` | `a0211878` | `a014f0ec` | `4c` | `0` | `a02128c8` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_byte1_intf_clk` | `a02118b4` | `a014f0ec` | `50` | `0` | `a02128c8` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx0_aux_clk` | `a02118f0` | `a014f0ec` | `74` | `0` | `a0212914` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx0_crypto_clk` | `a021192c` | `a014f0ec` | `68` | `0` | `a0212960` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx0_link_clk` | `a0211968` | `a014f0ec` | `5c` | `0` | `a0212960` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx0_link_intf_clk` | `a02119a4` | `a014f0ec` | `64` | `0` | `a0212960` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx0_usb_router_link_intf_clk` | `a02119e0` | `a014f0ec` | `60` | `0` | `a0212960` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_spdm_dp_crypto_clk` | `a0211a1c` | `a014f0ec` | `6014` | `6000` | `a0212960` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx0_pixel0_clk` | `a0211a58` | `a014f0ec` | `6c` | `0` | `a02129ac` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_spdm_dp_pixel_clk` | `a0211a94` | `a014f0ec` | `6018` | `6000` | `a02129ac` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx0_pixel1_clk` | `a0211ad0` | `a014f0ec` | `70` | `0` | `a02129f8` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_spdm_dp_pixel1_clk` | `a0211b0c` | `a014f0ec` | `601c` | `6000` | `a02129f8` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx1_aux_clk` | `a0211b48` | `a014f0ec` | `90` | `0` | `a0212a44` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx1_crypto_clk` | `a0211b84` | `a014f0ec` | `8c` | `0` | `a0212a90` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx1_link_clk` | `a0211bc0` | `a014f0ec` | `80` | `0` | `a0212a90` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx1_link_intf_clk` | `a0211bfc` | `a014f0ec` | `88` | `0` | `a0212a90` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx1_usb_router_link_intf_clk` | `a0211c38` | `a014f0ec` | `84` | `0` | `a0212a90` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx1_pixel0_clk` | `a0211c74` | `a014f0ec` | `78` | `0` | `a0212adc` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx1_pixel1_clk` | `a0211cb0` | `a014f0ec` | `7c` | `0` | `a0212b28` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx2_aux_clk` | `a0211cec` | `a014f0ec` | `a8` | `0` | `a0212b74` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx2_crypto_clk` | `a0211d28` | `a014f0ec` | `a4` | `0` | `a0212bc0` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx2_link_clk` | `a0211d64` | `a014f0ec` | `9c` | `0` | `a0212bc0` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx2_link_intf_clk` | `a0211da0` | `a014f0ec` | `a0` | `0` | `a0212bc0` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx2_pixel0_clk` | `a0211ddc` | `a014f0ec` | `94` | `0` | `a0212c0c` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx2_pixel1_clk` | `a0211e18` | `a014f0ec` | `98` | `0` | `a0212c58` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx3_aux_clk` | `a0211e54` | `a014f0ec` | `b8` | `0` | `a0212ca4` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx3_crypto_clk` | `a0211e90` | `a014f0ec` | `bc` | `0` | `a0212cf0` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx3_link_clk` | `a0211ecc` | `a014f0ec` | `b0` | `0` | `a0212cf0` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx3_link_intf_clk` | `a0211f08` | `a014f0ec` | `b4` | `0` | `a0212cf0` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_dptx3_pixel0_clk` | `a0211f44` | `a014f0ec` | `ac` | `0` | `a0212d3c` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_esc0_clk` | `a0211f80` | `a014f0ec` | `54` | `0` | `a0212d88` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_esc1_clk` | `a0211fbc` | `a014f0ec` | `58` | `0` | `a0212dd4` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_mdp1_clk` | `a0211ff8` | `a014f0ec` | `2004` | `2000` | `a0212e20` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_mdp_clk` | `a0212034` | `a014f0ec` | `10` | `0` | `a0212e20` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_mdp_lut1_clk` | `a0212070` | `a014f0ec` | `2014` | `2000` | `a0212e20` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_mdp_lut_clk` | `a02120ac` | `a014f0ec` | `20` | `0` | `a0212e20` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_mdp_ss_ip_clk` | `a02120e8` | `a014f0ec` | `30` | `0` | `a0212e20` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_spdm_mdp_clk` | `a0212124` | `a014f0ec` | `6010` | `6000` | `a0212e20` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_pclk0_clk` | `a0212160` | `a014f0ec` | `4` | `0` | `a0212e6c` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_spdm_pclk0_clk` | `a021219c` | `a014f0ec` | `6004` | `6000` | `a0212e6c` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_pclk1_clk` | `a02121d8` | `a014f0ec` | `8` | `0` | `a0212eb8` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_spdm_pclk1_clk` | `a0212214` | `a014f0ec` | `6008` | `6000` | `a0212eb8` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_pclk2_clk` | `a0212250` | `a014f0ec` | `c` | `0` | `a0212f04` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_spdm_pclk2_clk` | `a021228c` | `a014f0ec` | `600c` | `6000` | `a0212f04` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_rscc_vsync_clk` | `a02122c8` | `a014f0ec` | `4008` | `4000` | `a0212f50` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_vsync1_clk` | `a0212304` | `a014f0ec` | `2024` | `2000` | `a0212f50` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_vsync_clk` | `a0212340` | `a014f0ec` | `40` | `0` | `a0212f50` | `ffffffff` | `0` |
| 8850 | `disp_cc_osc_clk` | `a021237c` | `a014f0ec` | `c8` | `0` | `a0212f9c` | `ffffffff` | `0` |
| 8850 | `disp_cc_pll_test_clk` | `a02123b8` | `a014f0ec` | `5010` | `ffffffff` | `a0212fe8` | `ffffffff` | `0` |
| 8850 | `disp_cc_dcp_tmr_clk` | `a02123f4` | `a014f0ec` | `a008` | `a000` | `a0213034` | `ffffffff` | `0` |
| 8850 | `disp_cc_mdss_accu_shift_clk` | `a0212430` | `a014f0ec` | `6060` | `ffffffff` | `a0213034` | `ffffffff` | `0` |
| 8850 | `disp_cc_xo_clk` | `a021246c` | `a014f0ec` | `605c` | `ffffffff` | `a0213034` | `ffffffff` | `0` |
| 8850 | `gcc_qupv3_wrap_1_m_axi_clk` | `a021391c` | `a014f104` | `13140` | `8000` | `a02146e8` | `4a008` | `100000` |
| 8850 | `gcc_qupv3_wrap_2_m_ahb_clk` | `a0213958` | `a014f104` | `13298` | `e000` | `a02146e8` | `4a010` | `4` |
| 8850 | `gcc_qupv3_wrap_3_m_ahb_clk` | `a0213994` | `a014f104` | `133f0` | `98000` | `a02146e8` | `4a018` | `100` |
| 8850 | `gcc_qupv3_wrap_4_m_ahb_clk` | `a02139d0` | `a014f104` | `13548` | `99000` | `a02146e8` | `4a018` | `400000` |
| 8850 | `gcc_boot_rom_ahb_clk` | `a0213a0c` | `a014f104` | `28004` | `ffffffff` | `a0214734` | `4a010` | `40000` |
| 8850 | `gcc_disp_ahb_clk` | `a0213a48` | `a014f104` | `17004` | `17000` | `a0214734` | `ffffffff` | `0` |
| 8850 | `gcc_qupv3_i2c_s_ahb_clk` | `a0213a84` | `a014f104` | `13000` | `7000` | `a0214734` | `4a008` | `80` |
| 8850 | `gcc_qupv3_wrap_1_s_ahb_clk` | `a0213ac0` | `a014f104` | `13144` | `8000` | `a0214734` | `4a008` | `200000` |
| 8850 | `gcc_qupv3_wrap_2_s_ahb_clk` | `a0213afc` | `a014f104` | `1329c` | `e000` | `a0214734` | `4a010` | `2` |
| 8850 | `gcc_qupv3_wrap_3_ibi_1_ahb_clk` | `a0213b38` | `a014f104` | `9876c` | `98000` | `a0214734` | `4a018` | `40000` |
| 8850 | `gcc_qupv3_wrap_3_ibi_2_ahb_clk` | `a0213b74` | `a014f104` | `98770` | `98000` | `a0214734` | `4a018` | `80000` |
| 8850 | `gcc_qupv3_wrap_3_s_ahb_clk` | `a0213bb0` | `a014f104` | `133f4` | `98000` | `a0214734` | `4a018` | `200` |
| 8850 | `gcc_qupv3_wrap_4_s_ahb_clk` | `a0213bec` | `a014f104` | `1354c` | `99000` | `a0214734` | `4a018` | `800000` |
| 8850 | `gcc_disp_hf_axi_clk` | `a0213c28` | `a014f104` | `1701c` | `17000` | `a0214780` | `ffffffff` | `0` |
| 8850 | `gcc_disp_sf_axi_clk` | `a0213c64` | `a014f104` | `17008` | `17000` | `a02147cc` | `ffffffff` | `0` |
| 8850 | `gcc_qupv3_i2c_core_clk` | `a0213ca0` | `a014f104` | `13004` | `7000` | `a0214818` | `4a008` | `100` |
| 8850 | `gcc_qupv3_i2c_s0_clk` | `a0213cdc` | `a014f104` | `7004` | `7000` | `a0214864` | `4a008` | `400` |
| 8850 | `gcc_qupv3_i2c_s1_clk` | `a0213d18` | `a014f104` | `7020` | `7000` | `a02148b0` | `4a008` | `800` |
| 8850 | `gcc_qupv3_i2c_s2_clk` | `a0213d54` | `a014f104` | `703c` | `7000` | `a02148fc` | `4a008` | `1000` |
| 8850 | `gcc_qupv3_i2c_s3_clk` | `a0213d90` | `a014f104` | `7058` | `7000` | `a0214948` | `4a008` | `2000` |
| 8850 | `gcc_qupv3_i2c_s4_clk` | `a0213dcc` | `a014f104` | `7074` | `7000` | `a0214994` | `4a008` | `4000` |
| 8850 | `gcc_qupv3_wrap1_core_2x_clk` | `a0213e08` | `a014f104` | `1315c` | `8000` | `a02149e0` | `4a008` | `40000` |
| 8850 | `gcc_qupv3_wrap1_core_clk` | `a0213e44` | `a014f104` | `13148` | `8000` | `a02149e0` | `4a008` | `80000` |
| 8850 | `gcc_qupv3_wrap1_qspi_ref_clk` | `a0213e80` | `a014f104` | `88bc` | `8000` | `a0214a2c` | `4a010` | `20000000` |
| 8850 | `gcc_qupv3_wrap1_s2_clk` | `a0213ebc` | `a014f104` | `827c` | `8000` | `a0214a2c` | `4a008` | `1000000` |
| 8850 | `gcc_qupv3_wrap1_s0_clk` | `a0213ef8` | `a014f104` | `8004` | `8000` | `a0214a78` | `4a008` | `400000` |
| 8850 | `gcc_qupv3_wrap1_s1_clk` | `a0213f34` | `a014f104` | `8140` | `8000` | `a0214ac4` | `4a008` | `800000` |
| 8850 | `gcc_qupv3_wrap1_s3_clk` | `a0213f70` | `a014f104` | `8290` | `8000` | `a0214b10` | `4a008` | `2000000` |
| 8850 | `gcc_qupv3_wrap1_s4_clk` | `a0213fac` | `a014f104` | `83cc` | `8000` | `a0214b5c` | `4a008` | `4000000` |
| 8850 | `gcc_qupv3_wrap1_s5_clk` | `a0213fe8` | `a014f104` | `8508` | `8000` | `a0214ba8` | `4a008` | `8000000` |
| 8850 | `gcc_qupv3_wrap1_s6_clk` | `a0214024` | `a014f104` | `8644` | `8000` | `a0214bf4` | `4a008` | `10000000` |
| 8850 | `gcc_qupv3_wrap1_s7_clk` | `a0214060` | `a014f104` | `8780` | `8000` | `a0214c40` | `4a010` | `10000` |
| 8850 | `gcc_qupv3_wrap2_core_2x_clk` | `a021409c` | `a014f104` | `132b4` | `e000` | `a0214c8c` | `4a010` | `8` |
| 8850 | `gcc_qupv3_wrap2_core_clk` | `a02140d8` | `a014f104` | `132a0` | `e000` | `a0214c8c` | `4a010` | `1` |
| 8850 | `gcc_qupv3_wrap2_s0_clk` | `a0214114` | `a014f104` | `e004` | `e000` | `a0214cd8` | `4a010` | `10` |
| 8850 | `gcc_qupv3_wrap2_s1_clk` | `a0214150` | `a014f104` | `e140` | `e000` | `a0214d24` | `4a010` | `20` |
| 8850 | `gcc_qupv3_wrap2_s2_clk` | `a021418c` | `a014f104` | `e27c` | `e000` | `a0214d70` | `4a010` | `40` |
| 8850 | `gcc_qupv3_wrap2_s3_clk` | `a02141c8` | `a014f104` | `e3b8` | `e000` | `a0214dbc` | `4a010` | `80` |
| 8850 | `gcc_qupv3_wrap2_s4_clk` | `a0214204` | `a014f104` | `e4f4` | `e000` | `a0214e08` | `4a010` | `100` |
| 8850 | `gcc_qupv3_wrap3_core_2x_clk` | `a0214240` | `a014f104` | `1340c` | `98000` | `a0214e54` | `4a018` | `800` |
| 8850 | `gcc_qupv3_wrap3_core_clk` | `a021427c` | `a014f104` | `133f8` | `98000` | `a0214e54` | `4a018` | `400` |
| 8850 | `gcc_qupv3_wrap3_ibi_ctrl_1_clk` | `a02142b8` | `a014f104` | `98774` | `98000` | `a0214ea0` | `4a018` | `100000` |
| 8850 | `gcc_qupv3_wrap3_ibi_ctrl_2_clk` | `a02142f4` | `a014f104` | `98778` | `98000` | `a0214ea0` | `4a018` | `200000` |
| 8850 | `gcc_qupv3_wrap3_s0_clk` | `a0214330` | `a014f104` | `98004` | `98000` | `a0214eec` | `4a018` | `1000` |
| 8850 | `gcc_qupv3_wrap3_s1_clk` | `a021436c` | `a014f104` | `98140` | `98000` | `a0214f38` | `4a018` | `2000` |
| 8850 | `gcc_qupv3_wrap3_s2_clk` | `a02143a8` | `a014f104` | `9827c` | `98000` | `a0214f84` | `4a018` | `4000` |
| 8850 | `gcc_qupv3_wrap3_s3_clk` | `a02143e4` | `a014f104` | `983b8` | `98000` | `a0214fd0` | `4a018` | `8000` |
| 8850 | `gcc_qupv3_wrap3_s4_clk` | `a0214420` | `a014f104` | `984f4` | `98000` | `a021501c` | `4a018` | `10000` |
| 8850 | `gcc_qupv3_wrap3_s5_clk` | `a021445c` | `a014f104` | `98630` | `98000` | `a0215068` | `4a018` | `20000` |
| 8850 | `gcc_qupv3_wrap4_core_2x_clk` | `a0214498` | `a014f104` | `13564` | `99000` | `a02150b4` | `4a018` | `2000000` |
| 8850 | `gcc_qupv3_wrap4_core_clk` | `a02144d4` | `a014f104` | `13550` | `99000` | `a02150b4` | `4a018` | `1000000` |
| 8850 | `gcc_qupv3_wrap4_s0_clk` | `a0214510` | `a014f104` | `99004` | `99000` | `a0215100` | `4a018` | `4000000` |
| 8850 | `gcc_qupv3_wrap4_s1_clk` | `a021454c` | `a014f104` | `99140` | `99000` | `a021514c` | `4a018` | `8000000` |
| 8850 | `gcc_qupv3_wrap4_s2_clk` | `a0214588` | `a014f104` | `9927c` | `99000` | `a0215198` | `4a018` | `10000000` |
| 8850 | `gcc_qupv3_wrap4_s3_clk` | `a02145c4` | `a014f104` | `993b8` | `99000` | `a02151e4` | `4a018` | `20000000` |
| 8850 | `gcc_qupv3_wrap4_s4_clk` | `a0214600` | `a014f104` | `994f4` | `99000` | `a0215230` | `4a018` | `40000000` |
| 8845 | `disp_cc_dcp_sleep_clk` | `a423cb40` | `a417f0ec` | `a00c` | `a000` | `a423db88` | `ffffffff` | `0` |
| 8845 | `disp_cc_debug_clk` | `a423cb74` | `a417f0ec` | `5004` | `ffffffff` | `a423db88` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_spdm_debug_clk` | `a423cba8` | `a417f0ec` | `6020` | `6000` | `a423db88` | `ffffffff` | `0` |
| 8845 | `disp_cc_sleep_clk` | `a423cbdc` | `a417f0ec` | `6064` | `ffffffff` | `a423db88` | `ffffffff` | `0` |
| 8845 | `disp_cc_dcp_dbg_clk` | `a423cc10` | `a417f0ec` | `a028` | `a000` | `a423dbe0` | `ffffffff` | `0` |
| 8845 | `disp_cc_dcp_f_clk` | `a423cc44` | `a417f0ec` | `a02c` | `a000` | `a423dbe0` | `ffffffff` | `0` |
| 8845 | `disp_cc_dcp_proc_h_clk` | `a423cc78` | `a417f0ec` | `a030` | `a000` | `a423dbe0` | `ffffffff` | `0` |
| 8845 | `disp_cc_dcp_ss_h_clk` | `a423ccac` | `a417f0ec` | `a040` | `a000` | `a423dbe0` | `ffffffff` | `0` |
| 8845 | `disp_cc_dcp_dbg_apb_clk` | `a423cce0` | `a417f0ec` | `a05c` | `a000` | `a423dc20` | `ffffffff` | `0` |
| 8845 | `disp_cc_dcp_dbg_atb_clk` | `a423cd14` | `a417f0ec` | `a094` | `a000` | `a423dc60` | `ffffffff` | `0` |
| 8845 | `disp_cc_dcp_dbg_qts_clk` | `a423cd48` | `a417f0ec` | `a078` | `a000` | `a423dca0` | `ffffffff` | `0` |
| 8845 | `disp_cc_esync0_clk` | `a423cd7c` | `a417f0ec` | `cc` | `0` | `a423dce0` | `ffffffff` | `0` |
| 8845 | `disp_cc_esync1_clk` | `a423cdb0` | `a417f0ec` | `d0` | `0` | `a423dd20` | `ffffffff` | `0` |
| 8845 | `disp_cc_dcp_ahb_clk` | `a423cde4` | `a417f0ec` | `a004` | `a000` | `a423dd60` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_ahb1_clk` | `a423ce18` | `a417f0ec` | `2028` | `2000` | `a423dd60` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_ahb_clk` | `a423ce4c` | `a417f0ec` | `c4` | `0` | `a423dd60` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_ahb_swi_clk` | `a423ce80` | `a417f0ec` | `c0` | `0` | `a423dd60` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_non_gdsc_ahb_clk` | `a423ceb4` | `a417f0ec` | `4004` | `4000` | `a423dd60` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_rscc_ahb_clk` | `a423cee8` | `a417f0ec` | `400c` | `4000` | `a423dd60` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_byte0_clk` | `a423cf50` | `a417f0ec` | `44` | `0` | `a423dda0` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_byte0_intf_clk` | `a423cf84` | `a417f0ec` | `48` | `0` | `a423dda0` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_byte1_clk` | `a423cfb8` | `a417f0ec` | `4c` | `0` | `a423dde0` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_byte1_intf_clk` | `a423cfec` | `a417f0ec` | `50` | `0` | `a423dde0` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx0_aux_clk` | `a423d020` | `a417f0ec` | `74` | `0` | `a423de20` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx0_crypto_clk` | `a423d054` | `a417f0ec` | `68` | `0` | `a423de60` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx0_link_clk` | `a423d088` | `a417f0ec` | `5c` | `0` | `a423de60` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx0_link_intf_clk` | `a423d0bc` | `a417f0ec` | `64` | `0` | `a423de60` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx0_usb_router_link_intf_clk` | `a423d0f0` | `a417f0ec` | `60` | `0` | `a423de60` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_spdm_dp_crypto_clk` | `a423d124` | `a417f0ec` | `6014` | `6000` | `a423de60` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx0_pixel0_clk` | `a423d158` | `a417f0ec` | `6c` | `0` | `a423dea0` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_spdm_dp_pixel_clk` | `a423d18c` | `a417f0ec` | `6018` | `6000` | `a423dea0` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx0_pixel1_clk` | `a423d1c0` | `a417f0ec` | `70` | `0` | `a423dee0` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_spdm_dp_pixel1_clk` | `a423d1f4` | `a417f0ec` | `601c` | `6000` | `a423dee0` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx1_aux_clk` | `a423d228` | `a417f0ec` | `90` | `0` | `a423df20` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx1_crypto_clk` | `a423d25c` | `a417f0ec` | `8c` | `0` | `a423df60` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx1_link_clk` | `a423d290` | `a417f0ec` | `80` | `0` | `a423df60` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx1_link_intf_clk` | `a423d2c4` | `a417f0ec` | `88` | `0` | `a423df60` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx1_usb_router_link_intf_clk` | `a423d2f8` | `a417f0ec` | `84` | `0` | `a423df60` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx1_pixel0_clk` | `a423d32c` | `a417f0ec` | `78` | `0` | `a423dfa0` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx1_pixel1_clk` | `a423d360` | `a417f0ec` | `7c` | `0` | `a423dfe0` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx2_aux_clk` | `a423d394` | `a417f0ec` | `a8` | `0` | `a423e020` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx2_crypto_clk` | `a423d3c8` | `a417f0ec` | `a4` | `0` | `a423e060` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx2_link_clk` | `a423d3fc` | `a417f0ec` | `9c` | `0` | `a423e060` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx2_link_intf_clk` | `a423d430` | `a417f0ec` | `a0` | `0` | `a423e060` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx2_pixel0_clk` | `a423d464` | `a417f0ec` | `94` | `0` | `a423e0a0` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx2_pixel1_clk` | `a423d498` | `a417f0ec` | `98` | `0` | `a423e0e0` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx3_aux_clk` | `a423d4cc` | `a417f0ec` | `b8` | `0` | `a423e120` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx3_crypto_clk` | `a423d500` | `a417f0ec` | `bc` | `0` | `a423e160` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx3_link_clk` | `a423d534` | `a417f0ec` | `b0` | `0` | `a423e160` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx3_link_intf_clk` | `a423d568` | `a417f0ec` | `b4` | `0` | `a423e160` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_dptx3_pixel0_clk` | `a423d59c` | `a417f0ec` | `ac` | `0` | `a423e1a0` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_esc0_clk` | `a423d5d0` | `a417f0ec` | `54` | `0` | `a423e1e0` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_esc1_clk` | `a423d604` | `a417f0ec` | `58` | `0` | `a423e220` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_mdp1_clk` | `a423d638` | `a417f0ec` | `2004` | `2000` | `a423e260` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_mdp_clk` | `a423d66c` | `a417f0ec` | `10` | `0` | `a423e260` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_mdp_lut1_clk` | `a423d6a0` | `a417f0ec` | `2014` | `2000` | `a423e260` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_mdp_lut_clk` | `a423d6d4` | `a417f0ec` | `20` | `0` | `a423e260` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_mdp_ss_ip_clk` | `a423d708` | `a417f0ec` | `30` | `0` | `a423e260` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_spdm_mdp_clk` | `a423d73c` | `a417f0ec` | `6010` | `6000` | `a423e260` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_pclk0_clk` | `a423d770` | `a417f0ec` | `4` | `0` | `a423e2a0` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_spdm_pclk0_clk` | `a423d7a4` | `a417f0ec` | `6004` | `6000` | `a423e2a0` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_pclk1_clk` | `a423d7d8` | `a417f0ec` | `8` | `0` | `a423e2e0` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_spdm_pclk1_clk` | `a423d80c` | `a417f0ec` | `6008` | `6000` | `a423e2e0` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_pclk2_clk` | `a423d840` | `a417f0ec` | `c` | `0` | `a423e320` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_spdm_pclk2_clk` | `a423d874` | `a417f0ec` | `600c` | `6000` | `a423e320` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_rscc_vsync_clk` | `a423d8a8` | `a417f0ec` | `4008` | `4000` | `a423e360` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_vsync1_clk` | `a423d8dc` | `a417f0ec` | `2024` | `2000` | `a423e360` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_vsync_clk` | `a423d910` | `a417f0ec` | `40` | `0` | `a423e360` | `ffffffff` | `0` |
| 8845 | `disp_cc_osc_clk` | `a423d944` | `a417f0ec` | `c8` | `0` | `a423e3a0` | `ffffffff` | `0` |
| 8845 | `disp_cc_pll_test_clk` | `a423d978` | `a417f0ec` | `5010` | `ffffffff` | `a423e3e0` | `ffffffff` | `0` |
| 8845 | `disp_cc_dcp_tmr_clk` | `a423d9ac` | `a417f0ec` | `a008` | `a000` | `a423e420` | `ffffffff` | `0` |
| 8845 | `disp_cc_mdss_accu_shift_clk` | `a423d9e0` | `a417f0ec` | `6060` | `ffffffff` | `a423e420` | `ffffffff` | `0` |
| 8845 | `disp_cc_xo_clk` | `a423da14` | `a417f0ec` | `605c` | `ffffffff` | `a423e420` | `ffffffff` | `0` |
| 8845 | `gcc_qupv3_wrap_1_m_axi_clk` | `a423ec64` | `a417f104` | `13140` | `8000` | `a423f72c` | `4a008` | `100000` |
| 8845 | `gcc_qupv3_wrap_2_m_ahb_clk` | `a423ec98` | `a417f104` | `13298` | `e000` | `a423f72c` | `4a010` | `4` |
| 8845 | `gcc_qupv3_wrap_3_m_ahb_clk` | `a423eccc` | `a417f104` | `133f0` | `98000` | `a423f72c` | `4a018` | `100` |
| 8845 | `gcc_boot_rom_ahb_clk` | `a423ed00` | `a417f104` | `28004` | `ffffffff` | `a423f76c` | `4a010` | `40000` |
| 8845 | `gcc_disp_ahb_clk` | `a423ed34` | `a417f104` | `17004` | `17000` | `a423f76c` | `ffffffff` | `0` |
| 8845 | `gcc_qupv3_i2c_s_ahb_clk` | `a423ed68` | `a417f104` | `13000` | `7000` | `a423f76c` | `4a008` | `80` |
| 8845 | `gcc_qupv3_wrap_1_s_ahb_clk` | `a423ed9c` | `a417f104` | `13144` | `8000` | `a423f76c` | `4a008` | `200000` |
| 8845 | `gcc_qupv3_wrap_2_s_ahb_clk` | `a423edd0` | `a417f104` | `1329c` | `e000` | `a423f76c` | `4a010` | `2` |
| 8845 | `gcc_qupv3_wrap_3_ibi_1_ahb_clk` | `a423ee04` | `a417f104` | `9876c` | `98000` | `a423f76c` | `4a018` | `40000` |
| 8845 | `gcc_qupv3_wrap_3_ibi_2_ahb_clk` | `a423ee38` | `a417f104` | `98770` | `98000` | `a423f76c` | `4a018` | `80000` |
| 8845 | `gcc_qupv3_wrap_3_s_ahb_clk` | `a423ee6c` | `a417f104` | `133f4` | `98000` | `a423f76c` | `4a018` | `200` |
| 8845 | `gcc_disp_hf_axi_clk` | `a423eea0` | `a417f104` | `1701c` | `17000` | `a423f7ac` | `ffffffff` | `0` |
| 8845 | `gcc_disp_sf_axi_clk` | `a423eed4` | `a417f104` | `17008` | `17000` | `a423f7ec` | `ffffffff` | `0` |
| 8845 | `gcc_qupv3_i2c_core_clk` | `a423ef08` | `a417f104` | `13004` | `7000` | `a423f82c` | `4a008` | `100` |
| 8845 | `gcc_qupv3_i2c_s0_clk` | `a423ef3c` | `a417f104` | `7004` | `7000` | `a423f86c` | `4a008` | `400` |
| 8845 | `gcc_qupv3_i2c_s1_clk` | `a423ef70` | `a417f104` | `7020` | `7000` | `a423f8ac` | `4a008` | `800` |
| 8845 | `gcc_qupv3_i2c_s2_clk` | `a423efa4` | `a417f104` | `703c` | `7000` | `a423f8ec` | `4a008` | `1000` |
| 8845 | `gcc_qupv3_i2c_s3_clk` | `a423efd8` | `a417f104` | `7058` | `7000` | `a423f92c` | `4a008` | `2000` |
| 8845 | `gcc_qupv3_i2c_s4_clk` | `a423f00c` | `a417f104` | `7074` | `7000` | `a423f96c` | `4a008` | `4000` |
| 8845 | `gcc_qupv3_wrap1_core_2x_clk` | `a423f040` | `a417f104` | `1315c` | `8000` | `a423f9ac` | `4a008` | `40000` |
| 8845 | `gcc_qupv3_wrap1_core_clk` | `a423f074` | `a417f104` | `13148` | `8000` | `a423f9ac` | `4a008` | `80000` |
| 8845 | `gcc_qupv3_wrap1_qspi_ref_clk` | `a423f0a8` | `a417f104` | `88bc` | `8000` | `a423f9ec` | `4a010` | `20000000` |
| 8845 | `gcc_qupv3_wrap1_s2_clk` | `a423f0dc` | `a417f104` | `827c` | `8000` | `a423f9ec` | `4a008` | `1000000` |
| 8845 | `gcc_qupv3_wrap1_s0_clk` | `a423f110` | `a417f104` | `8004` | `8000` | `a423fa2c` | `4a008` | `400000` |
| 8845 | `gcc_qupv3_wrap1_s1_clk` | `a423f144` | `a417f104` | `8140` | `8000` | `a423fa6c` | `4a008` | `800000` |
| 8845 | `gcc_qupv3_wrap1_s3_clk` | `a423f178` | `a417f104` | `8290` | `8000` | `a423faac` | `4a008` | `2000000` |
| 8845 | `gcc_qupv3_wrap1_s4_clk` | `a423f1ac` | `a417f104` | `83cc` | `8000` | `a423faec` | `4a008` | `4000000` |
| 8845 | `gcc_qupv3_wrap1_s5_clk` | `a423f1e0` | `a417f104` | `8508` | `8000` | `a423fb2c` | `4a008` | `8000000` |
| 8845 | `gcc_qupv3_wrap1_s6_clk` | `a423f214` | `a417f104` | `8644` | `8000` | `a423fb6c` | `4a008` | `10000000` |
| 8845 | `gcc_qupv3_wrap1_s7_clk` | `a423f248` | `a417f104` | `8780` | `8000` | `a423fbac` | `4a010` | `10000` |
| 8845 | `gcc_qupv3_wrap2_core_2x_clk` | `a423f27c` | `a417f104` | `132b4` | `e000` | `a423fbec` | `4a010` | `8` |
| 8845 | `gcc_qupv3_wrap2_core_clk` | `a423f2b0` | `a417f104` | `132a0` | `e000` | `a423fbec` | `4a010` | `1` |
| 8845 | `gcc_qupv3_wrap2_s0_clk` | `a423f2e4` | `a417f104` | `e004` | `e000` | `a423fc2c` | `4a010` | `10` |
| 8845 | `gcc_qupv3_wrap2_s1_clk` | `a423f318` | `a417f104` | `e140` | `e000` | `a423fc6c` | `4a010` | `20` |
| 8845 | `gcc_qupv3_wrap2_s2_clk` | `a423f34c` | `a417f104` | `e27c` | `e000` | `a423fcac` | `4a010` | `40` |
| 8845 | `gcc_qupv3_wrap2_s3_clk` | `a423f380` | `a417f104` | `e3b8` | `e000` | `a423fcec` | `4a010` | `80` |
| 8845 | `gcc_qupv3_wrap2_s4_clk` | `a423f3b4` | `a417f104` | `e4f4` | `e000` | `a423fd2c` | `4a010` | `100` |
| 8845 | `gcc_qupv3_wrap2_s5_clk` | `a423f3e8` | `a417f104` | `e630` | `e000` | `a423fd6c` | `4a010` | `200` |
| 8845 | `gcc_qupv3_wrap2_s6_clk` | `a423f41c` | `a417f104` | `e76c` | `e000` | `a423fdac` | `4a010` | `400` |
| 8845 | `gcc_qupv3_wrap2_s7_clk` | `a423f450` | `a417f104` | `e8a8` | `e000` | `a423fdec` | `4a010` | `800` |
| 8845 | `gcc_qupv3_wrap3_core_2x_clk` | `a423f484` | `a417f104` | `1340c` | `98000` | `a423fe2c` | `4a018` | `800` |
| 8845 | `gcc_qupv3_wrap3_core_clk` | `a423f4b8` | `a417f104` | `133f8` | `98000` | `a423fe2c` | `4a018` | `400` |
| 8845 | `gcc_qupv3_wrap3_ibi_ctrl_1_clk` | `a423f4ec` | `a417f104` | `98774` | `98000` | `a423fe6c` | `4a018` | `100000` |
| 8845 | `gcc_qupv3_wrap3_ibi_ctrl_2_clk` | `a423f520` | `a417f104` | `98778` | `98000` | `a423fe6c` | `4a018` | `200000` |
| 8845 | `gcc_qupv3_wrap3_s0_clk` | `a423f554` | `a417f104` | `98004` | `98000` | `a423feac` | `4a018` | `1000` |
| 8845 | `gcc_qupv3_wrap3_s1_clk` | `a423f588` | `a417f104` | `98140` | `98000` | `a423feec` | `4a018` | `2000` |
| 8845 | `gcc_qupv3_wrap3_s2_clk` | `a423f5bc` | `a417f104` | `9827c` | `98000` | `a423ff2c` | `4a018` | `4000` |
| 8845 | `gcc_qupv3_wrap3_s3_clk` | `a423f5f0` | `a417f104` | `983b8` | `98000` | `a423ff6c` | `4a018` | `8000` |
| 8845 | `gcc_qupv3_wrap3_s4_clk` | `a423f624` | `a417f104` | `984f4` | `98000` | `a423ffac` | `4a018` | `10000` |
| 8845 | `gcc_qupv3_wrap3_s5_clk` | `a423f658` | `a417f104` | `98630` | `98000` | `a423ffec` | `4a018` | `20000` |

## Root tables

Each row gives descriptor, slot, CMD offset, type, default parent, rate table, rate count, and field widths.
A null slot has no MMIO root. Such a record represents a reference source.
Rate count and source count are separate descriptor fields.
The field column gives M/N width followed by DFS level count.

| SoC | Descriptor | Slot | CMD | Type | Default parent | Rates | Count | Fields |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 8850 | `a0212600` | `00000000` | `ffffffff` | reference | `00000000` | `00000000` | 0 | 0 / 0 |
| 8850 | `a0212668` | `a014f0ec` | `a010` | RCG | `a0213204` | `a021309c` | 9 | 0 / 0 |
| 8850 | `a02126b4` | `a014f0ec` | `a044` | RCG | `a0213204` | `a0213360` | 1 | 0 / 0 |
| 8850 | `a0212700` | `a014f0ec` | `a07c` | RCG | `a0213204` | `a0213388` | 8 | 0 / 0 |
| 8850 | `a021274c` | `a014f0ec` | `a060` | RCG | `a0213204` | `a0213360` | 1 | 0 / 0 |
| 8850 | `a0212798` | `a014f0ec` | `d4` | RCG | `a0213204` | `a02134c8` | 1 | 16 / 0 |
| 8850 | `a02127e4` | `a014f0ec` | `ec` | RCG | `a0213204` | `a02134c8` | 1 | 16 / 0 |
| 8850 | `a0212830` | `a014f0ec` | `378` | RCG | `a0213204` | `a02134f0` | 8 | 0 / 0 |
| 8850 | `a021287c` | `a014f0ec` | `194` | RCG | `a0213204` | `a0213360` | 1 | 0 / 0 |
| 8850 | `a02128c8` | `a014f0ec` | `1b0` | RCG | `a0213204` | `a0213360` | 1 | 0 / 0 |
| 8850 | `a0212914` | `a014f0ec` | `248` | RCG | `a0213204` | `a0213360` | 1 | 0 / 0 |
| 8850 | `a0212960` | `a014f0ec` | `1fc` | RCG | `a0213204` | `a0213360` | 1 | 0 / 0 |
| 8850 | `a02129ac` | `a014f0ec` | `218` | RCG | `a0213204` | `a0213360` | 1 | 16 / 0 |
| 8850 | `a02129f8` | `a014f0ec` | `230` | RCG | `a0213204` | `a0213360` | 1 | 16 / 0 |
| 8850 | `a0212a44` | `a014f0ec` | `2ac` | RCG | `a0213204` | `a0213360` | 1 | 0 / 0 |
| 8850 | `a0212a90` | `a014f0ec` | `290` | RCG | `a0213204` | `a0213360` | 1 | 0 / 0 |
| 8850 | `a0212adc` | `a014f0ec` | `260` | RCG | `a0213204` | `a0213360` | 1 | 16 / 0 |
| 8850 | `a0212b28` | `a014f0ec` | `278` | RCG | `a0213204` | `a0213360` | 1 | 16 / 0 |
| 8850 | `a0212b74` | `a014f0ec` | `310` | RCG | `a0213204` | `a0213360` | 1 | 0 / 0 |
| 8850 | `a0212bc0` | `a014f0ec` | `2c4` | RCG | `a0213204` | `00000000` | 0 | 0 / 0 |
| 8850 | `a0212c0c` | `a014f0ec` | `2e0` | RCG | `a0213204` | `a0213360` | 1 | 16 / 0 |
| 8850 | `a0212c58` | `a014f0ec` | `2f8` | RCG | `a0213204` | `a0213360` | 1 | 16 / 0 |
| 8850 | `a0212ca4` | `a014f0ec` | `35c` | RCG | `a0213204` | `a0213360` | 1 | 0 / 0 |
| 8850 | `a0212cf0` | `a014f0ec` | `340` | RCG | `a0213204` | `a0213360` | 1 | 0 / 0 |
| 8850 | `a0212d3c` | `a014f0ec` | `328` | RCG | `a0213204` | `a0213360` | 1 | 16 / 0 |
| 8850 | `a0212d88` | `a014f0ec` | `1cc` | RCG | `a0213204` | `a0213360` | 1 | 0 / 0 |
| 8850 | `a0212dd4` | `a014f0ec` | `1e4` | RCG | `a0213204` | `a0213360` | 1 | 0 / 0 |
| 8850 | `a0212e20` | `a014f0ec` | `164` | RCG | `a0213204` | `a0213630` | 9 | 0 / 0 |
| 8850 | `a0212e6c` | `a014f0ec` | `11c` | RCG | `a0213204` | `a0213360` | 1 | 8 / 0 |
| 8850 | `a0212eb8` | `a014f0ec` | `134` | RCG | `a0213204` | `a0213360` | 1 | 8 / 0 |
| 8850 | `a0212f04` | `a014f0ec` | `14c` | RCG | `a0213204` | `a0213360` | 1 | 8 / 0 |
| 8850 | `a0212f50` | `a014f0ec` | `17c` | RCG | `a0213204` | `a0213360` | 1 | 0 / 0 |
| 8850 | `a0212f9c` | `a014f0ec` | `104` | RCG | `00000000` | `a021380c` | 1 | 0 / 0 |
| 8850 | `a0212fe8` | `a014f0ec` | `9004` | divider | `a0213204` | `00000000` | 0 | 0 / 0 |
| 8850 | `a0213034` | `a014f0ec` | `6044` | RCG | `a0213204` | `a0213360` | 1 | 0 / 0 |
| 8850 | `a02146e8` | `a014f104` | `7206c` | RCG | `a0213204` | `00000000` | 0 | 0 / 0 |
| 8850 | `a0214734` | `a014f104` | `64400` | RCG | `a0213204` | `00000000` | 0 | 0 / 0 |
| 8850 | `a0214780` | `a014f104` | `c0c8` | RCG | `a0213204` | `00000000` | 0 | 0 / 0 |
| 8850 | `a02147cc` | `a014f104` | `c1f8` | RCG | `a0213204` | `00000000` | 0 | 0 / 0 |
| 8850 | `a0214818` | `a014f104` | `13014` | RCG | `a0213204` | `00000000` | 0 | 0 / 0 |
| 8850 | `a0214864` | `a014f104` | `7008` | RCG | `a0213204` | `a0213360` | 1 | 0 / 0 |
| 8850 | `a02148b0` | `a014f104` | `7024` | RCG | `a0213204` | `a0213360` | 1 | 0 / 0 |
| 8850 | `a02148fc` | `a014f104` | `7040` | RCG | `a0213204` | `a0213360` | 1 | 0 / 0 |
| 8850 | `a0214948` | `a014f104` | `705c` | RCG | `a0213204` | `a0213360` | 1 | 0 / 0 |
| 8850 | `a0214994` | `a014f104` | `7078` | RCG | `a0213204` | `a0213360` | 1 | 0 / 0 |
| 8850 | `a02149e0` | `a014f104` | `1316c` | RCG | `a0213204` | `00000000` | 0 | 0 / 0 |
| 8850 | `a0214a2c` | `a014f104` | `88c0` | DFS RCG | `a0213204` | `a021527c` | 19 | 16 / 8 |
| 8850 | `a0214a78` | `a014f104` | `8014` | DFS RCG | `a0213204` | `a02155e8` | 14 | 16 / 8 |
| 8850 | `a0214ac4` | `a014f104` | `8150` | DFS RCG | `a0213204` | `a02155e8` | 14 | 16 / 8 |
| 8850 | `a0214b10` | `a014f104` | `82a0` | DFS RCG | `a0213204` | `a02155e8` | 14 | 16 / 8 |
| 8850 | `a0214b5c` | `a014f104` | `83dc` | DFS RCG | `a0213204` | `a0215818` | 18 | 16 / 8 |
| 8850 | `a0214ba8` | `a014f104` | `8518` | DFS RCG | `a0213204` | `a02155e8` | 14 | 16 / 8 |
| 8850 | `a0214bf4` | `a014f104` | `8654` | DFS RCG | `a0213204` | `a02155e8` | 14 | 16 / 8 |
| 8850 | `a0214c40` | `a014f104` | `8790` | DFS RCG | `a0213204` | `a0215818` | 18 | 16 / 8 |
| 8850 | `a0214c8c` | `a014f104` | `132c4` | RCG | `a0213204` | `00000000` | 0 | 0 / 0 |
| 8850 | `a0214cd8` | `a014f104` | `e014` | DFS RCG | `a0213204` | `a0215818` | 18 | 16 / 8 |
| 8850 | `a0214d24` | `a014f104` | `e150` | DFS RCG | `a0213204` | `a0215818` | 18 | 16 / 8 |
| 8850 | `a0214d70` | `a014f104` | `e28c` | DFS RCG | `a0213204` | `a0215818` | 18 | 16 / 8 |
| 8850 | `a0214dbc` | `a014f104` | `e3c8` | DFS RCG | `a0213204` | `a02155e8` | 14 | 16 / 8 |
| 8850 | `a0214e08` | `a014f104` | `e504` | DFS RCG | `a0213204` | `a02155e8` | 14 | 16 / 8 |
| 8850 | `a0214e54` | `a014f104` | `1341c` | RCG | `a0213204` | `00000000` | 0 | 0 / 0 |
| 8850 | `a0214ea0` | `a014f104` | `9877c` | RCG | `a0213204` | `a0215ae8` | 1 | 0 / 0 |
| 8850 | `a0214eec` | `a014f104` | `98014` | DFS RCG | `a0213204` | `a02155e8` | 14 | 16 / 8 |
| 8850 | `a0214f38` | `a014f104` | `98150` | DFS RCG | `a0213204` | `a0215818` | 18 | 16 / 8 |
| 8850 | `a0214f84` | `a014f104` | `9828c` | DFS RCG | `a0213204` | `a0215818` | 18 | 16 / 8 |
| 8850 | `a0214fd0` | `a014f104` | `983c8` | DFS RCG | `a0213204` | `a02155e8` | 14 | 16 / 8 |
| 8850 | `a021501c` | `a014f104` | `98504` | DFS RCG | `a0213204` | `a02155e8` | 14 | 16 / 8 |
| 8850 | `a0215068` | `a014f104` | `98640` | DFS RCG | `a0213204` | `a0215b10` | 19 | 16 / 8 |
| 8850 | `a02150b4` | `a014f104` | `13574` | RCG | `a0213204` | `00000000` | 0 | 0 / 0 |
| 8850 | `a0215100` | `a014f104` | `99014` | DFS RCG | `a0213204` | `a0215818` | 18 | 16 / 8 |
| 8850 | `a021514c` | `a014f104` | `99150` | DFS RCG | `a0213204` | `a0215818` | 18 | 16 / 8 |
| 8850 | `a0215198` | `a014f104` | `9928c` | DFS RCG | `a0213204` | `a0215818` | 18 | 16 / 8 |
| 8850 | `a02151e4` | `a014f104` | `993c8` | DFS RCG | `a0213204` | `a02155e8` | 14 | 16 / 8 |
| 8850 | `a0215230` | `a014f104` | `99504` | DFS RCG | `a0213204` | `a02155e8` | 14 | 16 / 8 |
| 8845 | `a423db88` | `00000000` | `ffffffff` | reference | `00000000` | `00000000` | 0 | 0 / 0 |
| 8845 | `a423dbe0` | `a417f0ec` | `a010` | RCG | `a423e598` | `a423e478` | 9 | 0 / 0 |
| 8845 | `a423dc20` | `a417f0ec` | `a044` | RCG | `a423e598` | `a423e6ac` | 1 | 0 / 0 |
| 8845 | `a423dc60` | `a417f0ec` | `a07c` | RCG | `a423e598` | `a423e6cc` | 8 | 0 / 0 |
| 8845 | `a423dca0` | `a417f0ec` | `a060` | RCG | `a423e598` | `a423e6ac` | 1 | 0 / 0 |
| 8845 | `a423dce0` | `a417f0ec` | `d4` | RCG | `a423e598` | `a423e7cc` | 1 | 16 / 0 |
| 8845 | `a423dd20` | `a417f0ec` | `ec` | RCG | `a423e598` | `a423e7cc` | 1 | 16 / 0 |
| 8845 | `a423dd60` | `a417f0ec` | `378` | RCG | `a423e598` | `a423e7ec` | 8 | 0 / 0 |
| 8845 | `a423dda0` | `a417f0ec` | `194` | RCG | `a423e598` | `a423e6ac` | 1 | 0 / 0 |
| 8845 | `a423dde0` | `a417f0ec` | `1b0` | RCG | `a423e598` | `a423e6ac` | 1 | 0 / 0 |
| 8845 | `a423de20` | `a417f0ec` | `248` | RCG | `a423e598` | `a423e6ac` | 1 | 0 / 0 |
| 8845 | `a423de60` | `a417f0ec` | `1fc` | RCG | `a423e598` | `a423e6ac` | 1 | 0 / 0 |
| 8845 | `a423dea0` | `a417f0ec` | `218` | RCG | `a423e598` | `a423e6ac` | 1 | 16 / 0 |
| 8845 | `a423dee0` | `a417f0ec` | `230` | RCG | `a423e598` | `a423e6ac` | 1 | 16 / 0 |
| 8845 | `a423df20` | `a417f0ec` | `2ac` | RCG | `a423e598` | `a423e6ac` | 1 | 0 / 0 |
| 8845 | `a423df60` | `a417f0ec` | `290` | RCG | `a423e598` | `a423e6ac` | 1 | 0 / 0 |
| 8845 | `a423dfa0` | `a417f0ec` | `260` | RCG | `a423e598` | `a423e6ac` | 1 | 16 / 0 |
| 8845 | `a423dfe0` | `a417f0ec` | `278` | RCG | `a423e598` | `a423e6ac` | 1 | 16 / 0 |
| 8845 | `a423e020` | `a417f0ec` | `310` | RCG | `a423e598` | `a423e6ac` | 1 | 0 / 0 |
| 8845 | `a423e060` | `a417f0ec` | `2c4` | RCG | `a423e598` | `a423e6ac` | 1 | 0 / 0 |
| 8845 | `a423e0a0` | `a417f0ec` | `2e0` | RCG | `a423e598` | `a423e6ac` | 1 | 16 / 0 |
| 8845 | `a423e0e0` | `a417f0ec` | `2f8` | RCG | `a423e598` | `a423e6ac` | 1 | 16 / 0 |
| 8845 | `a423e120` | `a417f0ec` | `35c` | RCG | `a423e598` | `a423e6ac` | 1 | 0 / 0 |
| 8845 | `a423e160` | `a417f0ec` | `340` | RCG | `a423e598` | `a423e6ac` | 1 | 0 / 0 |
| 8845 | `a423e1a0` | `a417f0ec` | `328` | RCG | `a423e598` | `a423e6ac` | 1 | 16 / 0 |
| 8845 | `a423e1e0` | `a417f0ec` | `1cc` | RCG | `a423e598` | `a423e6ac` | 1 | 0 / 0 |
| 8845 | `a423e220` | `a417f0ec` | `1e4` | RCG | `a423e598` | `a423e6ac` | 1 | 0 / 0 |
| 8845 | `a423e260` | `a417f0ec` | `164` | RCG | `a423e598` | `a423e8ec` | 9 | 0 / 0 |
| 8845 | `a423e2a0` | `a417f0ec` | `11c` | RCG | `a423e598` | `a423e6ac` | 1 | 8 / 0 |
| 8845 | `a423e2e0` | `a417f0ec` | `134` | RCG | `a423e598` | `a423e6ac` | 1 | 8 / 0 |
| 8845 | `a423e320` | `a417f0ec` | `14c` | RCG | `a423e598` | `a423e6ac` | 1 | 8 / 0 |
| 8845 | `a423e360` | `a417f0ec` | `17c` | RCG | `a423e598` | `a423e6ac` | 1 | 0 / 0 |
| 8845 | `a423e3a0` | `a417f0ec` | `104` | RCG | `00000000` | `a423ea68` | 1 | 0 / 0 |
| 8845 | `a423e3e0` | `a417f0ec` | `9004` | divider | `a423e598` | `00000000` | 0 | 0 / 0 |
| 8845 | `a423e420` | `a417f0ec` | `6044` | RCG | `a423e598` | `a423e6ac` | 1 | 0 / 0 |
| 8845 | `a423f72c` | `a417f104` | `7206c` | RCG | `a423e598` | `00000000` | 0 | 0 / 0 |
| 8845 | `a423f76c` | `a417f104` | `64400` | RCG | `a423e598` | `00000000` | 0 | 0 / 0 |
| 8845 | `a423f7ac` | `a417f104` | `c0c8` | RCG | `a423e598` | `00000000` | 0 | 0 / 0 |
| 8845 | `a423f7ec` | `a417f104` | `c1f8` | RCG | `a423e598` | `00000000` | 0 | 0 / 0 |
| 8845 | `a423f82c` | `a417f104` | `13014` | RCG | `a423e598` | `00000000` | 0 | 0 / 0 |
| 8845 | `a423f86c` | `a417f104` | `7008` | RCG | `a423e598` | `a423e6ac` | 1 | 0 / 0 |
| 8845 | `a423f8ac` | `a417f104` | `7024` | RCG | `a423e598` | `a423e6ac` | 1 | 0 / 0 |
| 8845 | `a423f8ec` | `a417f104` | `7040` | RCG | `a423e598` | `a423e6ac` | 1 | 0 / 0 |
| 8845 | `a423f92c` | `a417f104` | `705c` | RCG | `a423e598` | `a423e6ac` | 1 | 0 / 0 |
| 8845 | `a423f96c` | `a417f104` | `7078` | RCG | `a423e598` | `a423e6ac` | 1 | 0 / 0 |
| 8845 | `a423f9ac` | `a417f104` | `1316c` | RCG | `a423e598` | `00000000` | 0 | 0 / 0 |
| 8845 | `a423f9ec` | `a417f104` | `88c0` | DFS RCG | `a423e598` | `a424002c` | 19 | 16 / 8 |
| 8845 | `a423fa2c` | `a417f104` | `8014` | DFS RCG | `a423e598` | `a42402e8` | 18 | 16 / 8 |
| 8845 | `a423fa6c` | `a417f104` | `8150` | DFS RCG | `a423e598` | `a42402e8` | 18 | 16 / 8 |
| 8845 | `a423faac` | `a417f104` | `82a0` | DFS RCG | `a423e598` | `a4240528` | 14 | 16 / 8 |
| 8845 | `a423faec` | `a417f104` | `83dc` | DFS RCG | `a423e598` | `a42402e8` | 18 | 16 / 8 |
| 8845 | `a423fb2c` | `a417f104` | `8518` | DFS RCG | `a423e598` | `a4240528` | 14 | 16 / 8 |
| 8845 | `a423fb6c` | `a417f104` | `8654` | DFS RCG | `a423e598` | `a4240528` | 14 | 16 / 8 |
| 8845 | `a423fbac` | `a417f104` | `8790` | DFS RCG | `a423e598` | `a4240528` | 14 | 16 / 8 |
| 8845 | `a423fbec` | `a417f104` | `132c4` | RCG | `a423e598` | `00000000` | 0 | 0 / 0 |
| 8845 | `a423fc2c` | `a417f104` | `e014` | DFS RCG | `a423e598` | `a42402e8` | 18 | 16 / 8 |
| 8845 | `a423fc6c` | `a417f104` | `e150` | DFS RCG | `a423e598` | `a42406e8` | 18 | 16 / 8 |
| 8845 | `a423fcac` | `a417f104` | `e28c` | DFS RCG | `a423e598` | `a42402e8` | 18 | 16 / 8 |
| 8845 | `a423fcec` | `a417f104` | `e3c8` | DFS RCG | `a423e598` | `a42402e8` | 18 | 16 / 8 |
| 8845 | `a423fd2c` | `a417f104` | `e504` | DFS RCG | `a423e598` | `a4240528` | 14 | 16 / 8 |
| 8845 | `a423fd6c` | `a417f104` | `e640` | DFS RCG | `a423e598` | `a4240528` | 14 | 16 / 8 |
| 8845 | `a423fdac` | `a417f104` | `e77c` | DFS RCG | `a423e598` | `a4240928` | 19 | 16 / 8 |
| 8845 | `a423fdec` | `a417f104` | `e8b8` | DFS RCG | `a423e598` | `a42402e8` | 18 | 16 / 8 |
| 8845 | `a423fe2c` | `a417f104` | `1341c` | RCG | `a423e598` | `00000000` | 0 | 0 / 0 |
| 8845 | `a423fe6c` | `a417f104` | `9877c` | RCG | `a423e598` | `a4240b88` | 1 | 0 / 0 |
| 8845 | `a423feac` | `a417f104` | `98014` | DFS RCG | `a423e598` | `a42402e8` | 18 | 16 / 8 |
| 8845 | `a423feec` | `a417f104` | `98150` | DFS RCG | `a423e598` | `a42402e8` | 18 | 16 / 8 |
| 8845 | `a423ff2c` | `a417f104` | `9828c` | DFS RCG | `a423e598` | `a4240528` | 14 | 16 / 8 |
| 8845 | `a423ff6c` | `a417f104` | `983c8` | DFS RCG | `a423e598` | `a4240528` | 14 | 16 / 8 |
| 8845 | `a423ffac` | `a417f104` | `98504` | DFS RCG | `a423e598` | `a42402e8` | 18 | 16 / 8 |
| 8845 | `a423ffec` | `a417f104` | `98640` | DFS RCG | `a423e598` | `a4240928` | 19 | 16 / 8 |

## Decoded rate tables

SM8850 uses 40-byte records. SM8845 uses 32-byte records.
Each unique table occurs once below, even when several roots share it.
The parent column identifies the source descriptor.
The PLL configuration column identifies the requested PLL rate record.
Zero means no extra PLL programming record.

| SoC | Rate entry | Hz | Parent | Selector | Twice divisor | M | N | PLL configuration |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 8850 | `a021309c` | 19200000 | `a0213204` | 0 | 2 | 0 | 0 | `00000000` |
| 8850 | `a02130c4` | 300000000 | `a0213278` | 4 | 2 | 0 | 0 | `a012a598` |
| 8850 | `a02130ec` | 360000000 | `a0213278` | 4 | 2 | 0 | 0 | `a012a5cc` |
| 8850 | `a0213114` | 495000000 | `a0213278` | 4 | 2 | 0 | 0 | `a012a600` |
| 8850 | `a021313c` | 600000000 | `a0213278` | 4 | 2 | 0 | 0 | `a012a634` |
| 8850 | `a0213164` | 600000000 | `a02132ec` | 1 | 2 | 0 | 0 | `00000000` |
| 8850 | `a021318c` | 700000000 | `a0213278` | 4 | 2 | 0 | 0 | `a012a668` |
| 8850 | `a02131b4` | 785000000 | `a0213278` | 4 | 2 | 0 | 0 | `a012a69c` |
| 8850 | `a02131dc` | 850000000 | `a0213278` | 4 | 2 | 0 | 0 | `a012a6d0` |
| 8850 | `a0213360` | 19200000 | `a0213204` | 0 | 2 | 0 | 0 | `00000000` |
| 8850 | `a0213388` | 19200000 | `a0213204` | 0 | 2 | 0 | 0 | `00000000` |
| 8850 | `a02133b0` | 100000000 | `a0213278` | 4 | 6 | 0 | 0 | `a012a598` |
| 8850 | `a02133d8` | 120000000 | `a0213278` | 4 | 6 | 0 | 0 | `a012a5cc` |
| 8850 | `a0213400` | 165000000 | `a0213278` | 4 | 6 | 0 | 0 | `a012a600` |
| 8850 | `a0213428` | 200000000 | `a0213278` | 4 | 6 | 0 | 0 | `a012a634` |
| 8850 | `a0213450` | 233333333 | `a0213278` | 4 | 6 | 0 | 0 | `a012a668` |
| 8850 | `a0213478` | 261666667 | `a0213278` | 4 | 6 | 0 | 0 | `a012a69c` |
| 8850 | `a02134a0` | 283333333 | `a0213278` | 4 | 6 | 0 | 0 | `a012a6d0` |
| 8850 | `a02134c8` | 19200000 | `a0213204` | 0 | 2 | 0 | 0 | `00000000` |
| 8850 | `a02134f0` | 19200000 | `a0213204` | 0 | 2 | 0 | 0 | `00000000` |
| 8850 | `a0213518` | 100000000 | `a0213278` | 6 | 6 | 0 | 0 | `a012a598` |
| 8850 | `a0213540` | 120000000 | `a0213278` | 6 | 6 | 0 | 0 | `a012a5cc` |
| 8850 | `a0213568` | 165000000 | `a0213278` | 6 | 6 | 0 | 0 | `a012a600` |
| 8850 | `a0213590` | 200000000 | `a0213278` | 6 | 6 | 0 | 0 | `a012a634` |
| 8850 | `a02135b8` | 233333333 | `a0213278` | 6 | 6 | 0 | 0 | `a012a668` |
| 8850 | `a02135e0` | 261666667 | `a0213278` | 6 | 6 | 0 | 0 | `a012a69c` |
| 8850 | `a0213608` | 283333333 | `a0213278` | 6 | 6 | 0 | 0 | `a012a6d0` |
| 8850 | `a0213630` | 85714286 | `a0213798` | 1 | 6 | 0 | 0 | `a012aab0` |
| 8850 | `a0213658` | 100000000 | `a0213798` | 1 | 6 | 0 | 0 | `a012aae4` |
| 8850 | `a0213680` | 156000000 | `a0213798` | 1 | 6 | 0 | 0 | `a012ab18` |
| 8850 | `a02136a8` | 207000000 | `a0213798` | 1 | 6 | 0 | 0 | `a012ab4c` |
| 8850 | `a02136d0` | 337000000 | `a0213798` | 1 | 6 | 0 | 0 | `a012ab80` |
| 8850 | `a02136f8` | 417000000 | `a0213798` | 1 | 6 | 0 | 0 | `a012abb4` |
| 8850 | `a0213720` | 532000000 | `a0213798` | 1 | 6 | 0 | 0 | `a012abe8` |
| 8850 | `a0213748` | 600000000 | `a0213798` | 1 | 6 | 0 | 0 | `a012ac1c` |
| 8850 | `a0213770` | 650000000 | `a0213798` | 1 | 6 | 0 | 0 | `a012ac50` |
| 8850 | `a021380c` | 38400000 | `a0213834` | 2 | 2 | 0 | 0 | `00000000` |
| 8850 | `a021527c` | 7372800 | `a02132ec` | 6 | 2 | 384 | 15625 | `00000000` |
| 8850 | `a02152a4` | 14745600 | `a02132ec` | 6 | 2 | 768 | 15625 | `00000000` |
| 8850 | `a02152cc` | 19200000 | `a0213204` | 0 | 2 | 0 | 0 | `00000000` |
| 8850 | `a02152f4` | 29491200 | `a02132ec` | 6 | 2 | 1536 | 15625 | `00000000` |
| 8850 | `a021531c` | 32000000 | `a02132ec` | 6 | 2 | 8 | 75 | `00000000` |
| 8850 | `a0215344` | 48000000 | `a02132ec` | 6 | 2 | 4 | 25 | `00000000` |
| 8850 | `a021536c` | 51200000 | `a02132ec` | 6 | 2 | 64 | 375 | `00000000` |
| 8850 | `a0215394` | 64000000 | `a02132ec` | 6 | 2 | 16 | 75 | `00000000` |
| 8850 | `a02153bc` | 66666667 | `a02132ec` | 1 | 18 | 0 | 0 | `00000000` |
| 8850 | `a02153e4` | 75000000 | `a02132ec` | 6 | 8 | 0 | 0 | `00000000` |
| 8850 | `a021540c` | 80000000 | `a02132ec` | 6 | 2 | 4 | 15 | `00000000` |
| 8850 | `a0215434` | 96000000 | `a02132ec` | 6 | 2 | 8 | 25 | `00000000` |
| 8850 | `a021545c` | 100000000 | `a02132ec` | 1 | 12 | 0 | 0 | `00000000` |
| 8850 | `a0215484` | 102400000 | `a02132ec` | 6 | 2 | 128 | 375 | `00000000` |
| 8850 | `a02154ac` | 112000000 | `a02132ec` | 6 | 2 | 28 | 75 | `00000000` |
| 8850 | `a02154d4` | 117964800 | `a02132ec` | 6 | 2 | 6144 | 15625 | `00000000` |
| 8850 | `a02154fc` | 120000000 | `a02132ec` | 1 | 10 | 0 | 0 | `00000000` |
| 8850 | `a0215524` | 150000000 | `a02132ec` | 6 | 4 | 0 | 0 | `00000000` |
| 8850 | `a021554c` | 250000000 | `a0215574` | 2 | 4 | 0 | 0 | `00000000` |
| 8850 | `a02155e8` | 7372800 | `a02132ec` | 6 | 2 | 384 | 15625 | `00000000` |
| 8850 | `a0215610` | 14745600 | `a02132ec` | 6 | 2 | 768 | 15625 | `00000000` |
| 8850 | `a0215638` | 19200000 | `a0213204` | 0 | 2 | 0 | 0 | `00000000` |
| 8850 | `a0215660` | 29491200 | `a02132ec` | 6 | 2 | 1536 | 15625 | `00000000` |
| 8850 | `a0215688` | 32000000 | `a02132ec` | 6 | 2 | 8 | 75 | `00000000` |
| 8850 | `a02156b0` | 48000000 | `a02132ec` | 6 | 2 | 4 | 25 | `00000000` |
| 8850 | `a02156d8` | 51200000 | `a02132ec` | 6 | 2 | 64 | 375 | `00000000` |
| 8850 | `a0215700` | 60000000 | `a02132ec` | 6 | 10 | 0 | 0 | `00000000` |
| 8850 | `a0215728` | 64000000 | `a02132ec` | 6 | 2 | 16 | 75 | `00000000` |
| 8850 | `a0215750` | 66666667 | `a02132ec` | 1 | 18 | 0 | 0 | `00000000` |
| 8850 | `a0215778` | 75000000 | `a02132ec` | 6 | 8 | 0 | 0 | `00000000` |
| 8850 | `a02157a0` | 80000000 | `a02132ec` | 6 | 2 | 4 | 15 | `00000000` |
| 8850 | `a02157c8` | 96000000 | `a02132ec` | 6 | 2 | 8 | 25 | `00000000` |
| 8850 | `a02157f0` | 100000000 | `a02132ec` | 1 | 12 | 0 | 0 | `00000000` |
| 8850 | `a0215818` | 7372800 | `a02132ec` | 6 | 2 | 384 | 15625 | `00000000` |
| 8850 | `a0215840` | 14745600 | `a02132ec` | 6 | 2 | 768 | 15625 | `00000000` |
| 8850 | `a0215868` | 19200000 | `a0213204` | 0 | 2 | 0 | 0 | `00000000` |
| 8850 | `a0215890` | 29491200 | `a02132ec` | 6 | 2 | 1536 | 15625 | `00000000` |
| 8850 | `a02158b8` | 32000000 | `a02132ec` | 6 | 2 | 8 | 75 | `00000000` |
| 8850 | `a02158e0` | 48000000 | `a02132ec` | 6 | 2 | 4 | 25 | `00000000` |
| 8850 | `a0215908` | 51200000 | `a02132ec` | 6 | 2 | 64 | 375 | `00000000` |
| 8850 | `a0215930` | 60000000 | `a02132ec` | 6 | 10 | 0 | 0 | `00000000` |
| 8850 | `a0215958` | 64000000 | `a02132ec` | 6 | 2 | 16 | 75 | `00000000` |
| 8850 | `a0215980` | 66666667 | `a02132ec` | 1 | 18 | 0 | 0 | `00000000` |
| 8850 | `a02159a8` | 75000000 | `a02132ec` | 6 | 8 | 0 | 0 | `00000000` |
| 8850 | `a02159d0` | 80000000 | `a02132ec` | 6 | 2 | 4 | 15 | `00000000` |
| 8850 | `a02159f8` | 96000000 | `a02132ec` | 6 | 2 | 8 | 25 | `00000000` |
| 8850 | `a0215a20` | 100000000 | `a02132ec` | 1 | 12 | 0 | 0 | `00000000` |
| 8850 | `a0215a48` | 102400000 | `a02132ec` | 6 | 2 | 128 | 375 | `00000000` |
| 8850 | `a0215a70` | 112000000 | `a02132ec` | 6 | 2 | 28 | 75 | `00000000` |
| 8850 | `a0215a98` | 117964800 | `a02132ec` | 6 | 2 | 6144 | 15625 | `00000000` |
| 8850 | `a0215ac0` | 120000000 | `a02132ec` | 1 | 10 | 0 | 0 | `00000000` |
| 8850 | `a0215ae8` | 37500000 | `a02132ec` | 6 | 16 | 0 | 0 | `00000000` |
| 8850 | `a0215b10` | 7372800 | `a02132ec` | 6 | 2 | 384 | 15625 | `00000000` |
| 8850 | `a0215b38` | 14745600 | `a02132ec` | 6 | 2 | 768 | 15625 | `00000000` |
| 8850 | `a0215b60` | 19200000 | `a0213204` | 0 | 2 | 0 | 0 | `00000000` |
| 8850 | `a0215b88` | 29491200 | `a02132ec` | 6 | 2 | 1536 | 15625 | `00000000` |
| 8850 | `a0215bb0` | 32000000 | `a02132ec` | 6 | 2 | 8 | 75 | `00000000` |
| 8850 | `a0215bd8` | 48000000 | `a02132ec` | 6 | 2 | 4 | 25 | `00000000` |
| 8850 | `a0215c00` | 51200000 | `a02132ec` | 6 | 2 | 64 | 375 | `00000000` |
| 8850 | `a0215c28` | 60000000 | `a02132ec` | 6 | 10 | 0 | 0 | `00000000` |
| 8850 | `a0215c50` | 64000000 | `a02132ec` | 6 | 2 | 16 | 75 | `00000000` |
| 8850 | `a0215c78` | 66666667 | `a02132ec` | 1 | 18 | 0 | 0 | `00000000` |
| 8850 | `a0215ca0` | 75000000 | `a02132ec` | 6 | 8 | 0 | 0 | `00000000` |
| 8850 | `a0215cc8` | 80000000 | `a02132ec` | 6 | 2 | 4 | 15 | `00000000` |
| 8850 | `a0215cf0` | 96000000 | `a02132ec` | 6 | 2 | 8 | 25 | `00000000` |
| 8850 | `a0215d18` | 100000000 | `a02132ec` | 1 | 12 | 0 | 0 | `00000000` |
| 8850 | `a0215d40` | 102400000 | `a02132ec` | 6 | 2 | 128 | 375 | `00000000` |
| 8850 | `a0215d68` | 112000000 | `a02132ec` | 6 | 2 | 28 | 75 | `00000000` |
| 8850 | `a0215d90` | 117964800 | `a02132ec` | 6 | 2 | 6144 | 15625 | `00000000` |
| 8850 | `a0215db8` | 120000000 | `a02132ec` | 1 | 10 | 0 | 0 | `00000000` |
| 8850 | `a0215de0` | 128000000 | `a02132ec` | 1 | 2 | 16 | 75 | `00000000` |
| 8845 | `a423e478` | 19200000 | `a423e598` | 0 | 2 | 0 | 0 | `00000000` |
| 8845 | `a423e498` | 300000000 | `a423e5f4` | 4 | 2 | 0 | 0 | `a415a590` |
| 8845 | `a423e4b8` | 360000000 | `a423e5f4` | 4 | 2 | 0 | 0 | `a415a5c0` |
| 8845 | `a423e4d8` | 495000000 | `a423e5f4` | 4 | 2 | 0 | 0 | `a415a5f0` |
| 8845 | `a423e4f8` | 600000000 | `a423e5f4` | 4 | 2 | 0 | 0 | `a415a620` |
| 8845 | `a423e518` | 600000000 | `a423e650` | 1 | 2 | 0 | 0 | `00000000` |
| 8845 | `a423e538` | 700000000 | `a423e5f4` | 4 | 2 | 0 | 0 | `a415a650` |
| 8845 | `a423e558` | 785000000 | `a423e5f4` | 4 | 2 | 0 | 0 | `a415a680` |
| 8845 | `a423e578` | 850000000 | `a423e5f4` | 4 | 2 | 0 | 0 | `a415a6b0` |
| 8845 | `a423e6ac` | 19200000 | `a423e598` | 0 | 2 | 0 | 0 | `00000000` |
| 8845 | `a423e6cc` | 19200000 | `a423e598` | 0 | 2 | 0 | 0 | `00000000` |
| 8845 | `a423e6ec` | 100000000 | `a423e5f4` | 4 | 6 | 0 | 0 | `a415a590` |
| 8845 | `a423e70c` | 120000000 | `a423e5f4` | 4 | 6 | 0 | 0 | `a415a5c0` |
| 8845 | `a423e72c` | 165000000 | `a423e5f4` | 4 | 6 | 0 | 0 | `a415a5f0` |
| 8845 | `a423e74c` | 200000000 | `a423e5f4` | 4 | 6 | 0 | 0 | `a415a620` |
| 8845 | `a423e76c` | 233333333 | `a423e5f4` | 4 | 6 | 0 | 0 | `a415a650` |
| 8845 | `a423e78c` | 261666667 | `a423e5f4` | 4 | 6 | 0 | 0 | `a415a680` |
| 8845 | `a423e7ac` | 283333333 | `a423e5f4` | 4 | 6 | 0 | 0 | `a415a6b0` |
| 8845 | `a423e7cc` | 19200000 | `a423e598` | 0 | 2 | 0 | 0 | `00000000` |
| 8845 | `a423e7ec` | 19200000 | `a423e598` | 0 | 2 | 0 | 0 | `00000000` |
| 8845 | `a423e80c` | 100000000 | `a423e5f4` | 6 | 6 | 0 | 0 | `a415a590` |
| 8845 | `a423e82c` | 120000000 | `a423e5f4` | 6 | 6 | 0 | 0 | `a415a5c0` |
| 8845 | `a423e84c` | 165000000 | `a423e5f4` | 6 | 6 | 0 | 0 | `a415a5f0` |
| 8845 | `a423e86c` | 200000000 | `a423e5f4` | 6 | 6 | 0 | 0 | `a415a620` |
| 8845 | `a423e88c` | 233333333 | `a423e5f4` | 6 | 6 | 0 | 0 | `a415a650` |
| 8845 | `a423e8ac` | 261666667 | `a423e5f4` | 6 | 6 | 0 | 0 | `a415a680` |
| 8845 | `a423e8cc` | 283333333 | `a423e5f4` | 6 | 6 | 0 | 0 | `a415a6b0` |
| 8845 | `a423e8ec` | 85714286 | `a423ea0c` | 1 | 6 | 0 | 0 | `a415a9e0` |
| 8845 | `a423e90c` | 100000000 | `a423ea0c` | 1 | 6 | 0 | 0 | `a415aa10` |
| 8845 | `a423e92c` | 156000000 | `a423ea0c` | 1 | 6 | 0 | 0 | `a415aa40` |
| 8845 | `a423e94c` | 207000000 | `a423ea0c` | 1 | 6 | 0 | 0 | `a415aa70` |
| 8845 | `a423e96c` | 337000000 | `a423ea0c` | 1 | 6 | 0 | 0 | `a415aaa0` |
| 8845 | `a423e98c` | 417000000 | `a423ea0c` | 1 | 6 | 0 | 0 | `a415aad0` |
| 8845 | `a423e9ac` | 532000000 | `a423ea0c` | 1 | 6 | 0 | 0 | `a415ab00` |
| 8845 | `a423e9cc` | 600000000 | `a423ea0c` | 1 | 6 | 0 | 0 | `a415ab30` |
| 8845 | `a423e9ec` | 650000000 | `a423ea0c` | 1 | 6 | 0 | 0 | `a415ab60` |
| 8845 | `a423ea68` | 38400000 | `a423ea88` | 2 | 2 | 0 | 0 | `00000000` |
| 8845 | `a424002c` | 7372800 | `a423e650` | 6 | 2 | 384 | 15625 | `00000000` |
| 8845 | `a424004c` | 14745600 | `a423e650` | 6 | 2 | 768 | 15625 | `00000000` |
| 8845 | `a424006c` | 19200000 | `a423e598` | 0 | 2 | 0 | 0 | `00000000` |
| 8845 | `a424008c` | 29491200 | `a423e650` | 6 | 2 | 1536 | 15625 | `00000000` |
| 8845 | `a42400ac` | 32000000 | `a423e650` | 6 | 2 | 8 | 75 | `00000000` |
| 8845 | `a42400cc` | 48000000 | `a423e650` | 6 | 2 | 4 | 25 | `00000000` |
| 8845 | `a42400ec` | 51200000 | `a423e650` | 6 | 2 | 64 | 375 | `00000000` |
| 8845 | `a424010c` | 64000000 | `a423e650` | 6 | 2 | 16 | 75 | `00000000` |
| 8845 | `a424012c` | 66666667 | `a423e650` | 1 | 18 | 0 | 0 | `00000000` |
| 8845 | `a424014c` | 75000000 | `a423e650` | 6 | 8 | 0 | 0 | `00000000` |
| 8845 | `a424016c` | 80000000 | `a423e650` | 6 | 2 | 4 | 15 | `00000000` |
| 8845 | `a424018c` | 96000000 | `a423e650` | 6 | 2 | 8 | 25 | `00000000` |
| 8845 | `a42401ac` | 100000000 | `a423e650` | 1 | 12 | 0 | 0 | `00000000` |
| 8845 | `a42401cc` | 102400000 | `a423e650` | 6 | 2 | 128 | 375 | `00000000` |
| 8845 | `a42401ec` | 112000000 | `a423e650` | 6 | 2 | 28 | 75 | `00000000` |
| 8845 | `a424020c` | 117964800 | `a423e650` | 6 | 2 | 6144 | 15625 | `00000000` |
| 8845 | `a424022c` | 120000000 | `a423e650` | 1 | 10 | 0 | 0 | `00000000` |
| 8845 | `a424024c` | 150000000 | `a423e650` | 6 | 4 | 0 | 0 | `00000000` |
| 8845 | `a424026c` | 250000000 | `a424028c` | 2 | 4 | 0 | 0 | `00000000` |
| 8845 | `a42402e8` | 7372800 | `a423e650` | 6 | 2 | 384 | 15625 | `00000000` |
| 8845 | `a4240308` | 14745600 | `a423e650` | 6 | 2 | 768 | 15625 | `00000000` |
| 8845 | `a4240328` | 19200000 | `a423e598` | 0 | 2 | 0 | 0 | `00000000` |
| 8845 | `a4240348` | 29491200 | `a423e650` | 6 | 2 | 1536 | 15625 | `00000000` |
| 8845 | `a4240368` | 32000000 | `a423e650` | 6 | 2 | 8 | 75 | `00000000` |
| 8845 | `a4240388` | 48000000 | `a423e650` | 6 | 2 | 4 | 25 | `00000000` |
| 8845 | `a42403a8` | 51200000 | `a423e650` | 6 | 2 | 64 | 375 | `00000000` |
| 8845 | `a42403c8` | 60000000 | `a423e650` | 6 | 10 | 0 | 0 | `00000000` |
| 8845 | `a42403e8` | 64000000 | `a423e650` | 6 | 2 | 16 | 75 | `00000000` |
| 8845 | `a4240408` | 66666667 | `a423e650` | 1 | 18 | 0 | 0 | `00000000` |
| 8845 | `a4240428` | 75000000 | `a423e650` | 6 | 8 | 0 | 0 | `00000000` |
| 8845 | `a4240448` | 80000000 | `a423e650` | 6 | 2 | 4 | 15 | `00000000` |
| 8845 | `a4240468` | 96000000 | `a423e650` | 6 | 2 | 8 | 25 | `00000000` |
| 8845 | `a4240488` | 100000000 | `a423e650` | 1 | 12 | 0 | 0 | `00000000` |
| 8845 | `a42404a8` | 102400000 | `a423e650` | 6 | 2 | 128 | 375 | `00000000` |
| 8845 | `a42404c8` | 112000000 | `a423e650` | 6 | 2 | 28 | 75 | `00000000` |
| 8845 | `a42404e8` | 117964800 | `a423e650` | 6 | 2 | 6144 | 15625 | `00000000` |
| 8845 | `a4240508` | 120000000 | `a423e650` | 1 | 10 | 0 | 0 | `00000000` |
| 8845 | `a4240528` | 7372800 | `a423e650` | 6 | 2 | 384 | 15625 | `00000000` |
| 8845 | `a4240548` | 14745600 | `a423e650` | 6 | 2 | 768 | 15625 | `00000000` |
| 8845 | `a4240568` | 19200000 | `a423e598` | 0 | 2 | 0 | 0 | `00000000` |
| 8845 | `a4240588` | 29491200 | `a423e650` | 6 | 2 | 1536 | 15625 | `00000000` |
| 8845 | `a42405a8` | 32000000 | `a423e650` | 6 | 2 | 8 | 75 | `00000000` |
| 8845 | `a42405c8` | 48000000 | `a423e650` | 6 | 2 | 4 | 25 | `00000000` |
| 8845 | `a42405e8` | 51200000 | `a423e650` | 6 | 2 | 64 | 375 | `00000000` |
| 8845 | `a4240608` | 60000000 | `a423e650` | 6 | 10 | 0 | 0 | `00000000` |
| 8845 | `a4240628` | 64000000 | `a423e650` | 6 | 2 | 16 | 75 | `00000000` |
| 8845 | `a4240648` | 66666667 | `a423e650` | 1 | 18 | 0 | 0 | `00000000` |
| 8845 | `a4240668` | 75000000 | `a423e650` | 6 | 8 | 0 | 0 | `00000000` |
| 8845 | `a4240688` | 80000000 | `a423e650` | 6 | 2 | 4 | 15 | `00000000` |
| 8845 | `a42406a8` | 96000000 | `a423e650` | 6 | 2 | 8 | 25 | `00000000` |
| 8845 | `a42406c8` | 100000000 | `a423e650` | 1 | 12 | 0 | 0 | `00000000` |
| 8845 | `a42406e8` | 7372800 | `a423e650` | 6 | 2 | 384 | 15625 | `00000000` |
| 8845 | `a4240708` | 14745600 | `a423e650` | 6 | 2 | 768 | 15625 | `00000000` |
| 8845 | `a4240728` | 19200000 | `a423e598` | 0 | 2 | 0 | 0 | `00000000` |
| 8845 | `a4240748` | 29491200 | `a423e650` | 6 | 2 | 1536 | 15625 | `00000000` |
| 8845 | `a4240768` | 32000000 | `a423e650` | 6 | 2 | 8 | 75 | `00000000` |
| 8845 | `a4240788` | 48000000 | `a423e650` | 6 | 2 | 4 | 25 | `00000000` |
| 8845 | `a42407a8` | 51200000 | `a423e650` | 6 | 2 | 64 | 375 | `00000000` |
| 8845 | `a42407c8` | 60000000 | `a423e650` | 6 | 10 | 0 | 0 | `00000000` |
| 8845 | `a42407e8` | 64000000 | `a423e650` | 6 | 2 | 16 | 75 | `00000000` |
| 8845 | `a4240808` | 66666667 | `a423e650` | 1 | 18 | 0 | 0 | `00000000` |
| 8845 | `a4240828` | 75000000 | `a423e650` | 6 | 8 | 0 | 0 | `00000000` |
| 8845 | `a4240848` | 80000000 | `a423e650` | 6 | 2 | 4 | 15 | `00000000` |
| 8845 | `a4240868` | 96000000 | `a423e650` | 6 | 2 | 8 | 25 | `00000000` |
| 8845 | `a4240888` | 100000000 | `a423e650` | 1 | 12 | 0 | 0 | `00000000` |
| 8845 | `a42408a8` | 102400000 | `a423e650` | 6 | 2 | 128 | 375 | `00000000` |
| 8845 | `a42408c8` | 112000000 | `a423e650` | 6 | 2 | 28 | 75 | `00000000` |
| 8845 | `a42408e8` | 117964800 | `a423e650` | 6 | 2 | 6144 | 15625 | `00000000` |
| 8845 | `a4240908` | 120000000 | `a423e650` | 1 | 10 | 0 | 0 | `00000000` |
| 8845 | `a4240928` | 7372800 | `a423e650` | 6 | 2 | 384 | 15625 | `00000000` |
| 8845 | `a4240948` | 14745600 | `a423e650` | 6 | 2 | 768 | 15625 | `00000000` |
| 8845 | `a4240968` | 19200000 | `a423e598` | 0 | 2 | 0 | 0 | `00000000` |
| 8845 | `a4240988` | 29491200 | `a423e650` | 6 | 2 | 1536 | 15625 | `00000000` |
| 8845 | `a42409a8` | 32000000 | `a423e650` | 6 | 2 | 8 | 75 | `00000000` |
| 8845 | `a42409c8` | 48000000 | `a423e650` | 6 | 2 | 4 | 25 | `00000000` |
| 8845 | `a42409e8` | 51200000 | `a423e650` | 6 | 2 | 64 | 375 | `00000000` |
| 8845 | `a4240a08` | 60000000 | `a423e650` | 6 | 10 | 0 | 0 | `00000000` |
| 8845 | `a4240a28` | 64000000 | `a423e650` | 6 | 2 | 16 | 75 | `00000000` |
| 8845 | `a4240a48` | 66666667 | `a423e650` | 1 | 18 | 0 | 0 | `00000000` |
| 8845 | `a4240a68` | 75000000 | `a423e650` | 6 | 8 | 0 | 0 | `00000000` |
| 8845 | `a4240a88` | 80000000 | `a423e650` | 6 | 2 | 4 | 15 | `00000000` |
| 8845 | `a4240aa8` | 96000000 | `a423e650` | 6 | 2 | 8 | 25 | `00000000` |
| 8845 | `a4240ac8` | 100000000 | `a423e650` | 1 | 12 | 0 | 0 | `00000000` |
| 8845 | `a4240ae8` | 102400000 | `a423e650` | 6 | 2 | 128 | 375 | `00000000` |
| 8845 | `a4240b08` | 112000000 | `a423e650` | 6 | 2 | 28 | 75 | `00000000` |
| 8845 | `a4240b28` | 117964800 | `a423e650` | 6 | 2 | 6144 | 15625 | `00000000` |
| 8845 | `a4240b48` | 120000000 | `a423e650` | 1 | 10 | 0 | 0 | `00000000` |
| 8845 | `a4240b68` | 128000000 | `a423e650` | 1 | 2 | 16 | 75 | `00000000` |
| 8845 | `a4240b88` | 37500000 | `a423e650` | 6 | 16 | 0 | 0 | `00000000` |

## PLL and GDSC descriptors

| Name | SM8850 descriptor | SM8845 descriptor | Type |
| --- | --- | --- | --- |
| disp_cc_pll0 | `a0213798` | `a423ea0c` | Taycan |
| disp_cc_pll1 | `a0213278` | `a423e5f4` | Taycan |
| disp_cc_pll2 | `a0213834` | `a423ea88` | Pongo |
| gcc_gpll0 | `a02132ec` | `a423e650` | Taycan, existing owner |
| gcc_gpll7 | `a0215574` | `a424028c` | Taycan, existing owner |
| disp_cc_mdss_core_gdsc | `a0212578` | `a423db18` | GDSC, DISP+1000 |
| disp_cc_mdss_core_int2_gdsc | `a02125bc` | `a423db50` | GDSC, DISP+3000 |
| disp_cc_mdss_byte0_div_clk_src | `a0212540` | `a423dae0` | Divider, DISP+1ac |
| disp_cc_mdss_byte1_div_clk_src | `a021255c` | `a423dafc` | Divider, DISP+1c8 |

Taycan uses MODE+0, OPMODE+4, L+10, ALPHA+14, and USER_CTL+18.
Its source requires a real start request, a nonzero multiplier, and acquisition time.
Pongo uses L+10 and USER_CTL+14 instead.
The existing Taycan and Pongo models implement these layouts.
Both images select the same PLL operation tables for these controllers.

The display Pongo descriptor selects a 32768 Hz reference.
The composition supplies that reference instead of the AOSS model's 19.2 MHz default.
The [Linux Kaanapali display driver](https://raw.githubusercontent.com/torvalds/linux/master/drivers/clk/qcom/dispcc-kaanapali.c) defines both PLL layouts and the sleep reference.
Its parent map 7 assigns selector 4 to PLL1 main and selector 6 to PLL1 even.
The AHB root therefore uses the even output for selector 6.

The PLL test source uses a nine-bit divider at DISP+`0x9004`, bits 24:16.
SM8850 `a00d7c60` writes that field. `a00d7c86` reads it.
SM8845 uses `a4104d28` and `a4104d4e`.
Its descriptor selects the crystal reference as its default source.
The integer-divider model now supplies `ClockSource` to its downstream branch.
It propagates source loss and divider changes without a constant running bit.
Existing four-bit divider callers retain their register layout.

### PLL rate parameters

These PLL records supply the root rate tables above.
The driver passes each record's configuration payload to the PLL operation table.
The Taycan programming PCs are SM8850 `a00d83a4` through `a00d841e`.
The corresponding SM8845 range is `a4105480` through `a41054fa`.
All listed records select predivider 1 and alpha format 1.

| SoC | Record | Hz | Predivider | L | Alpha | Format |
| --- | --- | --- | --- | --- | --- | --- |
| 8850 | `a012a598` | 300000000 | 1 | 15 | `a000` | 1 |
| 8850 | `a012a5cc` | 360000000 | 1 | 18 | `c000` | 1 |
| 8850 | `a012a600` | 495000000 | 1 | 25 | `c800` | 1 |
| 8850 | `a012a634` | 600000000 | 1 | 31 | `4000` | 1 |
| 8850 | `a012a668` | 700000000 | 1 | 36 | `7555` | 1 |
| 8850 | `a012a69c` | 785000000 | 1 | 40 | `e2aa` | 1 |
| 8850 | `a012a6d0` | 850000000 | 1 | 44 | `4555` | 1 |
| 8850 | `a012aab0` | 257142858 | 1 | 13 | `6492` | 1 |
| 8850 | `a012aae4` | 300000000 | 1 | 15 | `a000` | 1 |
| 8850 | `a012ab18` | 468000000 | 1 | 24 | `6000` | 1 |
| 8850 | `a012ab4c` | 621000000 | 1 | 32 | `5800` | 1 |
| 8850 | `a012ab80` | 1011000000 | 1 | 52 | `a800` | 1 |
| 8850 | `a012abb4` | 1251000000 | 1 | 65 | `2800` | 1 |
| 8850 | `a012abe8` | 1596000000 | 1 | 83 | `2000` | 1 |
| 8850 | `a012ac1c` | 1800000000 | 1 | 93 | `c000` | 1 |
| 8850 | `a012ac50` | 1950000000 | 1 | 101 | `9000` | 1 |
| 8845 | `a415a590` | 300000000 | 1 | 15 | `a000` | 1 |
| 8845 | `a415a5c0` | 360000000 | 1 | 18 | `c000` | 1 |
| 8845 | `a415a5f0` | 495000000 | 1 | 25 | `c800` | 1 |
| 8845 | `a415a620` | 600000000 | 1 | 31 | `4000` | 1 |
| 8845 | `a415a650` | 700000000 | 1 | 36 | `7555` | 1 |
| 8845 | `a415a680` | 785000000 | 1 | 40 | `e2aa` | 1 |
| 8845 | `a415a6b0` | 850000000 | 1 | 44 | `4555` | 1 |
| 8845 | `a415a9e0` | 257142858 | 1 | 13 | `6492` | 1 |
| 8845 | `a415aa10` | 300000000 | 1 | 15 | `a000` | 1 |
| 8845 | `a415aa40` | 468000000 | 1 | 24 | `6000` | 1 |
| 8845 | `a415aa70` | 621000000 | 1 | 32 | `5800` | 1 |
| 8845 | `a415aaa0` | 1011000000 | 1 | 52 | `a800` | 1 |
| 8845 | `a415aad0` | 1251000000 | 1 | 65 | `2800` | 1 |
| 8845 | `a415ab00` | 1596000000 | 1 | 83 | `2000` | 1 |
| 8845 | `a415ab30` | 1800000000 | 1 | 93 | `c000` | 1 |
| 8845 | `a415ab60` | 1950000000 | 1 | 101 | `9000` | 1 |

## Bootstrap-owned pages

The composition does not map these pages.
Their single owner remains in `models/qcom/sm88xx/dcp/bootstrap.h`.
The branch table above gives each descriptor and parent descriptor.
All listed DCP and RSCC branches are outside the MDSS core GDSC.
This domain classification follows their separate `a000` and `4000` reset groups.
It is not a physical power-topology measurement.

| System word | Type and name | Parent root | Domain |
| --- | --- | --- | --- |
| `09bab000` | mdss_core_gdsc | none | MDSS core supply |
| `09bae000` | BCR reset for non-GDSC group | none | outside MDSS core |
| `09bae004` | mdss_non_gdsc_ahb CBCR | DISP+378 | outside MDSS core |
| `09bae008` | mdss_rscc_vsync CBCR | DISP+17c | outside MDSS core |
| `09bae00c` | mdss_rscc_ahb CBCR | DISP+378 | outside MDSS core |
| `09bb4000` | DCP BCR reset | none | outside MDSS core |
| `09bb4004` | dcp_ahb CBCR | DISP+378 | outside MDSS core |
| `09bb4008` | dcp_tmr CBCR | DISP+6044 | outside MDSS core |
| `09bb400c` | dcp_sleep CBCR | reference record | outside MDSS core |
| `09bb4010`..`09bb4020` | dcp RCG CMD/CFG/M/N/D | XO=0, GPLL0=1, display PLL1=4 | outside MDSS core |
| `09bb4028` | dcp_dbg CBCR | DISP+a010 | outside MDSS core |
| `09bb402c` | dcp_f CBCR | DISP+a010 | outside MDSS core |
| `09bb4030` | dcp_proc_h CBCR | DISP+a010 | outside MDSS core |
| `09bb4040` | dcp_ss_h CBCR | DISP+a010 | outside MDSS core |
| `09bb4044`..`09bb4054` | dcp_dbg_apb RCG CMD/CFG/M/N/D | XO=0 | outside MDSS core |
| `09bb405c` | dcp_dbg_apb CBCR | DISP+a044 | outside MDSS core |
| `09bb4060`..`09bb4070` | dcp_dbg_qts RCG CMD/CFG/M/N/D | XO=0 | outside MDSS core |
| `09bb4078` | dcp_dbg_qts CBCR | DISP+a060 | outside MDSS core |
| `09bb407c`..`09bb408c` | dcp_dbg_atb RCG CMD/CFG/M/N/D | XO=0, display PLL1=4 | outside MDSS core |
| `09bb4094` | dcp_dbg_atb CBCR | DISP+a07c | outside MDSS core |

The DCP root selects CFG `0x1` for XO or `0x401` for display PLL1 without division.
Its GPLL0 rate uses CFG `0x101` without division.
The ATB root selects CFG `0x405` for display PLL1 divided by three.
Its XO rate uses CFG `0x1`. APB and QTS use CFG `0x1`.
These tables use M=N=0, so no fractional mode applies.
The tables above give all possible rates. They do not prove which row a runtime caller selects.

The deep-sleep code modifies these additional fields directly:

| System word | Request | SM8850 store PC | SM8845 store PC |
| --- | --- | --- | --- |
| `09bb4030` | Set bit 14, then bit 1 | `a00eaba6`, `a00eabae` | `a4116d50`, `a4116d58` |
| `09bb4040` | Set bit 1 | `a00eabb6` | `a4116d60` |
| `09bb4008` | Set bit 1 | `a00eabbe` | `a4116d68` |
| `09bb400c` | Set bit 0 | `a00eabc6` | `a4116d70` |
| `09bb4010` | Set CMD ROOT_EN bit 1 | `a00eabce` | `a4116d78` |

Bit 14 on the processor branch is a retained control, not a readiness request.
Branch bit 1 selects hardware control. Missing consumers need an explicit virtual-demand policy.
The clock source must still run before CLK_OFF clears.
The branch and root polling PCs appear in the access table.
GDSC enable waits for PWR_ON bit 31 after software clears collapse bit 0.
The driver can also check secondary status bit 9 when a status pointer exists.

## Composition and limits

Call `dofile(directory..'dcp-clocks.lua')(p, soc)` after the existing XBL clock composition.
`dcp-runtime.lua` adds this call.
The composition creates missing roots and branches on `apss_router` only.
It reuses concrete clock owners behind existing address translators.
It appends the DCP vote to the next free branch slot.
It does not replace existing XBL or QSEE votes.
The branch has four slots and rejects a fifth owner.
DCP branch votes are system `0x15a008`, `0x15a010`, and `0x15a018`.
DCP PLL votes use `0x15a020`.
The existing PLL owner combines three independent master requests.
The internal translation maps the third master to relative vote offset `0xa000`.

The table literals use the GPLL address origin for compact GCC records.
The composition adds `0x10000` to GCC root and branch addresses before mapping.
Vote masks use offsets relative to the DCP vote bank.

The display clock roots use their decoded sources, not constant ready bits.
A PHY-dependent rate remains unavailable until a real modeled source exists.
The current tables contain only XO fallback rates for those PHY roots.
Dynamic PHY rate requests are not evidence in this slice.
Reference-only records use the existing explicit virtual reference.
They do not create register-backed readiness.
The composition does not connect MDSS pixel branches to a physical display pipeline.
It also does not claim measured PLL latency or analog behavior.

The PLL fixture checks that the third master keeps both PLL consumers active.
It also checks that removal of the final master request stops them.
