#pragma once

#include "ota_types.h"
#include "prefabrication_manager.h"
#include "flash_update_manager.h"
#include "activation_manager.h"

class OtaService {
public:
    // 获取单例实例
    static OtaService& GetInstance();

    // 禁止拷贝与赋值
    OtaService(const OtaService&) = delete;
    OtaService& operator=(const OtaService&) = delete;

    // 外部接口
    OtaStatus_e SetRobotInfo(const RobotInfo_s* info);
    OtaStatus_e SetOtaMode(otaMode_e mode);
    OtaStatus_e StartUpdate(const char* path);
    OtaStatus_e GetUpdateStatus(UpdateSta_s* status);
    OtaStatus_e SetActive();
    OtaStatus_e GetActive(ActiveSta_s* status);

private:
    // 构造函数私有化
    OtaService();
    std::shared_ptr<PrefabricationManager> prefabManager_; // 预制化管理器
    std::shared_ptr<FlashUpdateManager> flashManager_; // 刷写管理器
    std::shared_ptr<ActivationManager> activeManager_; // 激活管理器

};
