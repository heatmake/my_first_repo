#include "mcu_updater.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <regex>
#include <tuple>

McuUpdater::McuUpdater()
{
    dataCtrl_ = std::make_shared<DataCtrl>();
}

bool McuUpdater::Update(const std::vector<fs::path> &mcuPackages, UpdateSta_s &updateStatus, uint8_t totalFiles)
{
    bool result = false;
    std::string mcuFlashPath;
    std::string mcuAppPath;

    for (const auto &path : mcuPackages)
    {
        const std::string filename = path.filename().string();

        if (std::regex_match(filename, std::regex(R"(MCU_FLASH_DRIVER.*\.bin)")))
        {
            std::cout << "[SOC] 检测到 MCU_FLASH_DRIVER 包: " << filename << "\n";
            mcuFlashPath = path.string();
        }
        else if (std::regex_match(filename, std::regex(R"(MCU_APP.*\.bin)")))
        {
            std::cout << "[SOC] 检测到 MCU_APP 包: " << filename << "\n";
            mcuAppPath = path.string();
        }
    }

    for (int attempt = 1; attempt <= kMaxRetries; ++attempt)
    {
        if (TryUpdateOnce(mcuFlashPath, mcuAppPath, updateStatus, totalFiles))
        {
            std::cout << "[MCU] Update successful.\n";
            result = true;
            break;
        }
        std::cout << "[MCU] Update failed on attempt " << attempt << ". Retrying...\n";
    }
    return result;
}

bool McuUpdater::TryUpdateOnce(std::string &flashDiverPath, std::string &appPath, UpdateSta_s &updateStatus, uint8_t totalFiles)
{
    return PreUpdate(appPath) && Flashing(flashDiverPath, appPath, updateStatus, totalFiles) && Reboot();
}

// 从文件名中提取版本号
std::string McuUpdater::ExtractVersionFromFilename(const std::string &filename)
{
    std::regex versionRegex(R"(MCU_APP-[^-]+-[^-]+-V(\d+)\.(\d+)\.(\d+)-\d+\.bin)");
    std::smatch match;
    if (std::regex_match(filename, match, versionRegex) && match.size() == 4)
    {
        return match[1].str() + "." + match[2].str() + "." + match[3].str(); // 主.子.修正
    }
    return ""; // 匹配失败
}

// 版本号比较函数：true 表示 newMcuVersion 更高
bool McuUpdater::IsVersionNewer(const std::string &newMcuVersion, const std::string &mcuVersion)
{
    auto splitVersion = [](const std::string &version) -> std::tuple<int, int, int>
    {
        int major = 0, minor = 0, patch = 0;
        sscanf(version.c_str(), "%d.%d.%d", &major, &minor, &patch);
        return {major, minor, patch};
    };

    auto [socMajor, socMinor, socPatch] = splitVersion(newMcuVersion);
    auto [mcuMajor, mcuMinor, mcuPatch] = splitVersion(mcuVersion);

    if (socMajor != mcuMajor)
        return socMajor > mcuMajor;
    if (socMinor != mcuMinor)
        return socMinor > mcuMinor;
    return socPatch > mcuPatch;
}

bool McuUpdater::PreUpdate(const std::string &appPath)
{
    std::cout << "[MCU] Pre-update checks...\n";

    bool success = false;
    std::string receivedVersion;

    // 1. 写入 flash_mode
    if (!JsonHelper::GetInstance().WriteString("/data/config/ota/ota_info.json", "flash_mode", "normal"))
    {
        std::cerr << "[MCU] Failed to write flash_mode to JSON.\n";
        return false;
    }

    // 2. 提取目标版本
    std::string newMcuVersion = ExtractVersionFromFilename(appPath);
    if (newMcuVersion.empty())
    {
        std::cerr << "[MCU] 无法从文件名中提取SOC版本，终止刷写流程！\n";
        return false;
    }

    // 3. 发送预检查请求
    const uint8_t checkValue = 0x01;
    const uint8_t *payload = nullptr;
    uint16_t payloadLen = 0;

    if (!dataCtrl_->SendAndRecv(DataPlaneType_e::DATA_TYPE_PRE_PROGRAM_CHECK,
                                &checkValue, sizeof(checkValue),
                                payload, payloadLen))
    {
        std::cerr << "[MCU] SendAndRecv failed.\n";
        return false;
    }

    if (payload == nullptr || payloadLen < 1)
    {
        std::cerr << "[MCU] Payload invalid or too short.\n";
        return false;
    }

    uint8_t status = payload[payloadLen - 1];
    if (status != 0x00)
    {
        std::cerr << "[MCU] MCU响应状态码无效，状态=0x"
                  << std::hex << static_cast<int>(status) << "\n";
        return false;
    }

    // 4. 提取版本号并判断
    receivedVersion.assign(reinterpret_cast<const char *>(payload), payloadLen - 1);
    std::cout << "[MCU] MCU ready. Version: " << receivedVersion << "\n";

    if (IsVersionNewer(newMcuVersion, receivedVersion))
    {
        std::cout << "[MCU] SOC版本较新，进入刷写流程，记录当前MCU版本...\n";
        if (!JsonHelper::GetInstance().WriteString("/data/config/ota/ota_info.json", "mcu_version", receivedVersion))
        {
            std::cerr << "[MCU] Failed to write mcu_version to JSON.\n";
            return false;
        }
        success = true;
    }
    else if (newMcuVersion == receivedVersion)
    {
        std::cout << "[MCU] SOC与MCU版本一致，跳过刷写。\n";
        success = true;
    }
    else
    {
        std::cout << "[MCU] SOC版本低于MCU，禁止刷写。\n";
    }

    return success;
}

bool McuUpdater::SendDownloadRequest(DownloadRequestType requestType, uint16_t totalPackages, uint32_t totalLength)
{
    std::cout << "[MCU] Sending Flash-driver download request to MCU...\n";

    bool result = false;

    uint8_t payload[7] = {
        static_cast<uint8_t>(requestType),
        static_cast<uint8_t>((totalPackages >> 8) & 0xFF),
        static_cast<uint8_t>(totalPackages & 0xFF),
        static_cast<uint8_t>((totalLength >> 24) & 0xFF),
        static_cast<uint8_t>((totalLength >> 16) & 0xFF),
        static_cast<uint8_t>((totalLength >> 8) & 0xFF),
        static_cast<uint8_t>(totalLength & 0xFF)};

    const uint8_t *responsePayload = nullptr;
    uint16_t responseLen = 0;

    if (!dataCtrl_->SendAndRecv(DATA_TYPE_DOWNLOAD_REQUEST, payload, sizeof(payload), responsePayload, responseLen))
    {
        std::cerr << "[MCU] Failed to send download request or receive response.\n";
    }
    else if (responsePayload == nullptr || responseLen < 1)
    {
        std::cerr << "[MCU] Invalid response payload.\n";
    }
    else if (responsePayload[0] != 0x00)
    {
        std::cerr << "[MCU] MCU responded with unexpected code: 0x"
                  << std::hex << static_cast<int>(responsePayload[0]) << "\n";
    }
    else
    {
        std::cout << "[MCU] MCU confirmed download request.\n";
        result = true;
    }

    return result;
}

// 获取文件总长度
uint32_t RbtOtaServiceImpl::CalculateTotalLength(const std::string &filePath)
{
    struct stat statBuf;
    if (stat(filePath.c_str(), &statBuf) != 0)
    {
        std::cerr << "[MCU] Failed to get file size: " << filePath << std::endl;
        return -1;
    }
    return static_cast<int>(statBuf.st_size);
}

// 计算总包数（每包最大 166 字节，向上取整）
uint16_t McuUpdater::CalculatePackageCount(const std::string &filePath)
{
    int totalLength = CalculateTotalLength(filePath);
    if (totalLength <= 0)
    {
        return 0;
    }

    return (totalLength + MAX_CHUNK_SIZE - 1) / MAX_CHUNK_SIZE;
}

std::vector<uint8_t> McuUpdater::ReadFileToBuffer(const std::string &filePath)
{
    std::ifstream file(filePath, std::ios::binary | std::ios::ate); // 以二进制方式打开，并跳到末尾获取大小
    if (!file)
    {
        std::cerr << "[ERROR] Failed to open file: " << filePath << std::endl;
        return {};
    }

    std::streamsize fileSize = file.tellg(); // 获取文件大小
    file.seekg(0, std::ios::beg);            // 回到文件开头

    std::vector<uint8_t> buffer(fileSize); // 分配足够的空间
    if (!file.read(reinterpret_cast<char *>(buffer.data()), fileSize))
    {
        std::cerr << "[ERROR] Failed to read file: " << filePath << std::endl;
        return {};
    }

    return buffer;
}

std::vector<uint8_t> ExtractPackage(const std::vector<uint8_t> &fileData, uint16_t index)
{
    size_t offset = index * MAX_CHUNK_SIZE;
    if (offset >= fileData.size())
        return {};

    size_t length = std::min(MAX_CHUNK_SIZE, fileData.size() - offset);
    return std::vector<uint8_t>(fileData.begin() + offset, fileData.begin() + offset + length);
}

bool McuUpdater::SendDownloadPackage(const std::vector<uint8_t> &package, uint16_t index)
{
    // 构造 payload：2字节 index + 数据内容
    std::vector<uint8_t> payload;
    payload.push_back(static_cast<uint8_t>((index >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>(index & 0xFF));
    payload.insert(payload.end(), package.begin(), package.end());

    std::cout << "[MCU] Sending Flash-driver package index: " << index
              << ", size: " << package.size() << " bytes" << std::endl;

    const uint8_t *recvBuf = nullptr;
    uint16_t recvLen = 0;

    // 使用 SendAndRecv 发送数据包并等待回应
    if (!dataCtrl_->SendAndRecv(DATA_TYPE_DOWNLOAD_DATA, payload.data(), payload.size(), recvBuf, recvLen))
    {
        std::cerr << "[MCU] Failed to send or receive response from MCU at package " << index << std::endl;
        return false;
    }

    // 检查回应长度是否足够（应至少为3字节）
    if (recvLen < 3 || recvBuf == nullptr)
    {
        std::cerr << "[MCU] Invalid response from MCU for package " << index << std::endl;
        return false;
    }

    // 解析响应的 index 和状态
    uint16_t respIndex = (static_cast<uint16_t>(recvBuf[0]) << 8) | recvBuf[1];
    uint8_t respStatus = recvBuf[2];

    // 检查 index 是否匹配
    if (respIndex != index)
    {
        std::cerr << "[MCU] Mismatched index in MCU response. Expected: " << index
                  << ", got: " << respIndex << std::endl;
        return false;
    }

    // 检查状态码
    if (respStatus == 0x01) // CRC_FAIL
    {
        std::cerr << "[MCU] Flash-driver CRC failed at package " << index << ", restarting...\n";
        return false;
    }

    return true;
}

// 计算 MD5 值为 16 字节二进制数组
std::array<uint8_t, 16> CalculateMd5Binary(const std::vector<uint8_t> &data)
{
    MD5_CTX context;
    unsigned char digest[16] = {0};

    MD5Init(&context);
    MD5Update(&context, const_cast<unsigned char *>(data.data()), data.size());
    MD5Final(&context, digest);

    std::array<uint8_t, 16> md5Bytes;
    std::copy(digest, digest + 16, md5Bytes.begin());
    return md5Bytes;
}

// 发送 MD5 校验请求，包含 16 字节原始 MD5 值
bool McuUpdater::SendMd5ChecksumRequest(const std::array<uint8_t, 16> &md5Bytes)
{
    const uint8_t *recvBuf = nullptr;
    uint16_t recvLen = 0;

    std::cout << "[MCU] Sending MD5 checksum for whole package...\n";
    bool sendSuccess = dataCtrl_->SendAndRecv(DATA_TYPE_CHECK_DATA,
                                              md5Bytes.data(), md5Bytes.size(),
                                              recvBuf, recvLen);

    bool result = false;

    if (sendSuccess && recvBuf != nullptr && recvLen >= 1)
    {
        if (recvBuf[0] == 0x00) // OK
        {
            std::cout << "[MCU] MD5 checksum acknowledged by MCU.\n";
            result = true;
        }
        else if (recvBuf[0] == 0x01) // NG
        {
            std::cerr << "[MCU] MD5 checksum failed, MCU responded NG.\n";
        }
        else
        {
            std::cerr << "[MCU] Unexpected response code: 0x"
                      << std::hex << static_cast<int>(recvBuf[0]) << "\n";
        }
    }
    else
    {
        std::cerr << "[MCU] Failed to send MD5 checksum or receive valid response.\n";
    }

    return result;
}

bool McuUpdater::FlashDriverUpdate(const std::string &flashDriverPath, UpdateSta_s &updateStatus, uint8_t totalFiles)
{
    std::cout << "[MCU] Ready to flash MCU with driver: " << flashDriverPath << std::endl;

    bool result = true;

    std::vector<uint8_t> fileData = ReadFileToBuffer(flashDriverPath);
    if (fileData.empty())
    {
        std::cerr << "[MCU] Failed to read Flash-driver binary.\n";
        result = false;
    }

    if (result)
    {
        uint32_t totalLength = static_cast<uint32_t>(fileData.size());
        uint16_t totalPackages = CalculatePackageCount(fileData);

        if (!SendDownloadRequest(DownloadRequestType::FLASH_DRIVER, totalPackages, totalLength))
        {
            std::cerr << "[MCU] Failed to send download request.\n";
            result = false;
        }
        else
        {
            for (uint16_t i = 0; i < totalPackages; ++i)
            {
                auto package = ExtractPackage(fileData, i);

                if (!SendDownloadPackage(package, i))
                {
                    std::cerr << "[MCU] Failed to send package " << i << ", restarting...\n";
                    result = false;
                    break;
                }
            }

            if (result)
            {
                auto md5Bytes = CalculateMd5Binary(fileData);

                if (!SendMd5ChecksumRequest(md5Bytes))
                {
                    std::cerr << "[MCU] MD5 校验请求失败，终止升级流程！\n";
                    result = false;
                }
            }
        }
    }

    if (result)
    {
        std::cout << "[MCU] Flash-driver transmission complete. Proceeding to APP flash phase..." << std::endl;
    }

    return result;
}

bool McuUpdater::SendAppEraseRequest()
{
    constexpr uint8_t ERASE_CMD = 0x01;
    constexpr uint8_t STATUS_OK = 0x00;
    constexpr uint8_t STATUS_NG = 0x01;
    constexpr uint8_t STATUS_ERASING = 0x02;

    dataCtrl_->PrivateProtocolEncap(DataPlaneType_e::DATA_TYPE_ERASE_APP, &ERASE_CMD, sizeof(ERASE_CMD));

    uint8_t recvBuf[COMM_DATA_LEN_MAX] = {0};
    bool success = false;

    if (SendAndRecvWithRetry(recvBuf, sizeof(recvBuf))) // 重试机制完全封装于此
    {
        uint8_t status = recvBuf[0]; // 根据协议判断状态

        switch (status)
        {
        case STATUS_OK:
            std::cout << "[MCU] MCU erase ready. Version: " << outVersion << std::endl;
            success = true;
            break;

        case STATUS_ERASING:
            std::cerr << "[MCU] MCU reported 'erasing' after max retries. Giving up." << std::endl;
            break;

        case STATUS_NG:
            std::cerr << "[MCU] MCU erase failed. Status NG." << std::endl;
            break;

        default:
            std::cerr << "[MCU] Unknown response status: " << static_cast<int>(status) << std::endl;
            break;
        }
    }
    else
    {
        std::cerr << "[MCU] Failed to send erase command after retries." << std::endl;
    }

    return success;
}

bool McuUpdater::AppUpdate(const std::string &appPath)
{
    std::cout << "[MCU] Ready to flash MCU with APP: " << appPath << std::endl;

    bool result = true;

    std::vector<uint8_t> fileData = ReadFileToBuffer(appPath);
    if (fileData.empty())
    {
        std::cerr << "[MCU] Failed to read APP binary.\n";
        result = false;
    }
    else
    {
        uint32_t totalLength = static_cast<uint32_t>(fileData.size());
        uint16_t totalPackages = CalculatePackageCount(fileData);

        // Step 1）发送擦除命令
        if (!SendAppEraseRequest())
        {
            std::cerr << "[MCU] Failed to request MCU app erase. Aborting update." << std::endl;
            result = false;
        }
        else
        {
            std::cout << "[MCU] Erase request sent successfully. Proceeding with OTA update..." << std::endl;

            // Step 2）发送下载请求
            if (!SendDownloadRequest(DownloadRequestType::APP, totalPackages, totalLength))
            {
                std::cerr << "[MCU] Failed to send download request for APP.\n";
                result = false;
            }
            else
            {
                // Step 3）发送所有 APP 数据包
                for (uint16_t i = 0; i < totalPackages; ++i)
                {
                    auto package = ExtractPackage(fileData, i);

                    if (!SendDownloadPackage(package, i))
                    {
                        std::cerr << "[MCU] Failed to send package " << i << ", aborting...\n";
                        result = false;
                        break;
                    }
                }

                // Step 4）如果之前没失败，发送 MD5 校验请求
                if (result)
                {
                    auto md5Bytes = CalculateMd5Binary(fileData);
                    if (!SendMd5ChecksumRequest(md5Bytes))
                    {
                        std::cerr << "[MCU] MD5 校验请求失败，终止升级流程！\n";
                        result = false;
                    }
                }

                // Step 5）成功完成
                if (result)
                {
                    std::cout << "[MCU] APP update successful. Proceeding to reset MCU...\n";
                }
            }
        }
    }

    return result;
}

bool McuUpdater::Flashing(const std::string &flashDiverPath, const std::string &appPat, UpdateSta_s &updateStatus, uint8_t totalFiles)
{
    bool result = true;
    // 1.Flash-driver 刷写阶段
    if (!FlashDriverUpdate(flashDiverPath, updateStatus, totalFiles))
    {
        std::cerr << "[MCU] Flash-driver update failed.\n";
        result = false;
    }

    // 2.APP 刷写阶段
    if (!AppUpdate(appPat, updateStatus, totalFiles))
    {
        std::cerr << "[MCU] APP update failed.\n";
        result = false;
    }

    std::cout << "[MCU] Flashing completed successfully.\n";
    return result;
}

bool McuUpdater::NotifyMcuReset()
{
    constexpr uint8_t RESET_CMD = 0x01;
    const uint8_t *payloadPtr = nullptr;
    uint16_t payloadLen = 0;
    bool success = false;

    std::cout << "[MCU] Sending MCU reset request." << std::endl;

    success = dataCtrl_->SendAndRecv(DataPlaneType_e::DATA_TYPE_RESET,
                                     &RESET_CMD,
                                     sizeof(RESET_CMD),
                                     payloadPtr,
                                     payloadLen);

    if (!success)
    {
        std::cerr << "[MCU] Failed to send reset command." << std::endl;
    }

    return success;
}

bool McuUpdater::Reboot()
{
    bool result = false;
    bool activeFlag = false;

    do
    {
        if (!JsonHelper::GetInstance().Readbool("/data/config/ota/ota_info.json", "active_flag", &activeFlag))
        {
            std::cout << "[MCU] Activation flag not found or invalid. Skipping reboot." << std::endl;
            break;
        }

        if (!activeFlag)
        {
            std::cout << "[MCU] Activation flag is false. Skipping reboot." << std::endl;
            break;
        }

        std::cout << "[MCU] Activation flag detected. Rebooting..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        bool resetFlag = true;
        if (!JsonHelper::GetInstance().Writebool("/data/config/ota/ota_info.json", "reset_flag", &resetFlag))
        {
            std::cerr << "[MCU] Failed to write reset flag." << std::endl;
            break;
        }

        if (!NotifyMcuReset())
        {
            std::cerr << "[MCU] NotifyMcuReset failed." << std::endl;
            break;
        }

        result = true;

    } while (false);

    return result;
}
