#include "prefabrication_manager.h"
#include <iostream>

OtaStatus_e PrefabricationManager::SetRobotInfo(const RobotInfo_s *info) {
    OtaStatus_e result = OtaStatus_e::Failed; 

    if (info) {
        bool success = JsonHelper::GetInstance().WriteString("/data/config/ota/robot_info.json", "robot_sw_version", info->version);
        if (success) {
            result = OtaStatus_e::Success; 
            std::cout << "[OTA] Set Robot Info: version = " << info->version << std::endl;
        } else {
            std::cerr << "[OTA] Failed to write robot_info.json." << std::endl;
        }
    }
    return result; 
}

OtaStatus_e PrefabricationManager::SetOtaMode(otaMode_e mode) {
    OtaStatus_e result = OtaStatus_e::Failed;
    if (mode == OTA_MODE_CANCEL) {
        std::cout << "[OTA] OTA mode canceled, no update will be performed." << std::endl;
        result = JsonHelper::GetInstance().WriteBool("/data/config/ota/ota_info.json", "ota_mode_flag", false);
        if (!result) {
            std::cerr << "[OTA] Failed to write ota_mode_flag = false" << std::endl;
        }
        result = OtaStatus_e::Success;
    } else if (mode == OTA_MODE_SET) {
        std::cout << "[OTA] OTA mode set. Starting update thread..." << std::endl;
        result = JsonHelper::GetInstance().WriteOtaStatus_e("/data/config/ota/ota_info.json", "ota_mode_flag", true);
        if (!result) {
            std::cerr << "[OTA] Failed to write ota_mode_flag = true" << std::endl;
        }
        result = OtaStatus_e::Success;
        //后续通知MCU进入OTA模式（保留接口）
        // if (result) {
        //     result = NotifyMcuEnterOtaMode();
        //     if (!result) {
        //         std::cerr << "[OTA] Failed to notify MCU to enter OTA mode via SPI." << std::endl;
        //     }
        // } else {
        //     std::cerr << "[OTA] Failed to write ota_mode_flag = true" << std::endl;
        // }
    } else {
        std::cerr << "[OTA] Invalid OTA mode: " << mode << std::endl;
    }
    return result; 
}