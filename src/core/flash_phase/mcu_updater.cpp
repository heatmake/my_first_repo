#include "mcu_updater.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <regex>
#include <tuple>
#include <fstream>
#include "otalog.h"

bool McuUpdater::Update(const std::vector<fs::path>& mcuPackages, std::function<void(int)> progressCallback)
{
    bool result = false;
    std::string mcuFlashPath;
    std::string mcuAppPath;

    for (const auto& path : mcuPackages) {
        const std::string filename = path.filename().string();

        if (std::regex_match(filename, std::regex(R"(MCU_FLASH_DRIVER.*\.bin)"))) {
            OTALOG(OlmInstall, OllInfo, "[MCU]detecting MCU_FLASH_DRIVER package: %s\n", filename.c_str());
            mcuFlashPath = path.string();
        } else if (std::regex_match(filename, std::regex(R"(MCU_APP.*\.bin)"))) {
            OTALOG(OlmInstall, OllInfo, "[MCU]detecting MCU_APP package: %s\n", filename.c_str());
            mcuAppPath = path.string();
        }
    }

    if (TryUpdateOnce(mcuFlashPath, mcuAppPath, progressCallback)) {
        OTALOG(OlmInstall, OllInfo, "[MCU]Update successful.\n");
        result = true;
    } else {
        OTALOG(OlmInstall, OllError, "[MCU]Update failed.\n");
        result = false;
    }
    return result;
}

bool McuUpdater::TryUpdateOnce(std::string& flashDiverPath,
                               std::string& appPath,
                               std::function<void(int)> progressCallback)
{
    return PreUpdate(appPath) && Flashing(flashDiverPath, appPath, progressCallback);

}

// 从文件名中提取版本号
std::string McuUpdater::ExtractVersionFromFilename(const std::string& filename)
{
    // 匹配 -V<主>.<子>.<修正> 形式的版本号
    std::regex versionRegex(R"(-V(\d+)\.(\d+)\.(\d+))");
    std::smatch match;

    if (std::regex_search(filename, match, versionRegex) && match.size() == 4) {
        return match[1].str() + "." + match[2].str() + "." + match[3].str();
    }

    return ""; // 匹配失败
}

// 版本号比较函数：true 表示 newMcuVersion 更高
bool McuUpdater::IsVersionNewer(const std::string& newMcuVersion, const std::string& receivedVersion)
{
    auto parseVersion = [](const std::string& version) {
        int major = 0, minor = 0, patch = 0;
        std::sscanf(version.c_str(), "%d.%d.%d", &major, &minor, &patch);
        return std::make_tuple(major, minor, patch);
    };

    auto [newMajor, newMinor, newPatch] = parseVersion(newMcuVersion);
    auto [oldMajor, oldMinor, oldPatch] = parseVersion(receivedVersion);

    return (newMajor > oldMajor) || (newMinor > oldMinor) || (newPatch > oldPatch);
}

bool McuUpdater::PreUpdate(const std::string& appPath)
{
    OTALOG(OlmInstall, OllInfo, "[MCU]Pre-update checks...\n");
    bool success = false;
    std::string receivedVersion;
    std::string newMcuVersion = ExtractVersionFromFilename(appPath);

    do {
        // 1. 写入 flash_mode
        if (!JsonHelper::GetInstance().WriteString(K_OTA_INFO_JSON_PATH, "flash_mode", "normal")) {
            OTALOG(OlmInstall, OllError, "[MCU]Failed to write flash_mode to JSON.\n");
            break;
        }

        // 2. 提取版本
        if (newMcuVersion.empty()) {
            OTALOG(OlmInstall, OllError, "[MCU]failed to extract newMcuVersion from filename. Aborting.\n");
            break;
        }

        // 3. 发送预检查并获取版本号
        std::string receivedVersion;
        if (!DataCtrl::GetInstance().RetryOperation(
                "PreProgramCheck", [&]() { return DataCtrl::GetInstance().PreProgramCheck(receivedVersion); })) {
            std::cerr << "[MCU]Pre-program check failed. Aborting." << std::endl;
            break;
        }
        // 4. 判断版本关系
        receivedVersion = ExtractVersionFromFilename(receivedVersion);
        if (IsVersionNewer(newMcuVersion, receivedVersion)) {
            OTALOG(OlmInstall, OllInfo, "[MCU]current MCU version is newer,record current MCU version\n");
            if (!JsonHelper::GetInstance().WriteString(K_OTA_INFO_JSON_PATH, "mcu_version", newMcuVersion)) {
                OTALOG(OlmInstall, OllError, "[MCU]Failed to write mcu_version to JSON.\n");
                break;
            }
        } else if (newMcuVersion == receivedVersion) {
            OTALOG(OlmInstall, OllInfo, "[MCU]newerversion is same as receivedversion，skip update.\n");
            break;
        } else {
            OTALOG(OlmInstall, OllError, "[MCU]newerversion is lower than receivedversion，update is forbidden.\n");
            OTALOG(OlmInstall,
                   OllInfo,
                   "[MCU]newerversion: %s,receivedversion: %s\n",
                   newMcuVersion.c_str(),
                   receivedVersion.c_str());
            break;
        }

        success = true;

    } while (false);

    return success;
}

std::vector<uint8_t> McuUpdater::ReadFileToBuffer(const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::binary | std::ios::ate); // 以二进制方式打开，并跳到末尾获取大小
    if (!file) {
        OTALOG(OlmInstall, OllError, "[ERROR] Failed to open file: %s\n", filePath.c_str());
        return {};
    }

    std::streamsize fileSize = file.tellg(); // 获取文件大小
    file.seekg(0, std::ios::beg);            // 回到文件开头

    std::vector<uint8_t> buffer(fileSize); // 分配足够的空间
    if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
        OTALOG(OlmInstall, OllError, "[ERROR] Failed to read file: %s\n", filePath.c_str());
        return {};
    }

    return buffer;
}

std::vector<uint8_t> McuUpdater::ExtractPackage(const std::vector<uint8_t>& fileData, uint16_t index)
{
    size_t offset = index * MAX_CHUNK_SIZE;
    if (offset >= fileData.size())
        return {};

    size_t length = std::min(MAX_CHUNK_SIZE, fileData.size() - offset);
    return std::vector<uint8_t>(fileData.begin() + offset, fileData.begin() + offset + length);
}

// 计算 MD5 值为 16 字节二进制数组
std::array<uint8_t, 16> McuUpdater::CalculateMd5Binary(const std::vector<uint8_t>& data)
{
    MD5_CTX context;
    unsigned char digest[16] = {0};

    MD5Init(&context);
    MD5Update(&context, const_cast<unsigned char*>(data.data()), data.size());
    MD5Final(&context, digest);

    std::array<uint8_t, 16> md5Bytes;
    std::copy(digest, digest + 16, md5Bytes.begin());
    return md5Bytes;
}

bool McuUpdater::FlashDriverUpdate(const std::string& flashDriverPath, std::function<void(int)> progressCallback)
{
    OTALOG(OlmInstall, OllInfo, "[MCU]Ready to flash MCU with driver: %s\n", flashDriverPath.c_str());

    std::vector<uint8_t> fileData = ReadFileToBuffer(flashDriverPath);
    if (fileData.empty()) {
        OTALOG(OlmInstall, OllError, "[MCU]Failed to read Flash-driver binary.\n");
        return false;
    }

    const uint32_t totalLength = static_cast<uint32_t>(fileData.size());
    const uint16_t totalPackages = static_cast<uint16_t>((fileData.size() + MAX_CHUNK_SIZE - 1) / MAX_CHUNK_SIZE);
    bool result = false;

    // 允许重试整个过程（例如最多尝试两次）
    const int maxRetryCount = 6;
    for (int retry = 0; retry < maxRetryCount; ++retry) {
        OTALOG(OlmInstall, OllInfo, "[MCU]Attempt %d: Sending DownloadRequest...\n", retry + 1);

        // Step 1: 发送 DownloadRequest
        if (!DataCtrl::GetInstance().RetryOperation("SendDownloadRequest", [&]() {
                return DataCtrl::GetInstance().SendDownloadRequest(
                    DownloadRequestType::FLASH_DRIVER, totalPackages, totalLength);
            })) {
            OTALOG(OlmInstall, OllError, "[MCU]SendDownloadRequest failed. Aborting.\n");
            continue; // 重试整个过程
        }

        // Step 2: 逐包发送数据
        bool allPackagesSent = false;
        for (int attempt = 1; attempt <= 3; ++attempt) {
            allPackagesSent = true;
            OTALOG(OlmInstall, OllInfo, "[MCU]Starting flash package transmission attempt: %d\n", attempt);

            for (uint16_t i = 0; i < totalPackages; ++i) {
                auto package = ExtractPackage(fileData, i);
                if (!DataCtrl::GetInstance().SendDownloadPackage(package, i)) {
                    OTALOG(OlmInstall,
                           OllError,
                           "[MCU]SendDownloadPackage failed at index %d. Retrying whole process.\n",
                           i);
                    allPackagesSent = false;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            if (allPackagesSent) {
                OTALOG(OlmInstall, OllInfo, "[MCU]All packages sent successfully on attempt %d.\n", attempt);
                break; // 发送成功，退出外层重试循环
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 等待一会再重新发送
            }
        }

        // Step 3: 校验 MD5（前提是包全部发完）
        if (allPackagesSent) {
            auto md5Bytes = CalculateMd5Binary(fileData);
            if (!DataCtrl::GetInstance().RetryOperation("SendMd5ChecksumRequest", [&]() {
                    return DataCtrl::GetInstance().SendMd5ChecksumRequest(md5Bytes);
                })) {
                OTALOG(OlmInstall, OllError, "[MCU]SendMd5ChecksumRequest failed. Aborting.\n");
                continue; // 重试整个过程
            }

            // 成功完成所有步骤
            result = true;
            break;
        }
    }

    if (result) {
        OTALOG(OlmInstall, OllInfo, "[MCU]Flash-driver transmission complete. Proceeding to APP flash phase\n");
        if (progressCallback) {
            progressCallback(1);
        }
    }

    return result;
}

bool McuUpdater::AppUpdate(const std::string& appPath, std::function<void(int)> progressCallback)
{
    OTALOG(OlmInstall, OllInfo, "[MCU]Ready to flash MCU with APP: %s\n", appPath.c_str());
    std::vector<uint8_t> fileData = ReadFileToBuffer(appPath);
    if (fileData.empty()) {
        OTALOG(OlmInstall, OllError, "[MCU]Failed to read APP binary.\n");
        return false;
    }

    const uint32_t totalLength = static_cast<uint32_t>(fileData.size());
    const uint16_t totalPackages = static_cast<uint16_t>((fileData.size() + MAX_CHUNK_SIZE - 1) / MAX_CHUNK_SIZE);
    bool result = false;

    const int maxRetryCount = 6;
    for (int retry = 0; retry < maxRetryCount; ++retry) {
        OTALOG(OlmInstall, OllInfo, "[MCU]Attempt %d: Starting APP update process...\n", retry + 1);
        // Step 1）发送擦除命令
        if (!DataCtrl::GetInstance().RetryOperation("SendAppEraseRequest",
                                                    [&]() { return DataCtrl::GetInstance().SendAppEraseRequest(); })) {
            OTALOG(OlmInstall, OllError, "[MCU]SendAppEraseRequest failed. Retrying whole process.\n");
            continue;
        }

        // Step 2）发送下载请求
        if (!DataCtrl::GetInstance().RetryOperation("SendDownloadRequest", [&]() {
                return DataCtrl::GetInstance().SendDownloadRequest(
                    DownloadRequestType::APP, totalPackages, totalLength);
            })) {
            OTALOG(OlmInstall, OllError, "[MCU]SendDownloadRequest failed. Retrying whole process.\n");
            continue;
        }

        // Step 3）发送所有 APP 数据包（最多尝试两轮）
        bool allPackagesSent = false;
        for (int attempt = 1; attempt <= 3; ++attempt) {
            allPackagesSent = true;
            OTALOG(OlmInstall, OllInfo, "[MCU]Starting APP package transmission attempt: %d\n", attempt);

            for (uint16_t i = 0; i < totalPackages; ++i) {
                auto package = ExtractPackage(fileData, i);
                if (!DataCtrl::GetInstance().SendDownloadPackage(package, i)) {
                    OTALOG(
                        OlmInstall,
                        OllError,
                        "[MCU]SendDownloadPackage failed at index %d on attempt %d. Retrying whole package process.\n",
                        i,
                        attempt);
                    allPackagesSent = false;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            if (allPackagesSent) {
                OTALOG(OlmInstall, OllInfo, "[MCU]All APP packages sent successfully on attempt %d.\n", attempt);
                break;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        // Step 4）发送 MD5 校验
        if (allPackagesSent) {
            auto md5Bytes = CalculateMd5Binary(fileData);
            if (!DataCtrl::GetInstance().RetryOperation("SendMd5ChecksumRequest", [&]() {
                    return DataCtrl::GetInstance().SendMd5ChecksumRequest(md5Bytes);
                })) {
                OTALOG(OlmInstall, OllError, "[MCU]SendMd5ChecksumRequest failed. Retrying whole process.\n");
                continue;
            }

            // 成功
            result = true;
            break;
        }
    }

    if (result) {
        OTALOG(OlmInstall, OllInfo, "[MCU]APP update complete. Rebooting MCU...\n");
        if (progressCallback) {
            progressCallback(1);
        }
    }

    return result;
}

bool McuUpdater::Flashing(const std::string& flashDiverPath,
                          const std::string& appPath,
                          std::function<void(int)> progressCallback)
{
    bool result = true;

    do {
        // Flash-driver 刷写阶段
        if (!FlashDriverUpdate(flashDiverPath, progressCallback)) {
            OTALOG(OlmInstall, OllError, "[MCU]Flash-driver update failed.\n");
            result = false;
            break; // 直接退出，不进行 APP 刷写
        }

        // APP 刷写阶段
        if (!AppUpdate(appPath, progressCallback)) {
            OTALOG(OlmInstall, OllError, "[MCU]APP update failed.\n");
            result = false;
            break; // 失败后立即退出
        }

    } while (false);

    if (result) {
        OTALOG(OlmInstall, OllInfo, "[MCU]Flashing completed successfully.\n");
    }

    return result;
}

bool McuUpdater::Reboot()
{
    bool result = false;
    uint8_t activeFlag = 1;
    bool resetFlag = true;

    // 等待 active_flag 存在且为 true
    while (true) {
        if (JsonHelper::GetInstance().ReadInt(K_OTA_INFO_JSON_PATH, "active_flag", &activeFlag)) {
            if (activeFlag == 1) {
                OTALOG(OlmInstall, OllInfo, "[MCU]active_flag found and set to true.\n");
                break; // 退出等待
            } else {
                OTALOG(OlmInstall, OllInfo, "[MCU]active_flag not found or set to false, waiting...\n");
            }
        } else {
            OTALOG(OlmInstall, OllInfo, "[MCU]active_flag not found, waiting...\n");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    activeFlag = 0;
    JsonHelper::GetInstance().WriteInt(K_OTA_INFO_JSON_PATH, "active_flag", activeFlag);
    if (JsonHelper::GetInstance().WriteBool(K_OTA_INFO_JSON_PATH, "reset_flag", resetFlag)) {
        if (DataCtrl::GetInstance().RetryOperation("NotifyMcuReset",
                                                   [&]() { return DataCtrl::GetInstance().NotifyMcuReset(); })) {
            result = true;
        } else {
            OTALOG(OlmInstall, OllError, "[MCU]NotifyMcuReset failed. Aborting.\n");
        }
    } else {
        OTALOG(OlmInstall, OllError, "[MCU]Failed to write reset flag.\n");
    }

    return result;
}
