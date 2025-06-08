#pragma once
#include <thread>
#include <atomic>
#include <string>
#include "ota_types.h"
#include "soc_updater.h"
#include "mcu_updater.h"

class FlashUpdateManager {
public:
    FlashUpdateManager();
    ~FlashUpdateManager();

    OtaStatus_e StartUpdate(const char* path);
    OtaStatus_e GetUpdateStatus(UpdateSta_s& status);

private:
    void UpdateThreadFunc();

private:
    UpdateSta_s updateStatus_;
    std::thread updateThread_;
    std::string packagePath_;
    std::shared_ptr<SocUpdater> socUpdater_;
    std::shared_ptr<McuUpdater> mcuUpdater_;
};
