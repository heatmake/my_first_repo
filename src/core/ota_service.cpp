#include "ota_service.h"
#include <iostream>
#include "otalog.h"


// 获取单例实例
OtaService& OtaService::GetInstance() {
    static OtaService instance;
    return instance;
}

OtaService::OtaService() {
    prefabManager_ = std::make_shared<PrefabricationManager>();
    flashManager_ = std::make_shared<FlashUpdateManager>();
    activeManager_ = std::make_shared<ActivationManager>();
}


OtaStatus_e OtaService::SetRobotInfo(const RobotInfo_s* info) {
    if (!info)
    {
        OTALOG(OlmInstall, OllError, "[OtaService]Invalid Robot Info\n");
        return OTA_STATUS_FAILED;
    } 
    return prefabManager_->SetRobotInfo(info);
}

OtaStatus_e OtaService::SetOtaMode(otaMode_e mode) {
    return prefabManager_->SetOtaMode(mode);
}

OtaStatus_e OtaService::StartUpdate(const char* path) {
    if (!path)
    {
        OTALOG(OlmInstall, OllError, "[OtaService]Invalid Update Path\n");
        return OTA_STATUS_FAILED;;
    } 
    return flashManager_->StartUpdate(path);
}

OtaStatus_e OtaService::GetUpdateStatus(UpdateSta_s* status) {
    if (!status)
    {
        OTALOG(OlmInstall, OllError, "[OtaService]Invalid Update OtaStatus_e\n");
        return OTA_STATUS_FAILED;;
    } 
    return flashManager_->GetUpdateStatus(status);
}

OtaStatus_e OtaService::SetActive() {
    return activeManager_->SetActive();
}

OtaStatus_e OtaService::GetActive(ActiveSta_s* status) {
    if (!status)
    {
        OTALOG(OlmInstall, OllError, "[OtaService]Invalid Active OtaStatus_e");
        return OTA_STATUS_FAILED;;
    } 
    return activeManager_->GetActive(status);
}
