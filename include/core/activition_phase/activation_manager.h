#pragma once
#include "ota_types.h"
#include <memory>
#include "mcu_updater.h"

class ActivationManager {
public:
    bool SetActive();
    bool GetActive(ActiveSta_s *status);

private:
    bool activated_ = false;
    bool failed_ = false;
};