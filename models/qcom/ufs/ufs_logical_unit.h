// SPDX-License-Identifier: BSD-3-Clause-Clear
#ifndef QCOM_VP_UFS_LOGICAL_UNIT_H
#define QCOM_VP_UFS_LOGICAL_UNIT_H

#include <ufs_lu.h>
#include <stdexcept>

// The selected LU retains its original QBox block backend and SCSI device.
class UfsLogicalUnit : public ufs_lu {
public:
    cci::cci_param<uint32_t> boot_lun_id{"boot_lun_id", 255, "OEM provisioned boot LU ID"};

    UfsLogicalUnit(const sc_core::sc_module_name& name, sc_core::sc_object* instance,
                   sc_core::sc_object* controller)
        : ufs_lu(name, checked_instance(instance), checked_controller(controller)) {
        const auto configure = set_dev_props;
        set_dev_props = [this, configure]() {
            if (boot_lun_id.get_value() > 2) {
                throw std::invalid_argument("UfsLogicalUnit requires an OEM boot LU ID");
            }
            configure();
            m_dev.set_prop_uint("boot-lun-id", boot_lun_id.get_value());
        };
    }

private:
    static QemuInstance& checked_instance(sc_core::sc_object* object) {
        auto* instance = dynamic_cast<QemuInstance*>(object);
        if (!instance) throw std::invalid_argument("UfsLogicalUnit requires a QemuInstance");
        return *instance;
    }

    static ufs* checked_controller(sc_core::sc_object* object) {
        auto* controller = dynamic_cast<ufs*>(object);
        if (!controller) throw std::invalid_argument("UfsLogicalUnit requires a UFS controller");
        return controller;
    }
};
#endif
