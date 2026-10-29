// SPDX-License-Identifier: BSD-3-Clause-Clear
#include <stdint.h>

#define CSR(offset) (*(volatile uint32_t *)(0x02230000U + (offset)))
// 15703a28 writes this control. 15703a60 polls the status word.
#define TPKEY_CONTROL (*(volatile uint32_t *)0x01df101cU)
#define TPKEY_STATUS (*(volatile uint32_t *)0x01df1020U)
#define TPKEY_READY 0x100U
// CRYPTO0_CRYPTO_KM_RG_SHARED and KM_CTL/KM_STATUS in RG_TZ.
#define KM_IPCAT_VERSION (*(volatile uint32_t *)0x01df0000U)
#define KM_KEY_POLICY_VERSION (*(volatile uint32_t *)0x01df0004U)
#define KM_SHARED_STATUS (*(volatile uint32_t *)0x01df0008U)
#define KM_KEYTABLE_SIZE (*(volatile uint32_t *)0x01df000cU)
#define KM_CTL (*(volatile uint32_t *)0x01df1000U)
#define KM_STATUS (*(volatile uint32_t *)0x01df1004U)

#ifndef CASE
#define CASE 0
#endif

static void require(int condition, unsigned step) {
    if (!condition) {
        *(volatile uint32_t *)(0xbad00000U + step * 4) = step;
        for (;;) {}
    }
}

static void acquire(void) {
    CSR(0x60) = 0x100;
    require((CSR(0x60) & 0x70) == 0x10, 1);
    CSR(0) = 0x1c;
    CSR(4) = 0;
    CSR(8) = 0;
    CSR(12) = 0;
    CSR(0x2c) = 2;
}

static void complete(void) {
    __asm__ volatile("dsb sy" ::: "memory");
    CSR(0x60) = 0x200;
    require((CSR(0x60) & 0x70) == 0x40, 2);
    require(CSR(0x144) == 0, 3);
    require(CSR(0x148) == 0, 4);
}

static void release(void) {
    CSR(0x130) = 1;
    require(CSR(0x60) == 0 && CSR(0x128) == 0, 5);
}

static void broadcast(void) {
    acquire();
    complete();
    release();
}

void test(void) {
#if CASE == 0
    // The cold GPCE reports no transport key before the command.
    require((TPKEY_STATUS & TPKEY_READY) == 0, 10);
    // The selected firmware opens the receive window before it waits.
    TPKEY_CONTROL = 0x13e;
    require((TPKEY_STATUS & TPKEY_READY) == 0, 11);
    broadcast();
    require((TPKEY_STATUS & TPKEY_READY) != 0, 12);
    // A repeated broadcast keeps the retained state.
    broadcast();
    require((TPKEY_STATUS & TPKEY_READY) != 0, 13);
#elif CASE == 1
    // A broadcast with the receive window closed stays pending.
    broadcast();
    require((TPKEY_STATUS & TPKEY_READY) == 0, 14);
    TPKEY_CONTROL = 0x13e;
    require((TPKEY_STATUS & TPKEY_READY) != 0, 15);
#elif CASE == 4
    // The shared group identifies the model and reports the virtual key table.
    require(KM_IPCAT_VERSION == 0x02020000U, 20);
    require(KM_KEY_POLICY_VERSION == 4U, 21);
    require(KM_KEYTABLE_SIZE == 16U, 22);
    // No command is in flight, so the shared status reports no busy bit.
    require((KM_SHARED_STATUS & 1U) == 0, 23);
    // The cold model reports no self-test until the firmware writes KM_CTL,
    // and the cold key table holds no valid slot.
    require(KM_STATUS == 0U, 24);
    // 157039e0 clears CRC_CHECK_EN after reset. That write completes the
    // only self-test step the model runs.
    KM_CTL = KM_CTL & ~1U;
    require((KM_STATUS & 0x280U) == 0x280U, 25);
    require((KM_STATUS & 0x140U) == 0, 26);
    require((KM_STATUS & 1U) != 0, 27);
    // A second write keeps the state and adds no error.
    KM_CTL = 0;
    require((KM_STATUS & 0x3ffU) == 0x281U, 28);
#elif CASE == 5
    // An offset outside the implemented groups must fail.
    require(*(volatile uint32_t *)0x01df0100U == 0, 30);
#else
    // Cases two and three must fail the execute write.
    broadcast();
    require(0, 16);
#endif
    *(volatile uint32_t *)0x600d0000U = 1;
    for (;;) {}
}
