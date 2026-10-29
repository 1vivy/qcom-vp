// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <ports/target-signal-socket.h>
#include <iostream>

namespace qcom_vp {

// The virtual board starts powered. PMIC reset and shutdown remain unsupported.
class VirtualPower : public sc_core::sc_module {
public:
    TargetSignalSocket<bool> ps_hold{"ps_hold"};

    explicit VirtualPower(sc_core::sc_module_name name) : sc_module(name) {
        ps_hold.register_value_changed_cb([this](const bool& level) {
            if (asserted_ && !level) {
                std::cerr << "UNMODELED_POWER_TRANSITION source=" << this->name() << '\n';
                sc_core::sc_stop();
            }
            asserted_ = asserted_ || level;
        });
    }

private:
    bool asserted_ = false;
};

}
