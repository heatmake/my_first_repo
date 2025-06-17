#include "activation_manager.h"
#include <iostream>
#include <regex>
#include "otalog.h"

ActivationManager::ActivationManager()
{
    OTALOG(OlmInstall, OllInfo, "[ActivationManager]ActivationManager created.\n");
}

std::string ActivationManager::ExtractVersionFromFilename(const std::string& filename)
{
    // 匹配 -V<主>.<子>.<修正> 形式的版本号
    std::regex versionRegex(R"(-V(\d+)\.(\d+)\.(\d+))");
    std::smatch match;

    if (std::regex_search(filename, match, versionRegex) && match.size() == 4) {
        return match[1].str() + "." + match[2].str() + "." + match[3].str();
    }

    return ""; // 匹配失败
}

// 第一步：设置激活标志并通知 MCU 重启
OtaStatus_e ActivationManager::SetActive()
{
    OtaStatus_e result = OTA_STATUS_SUCCESS;
    OTALOG(OlmInstall, OllInfo, "[ActivationManager]SetActive begin\n");
    int value = 1;
    bool writeSuccess = JsonHelper::GetInstance().WriteInt(K_OTA_INFO_JSON_PATH, "active_flag", value);
    if (!writeSuccess) {
        OTALOG(OlmInstall, OllError, "[ActivationManager]Failed to write active status\n");
        result = OTA_STATUS_FAILED;;
    }
    return result;
}

// 第二步：系统重启后 OTA 周期调用查询激活状态
OtaStatus_e ActivationManager::GetActive(ActiveSta_s* status)
{
    OtaStatus_e result = OTA_STATUS_FAILED;

    if (!status) {
        OTALOG(OlmInstall, OllError, "[ActivationManager]Invalid status pointer.\n");
        return result;
    }

    JsonHelper& json = JsonHelper::GetInstance();
    const std::string otaPath = K_OTA_INFO_JSON_PATH;

    bool resetFlag = false;
    bool socFlag = false;
    bool resetFlagOk = json.ReadBool(otaPath, "reset_flag", &resetFlag);

    if(!json.ReadBool(otaPath, "soc_flag", &socFlag))
    {
        OTALOG(OlmInstall, OllError, "[ActivationManager]soc_flag read false. Skipping activation.\n");
        status->result = 2;
    }
    
    if (!socFlag) {
        if (!resetFlagOk || !resetFlag) {
            OTALOG(OlmInstall, OllInfo, "[ActivationManager]Reset flag is not set or false. Skipping activation.\n");
            json.WriteInt(otaPath, "active_flag", 2);
            status->result = 2;
        } else {
            std::string expectedVersion;
            bool hasExpected = json.ReadString(otaPath, "mcu_version", &expectedVersion);

            if (!hasExpected) {
                OTALOG(OlmInstall, OllError, "[ActivationManager]Missing expected MCU version.\n");
                json.WriteInt(otaPath, "active_flag", 2);
                status->result = 2;
            } else {
                std::string responseVersion;
                bool flashSuccess = DataCtrl::GetInstance().RetryOperation("FlashCompleteRequest", [&]() {
                    return DataCtrl::GetInstance().FlashCompleteRequest(responseVersion);
                });
                responseVersion = ExtractVersionFromFilename(responseVersion);
                if (!flashSuccess) {
                    OTALOG(OlmInstall, OllError, "[ActivationManager]FlashCompleteRequest failed.\n");
                    status->result = 2;
                } else if (responseVersion == expectedVersion) {
                    OTALOG(OlmInstall,
                           OllInfo,
                           "version match: Expected=%s, Got=%s\n",
                           expectedVersion.c_str(),
                           responseVersion.c_str());
                    json.WriteInt(otaPath, "active_flag", 0);
                    status->result = 0;
                    resetFlag = false;
                    bool resetFlagOk = json.WriteBool(otaPath, "reset_flag", resetFlag);
                    result = OTA_STATUS_SUCCESS;
                } else {
                    OTALOG(OlmInstall,
                           OllError,
                           "version mismatch: Expected=%s, Got=%s\n",
                           expectedVersion.c_str(),
                           responseVersion.c_str());
                    json.WriteInt(otaPath, "active_flag", 2);
                    status->result = 2;
                }
            }
        }
    } else {
        OTALOG(OlmInstall, OllInfo, "[ActivationManager]no MCU upgrade package, return activation success\n");
        status->result = 0;
        result = OTA_STATUS_SUCCESS;
    }

    return result;
}
