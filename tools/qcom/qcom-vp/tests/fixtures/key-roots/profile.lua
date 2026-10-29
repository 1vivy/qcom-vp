-- SPDX-License-Identifier: BSD-3-Clause-Clear
-- Public test vectors only. These values are not device secrets.
return {
    oem_hw_key_hex = string.rep('12', 32),
    virtual_root_hex = string.rep('12', 48),
}
