#include "ota_service.h"
#include <iostream>

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
        std::cout << "Invalid Robot Info" << std::endl;
        std::cout << "Failed to set robot info" << std::endl;
        return OtaStatus_e::Failed;
    } 
    return prefabManager_->SetRobotInfo(info);
}

OtaStatus_e OtaService::SetOtaMode(otaMode_e mode) {
    return prefabManager_->SetOtaMode(mode);
}

OtaStatus_e OtaService::StartUpdate(const char* path) {
    if (!path)
    {
        std::cout << "Invalid Update Path" << std::endl;
        return OtaStatus_e::Failed;
    } 
    return flashManager_->StartUpdate(path);
}

OtaStatus_e OtaService::GetUpdateStatus(UpdateSta_s* status) {
    if (!status)
    {
        std::cout << "Invalid Update Status" << std::endl;
        return OtaStatus_e::Failed;
    } 
    return flashManager_->GetUpdateStatus(status);
}

OtaStatus_e OtaService::SetActive() {
    return activeManager_->SetActive();
}

OtaStatus_e OtaService::GetActive(ActiveSta_s* status) {
    if (!status)
    {
        std::cout << "Invalid Active Status" << std::endl;
        return OtaStatus_e::Failed;
    } 
    return activeManager_->GetActive(status);
}
