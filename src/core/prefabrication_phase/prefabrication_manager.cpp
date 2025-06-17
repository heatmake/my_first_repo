#include "prefabrication_manager.h"
#include <iostream>
#include "otalog.h"

OtaStatus_e PrefabricationManager::SetRobotInfo(const RobotInfo_s *info) {
    OtaStatus_e result = OTA_STATUS_FAILED; 

    if (info) {
        bool success = JsonHelper::GetInstance().WriteString(K_ROBOT_INFO_JSON_PATH, "robot_sw_version", info->version);
        if (success) {
            result = OTA_STATUS_SUCCESS; 
            OTALOG(OlmInstall, OllInfo, "[PRE]Set Robot Info: version = %s\n", info->version);
        } else {
            OTALOG(OlmInstall, OllError, "[PRE]Failed to write robot_info.json.\n" );
        }
    }
    return result; 
}

OtaStatus_e PrefabricationManager::SetOtaMode(otaMode_e mode) {
    OtaStatus_e result = OTA_STATUS_FAILED;
    bool otaModeFlag;
    if (mode == OTA_MODE_CANCEL) {
        OTALOG(OlmInstall, OllInfo, "[PRE]OTA mode canceled, no update will be performed.\n");
        otaModeFlag = false;
        bool readResult = JsonHelper::GetInstance().WriteBool(K_OTA_INFO_JSON_PATH, "ota_mode_flag", otaModeFlag);
        if (!readResult) {
            OTALOG(OlmInstall, OllError,  "[PRE]Failed to write ota_mode_flag = false\n" );
        }
        result = OTA_STATUS_SUCCESS;  
    } else if (mode == OTA_MODE_SET) {
        OTALOG(OlmInstall, OllInfo, "[PRE]Set OTA mode.Starting update thread\n");
        otaModeFlag = true;
        bool readResult = JsonHelper::GetInstance().WriteBool(K_OTA_INFO_JSON_PATH, "ota_mode_flag", otaModeFlag);
        if (!readResult) {
            OTALOG(OlmInstall, OllError,  "[PRE]Failed to write ota_mode_flag = true\n" );
        }
        result = OTA_STATUS_SUCCESS;
        //后续通知MCU进入OTA模式（保留接口）
        // if (result) {
        //     result = NotifyMcuEnterOtaMode();
        //     if (!result) {
        //         std::cerr << "[PRE]Failed to notify MCU to enter OTA mode via SPI." << std::endl;
        //     }
        // } else {
        //     std::cerr << "[PRE]Failed to write ota_mode_flag = true" << std::endl;  
        // }
    } else {
        OTALOG(OlmInstall, OllError, "[PRE]Failed to write ota_mode_flag = true\n");
    }
    return result; 
}