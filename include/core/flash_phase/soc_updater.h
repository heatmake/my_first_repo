#ifndef SOC_UPDATER_H
#define SOC_UPDATER_H
#include <string>
#include "mcu_updater.h"
#include <memory>
#include "ota_types.h"

class SocUpdater {
public:
    bool Update(const std::vector<fs::path>& socPackages,UpdateSta_s &updateStatus,uint8_t totalFiles);
private:
    void RestoreConfigs();
    OtaStatus_e FlashDeb(const std::string &description, const std::string &filepath);
    OtaStatus_e FlashSocSystem(const std::string &filepath);

    const int kMaxRetries = 3;

    // uint32_t stage_;   // 0=成功，1=升级中，2=失败
    // uint32_t process_ ; // 0-100%

    // std::string mcu_flash_path;
    // std::string mcu_app_path;

    std::shared_ptr<McuUpdater> mcuUpdater_;

};
#endif // SOC_UPDATER_H
