-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- QSEE secure-camera boot protection words.
-- SM8850 0x15779c18 and SM8845 0x1576d008 write each word and then require the
-- readback to equal the written value. A mismatch returns 0x20 and reaches fatal
-- reason 0x30 in the caller. See docs/qcom/sm88xx/boot-dependencies.md.
-- The base is 0x0900000c. The producer offsets are 0x58,0x5c,0x60,0x00,0x18,
-- 0x04,0x4c,0x50,0x10. The producer writes 0x8fb8027f to 0x64,0x68,0x6c,
-- 0x8000001f to 0x0c,0x24, 0x8007ffff to 0x10, 0x80000001 to 0x58,
-- 0x803fffff to 0x5c, and zero to 0x1c.
-- The SM8850 OEM devcfg disables the physical-lane protection branch.
-- The SM8845 optional-property value is not established. Its five optional words
-- at 0x09251010 and nearby addresses are not part of this base-bank list.
return {
    0x0900000c,0x09000010,0x0900001c,0x09000024,0x09000058,
    0x0900005c,0x09000064,0x09000068,0x0900006c,
}
