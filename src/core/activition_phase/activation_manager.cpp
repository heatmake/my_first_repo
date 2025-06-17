#include "activation_manager.h"
#include <iostream>

// 第一步：设置激活标志并通知 MCU 重启
bool ActivationManager::SetActive()
{
    std::cout << "[OTA] Activating update..." << std::endl;
    ActiveSta_s value;
    value.result = 1; // 激活中
    bool writeSuccess = JsonHelper::Instance().WriteBool("/data/config/ota/ota_info.json", "active_flag", &value);
    if (!writeSuccess)
    {
        std::cerr << "[OTA] status" << std::endl;
        return false;
    }
    std::cout << "[OTA] Active flag set to 1. Rebooting MCU..." << std::endl;
    return true;
}

bool RbtOtaServiceImpl::FlashCompleteRequest(std::string &outVersion)
{
    constexpr uint8_t FLASH_CMD = 0x01;
    dataCtrl_->PrivateProtocolEncap(DataPlaneType_e::DATA_TYPE_FLASH_FINISH, &FLASH_CMD, sizeof(FLASH_CMD));

    uint8_t recvBuf[COMM_DATA_LEN_MAX] = {0};

    if (dataCtrl_->SendData(recvBuf, sizeof(recvBuf)))
    {
        uint8_t status = recvBuf[24]; // 状态在第25字节

        std::cout << "[OTA] Flash complete ack received. Attempt: " << retry + 1 << ", Status: 0x"
                  << std::hex << static_cast<int>(status) << std::dec << std::endl;

        if (status == 0x00)
        {
            // 提取前24字节为版本号
            outVersion = std::string(reinterpret_cast<char *>(recvBuf), 24);
            std::cout << "[OTA] Flash successful. MCU Version: " << outVersion << std::endl;
            return true;
        }
        else if (status == 0x01)
        {
            std::cerr << "[OTA] MCU flash failed with NG status." << std::endl;
            break;
        }
        else
        {
            std::cerr << "[OTA] Unknown flash status: 0x"
                      << std::hex << static_cast<int>(status) << std::dec << std::endl;
            break;
        }
    }
    return false;
}

// 第二步：系统重启后 OTA 周期调用查询激活状态
uint8_t ActivationManager::GetActive(ActiveSta_s *status)
{
    if (!status)
    {
        std::cerr << "[OTA] Invalid status pointer" << std::endl;
        return 2;
    }

    bool resetFlag = false;
    if (!JsonHelper::Instance().ReadBool("/data/config/ota/ota_info.json", "reset_flag", &resetFlag) || !resetFlag)
    {
        std::cout << "[OTA] reset_flag not set. Skipping activation." << std::endl;
        JsonHelper::Instance().WriteBool("/data/config/ota/ota_info.json", "active_flag", 0);
        return 1; // 激活中
    }

    std::string expectedVersion;
    if (!JsonHelper::Instance().ReadString("/data/config/ota/ota_info.json", "mcu_version", &expectedVersion))
    {
        std::cerr << "[OTA] mcu_version missing." << std::endl;
        JsonHelper::Instance().WriteBool("/data/config/ota/ota_info.json", "active_flag", 0);
        return 1; // 激活中
    }

    const int kMaxRetry = 6;
    const int kRetryIntervalMs = 50;

    for (int i = 0; i < kMaxRetry; ++i)
    {
        std::string responseVersion;
        if (!FlashCompleteRequest(responseVersion))
        {
            std::cerr << "[OTA] FlashCompleteRequest failed. Retry " << (i + 1) << "/" << kMaxRetry << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(kRetryIntervalMs));
            continue;
        }

        if (responseVersion == expectedVersion)
        {
            std::cout << "[OTA] Version match: " << responseVersion << std::endl;
            return 0; // 激活成功
        }
        else
        {
            std::cerr << "[OTA] Version mismatch. Expected: " << expectedVersion << ", Got: " << responseVersion << std::endl;
            JsonHelper::Instance().WriteString("/data/config/ota/ota_info.json", "flash_status", "version_mismatch");
            return 2; // 激活失败
        }
    }

    std::cerr << "[OTA] Flash verification timed out." << std::endl;
    return 2; // 激活失败
}
