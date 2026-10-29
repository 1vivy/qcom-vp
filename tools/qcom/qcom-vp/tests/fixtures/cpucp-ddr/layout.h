// SPDX-License-Identifier: BSD-3-Clause-Clear
#if SOC == 8850
#define CODE 0x812d0000
#define ELF_ZERO0 0x81280000
#define ELF_ZERO0_SIZE 0x7ce8
#define ELF_ZERO1 0x81240000
#define SNAP_IMAGE 0x81240000
#define SNAP_RAM 0x81280000
#elif SOC == 8845
#define CODE 0x81240000
#define ELF_ZERO0 0x812d0000
#define ELF_ZERO0_SIZE 0x8010
#define ELF_ZERO1 0x812da000
#define SNAP_IMAGE 0x822ffe00
#define SNAP_RAM 0x8233fe00
#else
#error Select SOC=8850 or SOC=8845
#endif
#define ELF_ZERO1_SIZE 0xb610
#define LOCAL_RAM 0x18b30000
#define IMAGE 0x1d800000
#define SPAN 0x40000
#define STACK_TOP (LOCAL_RAM + 0x8000)
#define FLAGS 0x80001000
#define LOCAL_PATTERN 0x3333333333333333
#define IMAGE_PATTERN 0x5555555555555555
#define POISON 0x5a5a5a5a5a5a5a5a
// Fixture-only APSS windows that drive the production DMA routers.
#define UFS_VIEW(address) ((address) + 0x80000000)
#define AUTH_VIEW(address) ((address) + 0x180000000)
