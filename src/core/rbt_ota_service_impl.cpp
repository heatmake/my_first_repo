#include "rbt_ota_service_impl.h"
#include "sys_parameter_api.h"
#include <iostream> // 仅用于示例打印，生产中可删
#include <fstream>
#include <filesystem>
#include <iostream>
#include <regex>
#include "data_ctrl.h"

RbtOtaServiceImpl &RbtOtaServiceImpl::Instance()
{
    static RbtOtaServiceImpl instance;
    return instance;
}

RbtOtaServiceImpl::RbtOtaServiceImpl()
{
    dataCtrl_ = std::make_shared<DataCtrl>();
}

bool RbtOtaServiceImpl::SetRobotInfo(const RobotInfo_s *info)
{
    bool result = false; 

    if (info) {
        bool success = JsonHelper::Instance().WriteString("/data/config/ota/robot_info.json", "version", info->version);
        if (success) {
            result = true; 
            std::cout << "[OTA] Set Robot Info: version = " << info->version << std::endl;
        } else {
            std::cerr << "[OTA] Failed to write robot_info.json." << std::endl;
        }
    }

    return result; 
}


bool RbtOtaServiceImpl::SetOtaMode(otaMode_e mode)
{
    bool result = false;

    if (mode == OTA_MODE_CANCEL) {
        std::cout << "[OTA] OTA mode canceled, no update will be performed." << std::endl;
        otaModeFlag_ = false;

        result = JsonHelper::Instance().WriteBool("/data/config/ota/ota_info.json", "ota_mode_flag", otaModeFlag_);
        if (!result) {
            std::cerr << "[OTA] Failed to write ota_mode_flag = false" << std::endl;
        }
    } else if (mode == OTA_MODE_SET) {
        std::cout << "[OTA] OTA mode set. Starting update thread..." << std::endl;
        otaModeFlag_ = true;

        result = JsonHelper::Instance().WriteBool("/data/config/ota/ota_info.json", "ota_mode_flag", otaModeFlag_);
        if (!result) {
            std::cerr << "[OTA] Failed to write ota_mode_flag = true" << std::endl;
        }
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



bool RbtOtaServiceImpl::StartUpdate(const char *path)
{
    bool result = false;
    bool otaModeFlag = false;

    // 读取 ota_mode_flag
    try
    {
        std::ifstream ifs("/data/config/ota/ota_info.json");
        if (ifs.is_open())
        {
            nlohmann::json j;
            ifs >> j;

            if (j.contains("ota_mode_flag") && j["ota_mode_flag"].is_boolean())
            {
                otaModeFlag = j["ota_mode_flag"];
            }
        }
        else
        {
            std::cerr << "[OTA] Failed to open ota_info.json" << std::endl;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "[OTA] Exception while reading ota_info.json: " << e.what() << std::endl;
    }

    if (!otaModeFlag)
    {
        std::cerr << "[OTA] ota_mode_flag not set or false. Cannot start update." << std::endl;
        return false;
    }

    if (!path)
    {
        std::cerr << "[OTA] Invalid update path." << std::endl;
        return false;
    }

    std::cout << "[OTA] Starting update from path: " << path << std::endl;

    try
    {
        std::thread(&RbtOtaServiceImpl::RunOtaUpdateThread, this, std::string(path)).detach();
        result = true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "[OTA] Failed to start update thread: " << e.what() << std::endl;
    }

    return result;
}

void RbtOtaServiceImpl::RunOtaUpdateThread(const std::string &path)
{

    std::regex zipPattern(R"(OTA_ROBOT.*\.zip)");
    std::string zipFile;

    // 查找符合的 zip 文件
    for (const auto &entry : std::filesystem::directory_iterator(path))
    {
        if (entry.is_regular_file())
        {
            const std::string filename = entry.path().filename().string();
            if (std::regex_match(filename, zipPattern))
            {
                zipFile = entry.path().string();
                break; // 找到第一个就解压
            }
        }
    }

    if (zipFile.empty())
    {
        std::cerr << "No OTA_ROBOT*.zip file found in path: " << path << std::endl;
        return;
    }

    std::string extractDir = path; // 解压到原目录
    std::string unzipCmd = "unzip -o \"" + zipFile + "\" -d \"" + extractDir + "\"";

    std::cout << "Unzipping OTA package: " << zipFile << std::endl;

    if (system(unzipCmd.c_str()) != 0)
    {
        std::cerr << "Failed to unzip OTA package." << std::endl;
        return;
    }

    std::cout << "Unzip completed successfully." << std::endl;

    // 定义正则模式
    std::vector<std::regex> patterns = {
        std::regex(R"(rte.*\.deb)"),
        std::regex(R"(app.*\.deb)"),
        std::regex(R"(robot.*\.deb)"),
        std::regex(R"(navi.*\.deb)"),
        std::regex(R"(mc.*\.deb)"),
        std::regex(R"(SOC.*\.tar\.gz)"),
        std::regex(R"(MCU_FLASH_DRIVER.*\.bin)"),
        std::regex(R"(MCU_APP.*\.bin)")};

    // 查找匹配文件
    std::vector<std::pair<std::string, int>> matchedFiles; // 保存路径+匹配的pattern下标
    for (const auto &file : std::filesystem::directory_iterator(extractDir))
    {
        if (!std::filesystem::is_regular_file(file))
            continue;
        std::string filename = file.path().filename().string();
        for (size_t i = 0; i < patterns.size(); ++i)
        {
            if (std::regex_match(filename, patterns[i]))
            {
                matchedFiles.emplace_back(file.path().string(), i);
                break;
            }
        }
    }

    int totalFiles = matchedFiles.size();
    if (totalFiles == 0)
    {
        std::cerr << "No OTA matching files found." << std::endl;
        return;
    }

    int processed = 0;
    int totalFiles = matchedFiles.size();
    nlohmann::json ota_info;

    // 初始化为升级中
    int stage = 1;

    // 更新进度函数
    auto update_progress = [&]()
    {
        int progress_percent = totalFiles > 0
                                   ? static_cast<int>((processed * 100.0) / totalFiles)
                                   : 100;

        progress_percent = std::clamp(progress_percent, 0, 100);

        ota_info["progress"] = progress_percent;
        ota_info["stage"] = stage;

        std::ofstream ofs("/data/config/ota/ota_info.json");
        if (ofs.is_open())
        {
            ofs << ota_info.dump(4);
        }
        else
        {
            std::cerr << "[OTA] Failed to write progress to ota_info.json" << std::endl;
        }

        std::cout << "[OTA] Progress updated: " << progress_percent << "%, stage: " << stage << std::endl;
    };

    // 写初始状态（stage=1，升级中）
    update_progress();

    // 处理每个匹配到的文件
    for (const auto &[filepath, typeIndex] : matchedFiles)
    {
        std::string filename = std::filesystem::path(filepath).filename().string();
        bool success = false;

        switch (typeIndex)
        {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        {
            std::string cmd = "dpkg -i \"" + filepath + "\"";
            success = (system(cmd.c_str()) == 0);
            break;
        }
        case 5:
        {
            std::string extractCmd = "tar xjpf /application/ota/ota_tools_R36.4.0_aarch64.tbz2";
            std::string nvOtaCmd = "/application/ota/Linux_for_Tegra/tools/ota_tools/version_upgrade/nv_ota_start.sh \"" + filepath + "\"";
            success = (system(extractCmd.c_str()) == 0 && system(nvOtaCmd.c_str()) == 0);
            break;
        }
        case 6:
        {
            success = PerformMcuFlashDriver(filepath);
            break;
        }
        case 7:
        {
            success = PerformMcuAppFlash(filepath);
            break;
        }
        default:
            break;
        }

        ota_info[filename] = success ? "installed" : "failed";
        ++processed;
        update_progress();

        // 如果失败，立即退出，并标记 stage = 2
        if (!success)
        {
            stage = 2;
            update_progress();
            std::cerr << "[OTA] Upgrade failed at: " << filename << std::endl;
            return;
        }
    }

    // 所有处理成功，标记 stage = 0
    stage = 0;
    update_progress();
    std::cout << "[OTA] All OTA packages installed successfully." << std::endl;
    // 完后，MCU刷写进入复位阶段；
    PerformMcuResetAndConfirm();
}

// // 获取文件总长度
// uint32_t RbtOtaServiceImpl::CalculateTotalLength(const std::string &filePath)
// {
//     struct stat statBuf;
//     if (stat(filePath.c_str(), &statBuf) != 0)
//     {
//         std::cerr << "[OTA] Failed to get file size: " << filePath << std::endl;
//         return -1;
//     }
//     return static_cast<int>(statBuf.st_size);
// }

// 计算总包数（每包最大 166 字节，向上取整）
uint16_t RbtOtaServiceImpl::CalculatePackageCount(const std::string &filePath)
{
    int totalLength = CalculateTotalLength(filePath);
    if (totalLength <= 0)
    {
        return 0;
    }

    return (totalLength + MAX_CHUNK_SIZE - 1) / MAX_CHUNK_SIZE;
}

std::vector<uint8_t> RbtOtaServiceImpl::ReadFileToBuffer(const std::string &filePath)
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

bool RbtOtaServiceImpl::WriteOtaJson(const std::string &key, const std::string &value)
{
    const std::string filepath = "/data/config/ota/ota_info.json";
    try
    {
        nlohmann::json j;

        std::ifstream in(filepath);
        if (in.is_open())
        {
            in >> j;
            in.close();
        }

        j[key] = value;

        std::ofstream out(filepath);
        if (!out.is_open())
        {
            std::cerr << "[OTA] Failed to open JSON file for writing: " << filepath << std::endl;
            return false;
        }

        out << j.dump(4);
        out.close();
        std::cout << "[OTA] Updated OTA JSON [" << key << "]: " << value << std::endl;
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "[OTA] Exception writing to OTA JSON: " << e.what() << std::endl;
        return false;
    }
}

bool RbtOtaServiceImpl::QueryMcuReadyState(std::string &outVersion)
{
    const int RETRY_LIMIT = 6;
    const int RETRY_INTERVAL = 50;

    for (int retry = 0; retry < RETRY_LIMIT; ++retry)
    {
        uint8_t checkValue = 0x01;
        dataCtrl_->PrivateProtocolEncap(DataPlaneType_e::DATA_TYPE_PRE_PROGRAM_CHECK, &checkValue, sizeof(checkValue));

        uint8_t recvBuf[COMM_DATA_LEN_MAX] = {0};

        if (dataCtrl_->SendData(recvBuf, sizeof(recvBuf)))
        {
            std::cout << "[OTA] Sent pre-flash query to MCU... Attempt " << retry + 1 << std::endl;

            DataPro_t *protoHead = reinterpret_cast<DataPro_t *>(recvBuf);
            if (protoHead->sof != START_OF_FRAME)
            {
                std::cerr << "[OTA] Invalid start-of-frame!" << std::endl;
                return false;
            }

            uint16_t dataLen = protoHead->length;
            if (dataLen == 0 || dataLen > (COMM_DATA_LEN_MAX - sizeof(DataPro_t) - CHECK_SUM_LEN))
            {
                std::cerr << "[OTA] Invalid data length from MCU!" << std::endl;
                return false;
            }

            const char *payload = reinterpret_cast<const char *>(recvBuf + sizeof(DataPro_t));
            outVersion = std::string(payload, dataLen - 1);
            uint8_t status = static_cast<uint8_t>(payload[dataLen - 1]);

            if (status == 0x00)
            {
                std::cout << "[OTA] MCU ready. Version: " << outVersion << std::endl;
                return true;
            }
            else
            {
                std::cerr << "[OTA] MCU responded with invalid status." << std::endl;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_INTERVAL));
    }

    return false;
}

bool RbtOtaServiceImpl::SendDownloadRequest(uint16_t totalPackages, uint32_t totalLength)
{
    const uint8_t kDownloadRequestType = 0x01;
    uint8_t payload[7] = {
        kDownloadRequestType,
        static_cast<uint8_t>((totalPackages >> 8) & 0xFF),
        static_cast<uint8_t>(totalPackages & 0xFF),
        static_cast<uint8_t>((totalLength >> 24) & 0xFF),
        static_cast<uint8_t>((totalLength >> 16) & 0xFF),
        static_cast<uint8_t>((totalLength >> 8) & 0xFF),
        static_cast<uint8_t>(totalLength & 0xFF)};

    int retry = 0;
    while (retry < RETRY_LIMIT)
    {
        std::cout << "[OTA] Sending Flash-driver download request to MCU...\n";
        dataCtrl_->PrivateProtocolEncap(DATA_TYPE_DOWNLOAD_REQUEST, payload, sizeof(payload));

        uint8_t recvBuf[COMM_DATA_LEN_MAX] = {0};
        if (dataCtrl_->SendData(recvBuf, sizeof(recvBuf)) && recvBuf[0] == 0x00)
        {
            std::cout << "[OTA] MCU confirmed download request.\n";
            return true;
        }

        retry++;
        std::this_thread::sleep_for(std::chrono::milliseconds(kPeriodMs));
    }

    std::cerr << "[OTA] Download request confirmation failed.\n";
    return false;
}

bool RbtOtaServiceImpl::SendFlashDriverData(const std::vector<uint8_t> &fileData)
{
    size_t offset = 0;
    uint16_t index = 0;

    while (offset < fileData.size())
    {
        size_t payloadSize = std::min(static_cast<size_t>(COMM_DATA_LEN_MAX - 12), fileData.size() - offset);
        std::vector<uint8_t> packet = {
            static_cast<uint8_t>((index >> 8) & 0xFF),
            static_cast<uint8_t>(index & 0xFF)};

        packet.insert(packet.end(), fileData.begin() + offset, fileData.begin() + offset + payloadSize);
        dataCtrl_->PrivateProtocolEncap(DATA_TYPE_DOWNLOAD_DATA, packet.data(), packet.size());

        uint8_t recvBuf[COMM_DATA_LEN_MAX] = {0};
        if (!dataCtrl_->SendData(recvBuf, sizeof(recvBuf)))
        {
            std::cerr << "[OTA] Send failed at packet " << index << ".\n";
            return false;
        }

        uint16_t recvIndex = (recvBuf[0] << 8) | recvBuf[1];
        uint8_t status = recvBuf[2];

        if (recvIndex != index || status != 0x00)
        {
            std::cerr << "[OTA] Packet mismatch or CRC error at index " << index << ".\n";
            return false;
        }

        offset += payloadSize;
        ++index;
    }

    return true;
}

bool RbtOtaServiceImpl::PerformMd5Check(const std::string &filePath)
{
    std::string md5 = CalculateFileMd5(filePath);
    int retry = 0;

    while (retry < RETRY_LIMIT)
    {
        std::cout << "[OTA] Sending MD5 for verification...\n";
        dataCtrl_->PrivateProtocolEncap(DATA_TYPE_CHECK_DATA,
                                        reinterpret_cast<const uint8_t *>(md5.data()),
                                        md5.size());

        uint8_t recvBuf[COMM_DATA_LEN_MAX] = {0};
        if (dataCtrl_->SendData(recvBuf, sizeof(recvBuf)) && recvBuf[0] == 0x00)
        {
            std::cout << "[OTA] MD5 check passed.\n";
            return true;
        }

        retry++;
        std::this_thread::sleep_for(std::chrono::milliseconds(kPeriodMs));
    }

    std::cerr << "[OTA] MD5 verification failed.\n";
    return false;
}

bool RbtOtaServiceImpl::PerformMcuFlashDriver(const std::string &driverPath)
{
    std::cout << "[OTA] === Start MCU Flash Driver Procedure ===" << std::endl;

    // Step 1：设置刷写模式 NORMAL
    flashMode_ = NORMAL;
    if (!WriteOtaJson("flash_mode", "NORMAL"))
        return false;

    // Step 2：检测 MCU 是否就绪
    std::string mcuVersion;
    if (!QueryMcuReadyState(mcuVersion))
    {
        std::cerr << "[OTA] MCU not ready. Switching to FORCE mode.\n";
        if (!WriteOtaJson("flash_mode", "FORCE"))
            return false;
        return false; // 如需强刷，可改成 true
    }

    // Step 2.3：记录 MCU 版本
    if (!WriteOtaJson("mcu_driver_version", mcuVersion))
        return false;

    std::cout << "[OTA] Ready to flash MCU with driver: " << driverPath << std::endl;

    std::vector<uint8_t> fileData = ReadFileToBuffer(driverPath);
    uint32_t totalLength = static_cast<uint32_t>(fileData.size());
    uint16_t totalPackages = CalculatePackageCount(driverPath);

    uint8_t overallRetryCount = 0;
    bool finalDownloadSuccess = false;

    while (overallRetryCount < RETRY_LIMIT && !finalDownloadSuccess)
    {
        if (!SendDownloadRequest(totalPackages, totalLength))
        {
            overallRetryCount++;
            continue;
        }

        if (!SendFlashDriverData(fileData))
        {
            overallRetryCount++;
            continue;
        }

        if (!PerformMd5Check(driverPath))
        {
            overallRetryCount++;
            continue;
        }

        finalDownloadSuccess = true;
    }

    if (!finalDownloadSuccess)
    {
        std::cerr << "[OTA] Flash-driver download failed after all retries.\n";
        return false;
    }

    std::cout << "[OTA] Flash-driver transmission complete. Entering APP flash phase..." << std::endl;
    return true;
}

Version ParseVersionFromPath(const std::string &appPath)
{
    std::regex pattern(R"(V(\d+)\.(\d+)\.(\d+)-(\d+))");
    std::smatch match;

    if (std::regex_search(appPath, match, pattern))
    {
        return Version{
            std::stoi(match[1]),
            std::stoi(match[2]),
            std::stoi(match[3]),
            std::stoi(match[4])};
    }
    else
    {
        throw std::invalid_argument("Invalid version format in path: " + appPath);
    }
}

bool RbtOtaServiceImpl::WaitForMcuEraseAppPartitionAck()
{
    constexpr uint8_t ERASE_CMD = 0x01;
    constexpr uint8_t STATUS_OK = 0x00;
    constexpr uint8_t STATUS_NG = 0x01;
    constexpr uint8_t STATUS_ERASING = 0x02;

    for (int retry = 0; retry < RETRY_LIMIT; ++retry)
    {
        dataCtrl_->PrivateProtocolEncap(DataPlaneType_e::DATA_TYPE_ERASE_APP, &ERASE_CMD, sizeof(ERASE_CMD));

        uint8_t recvBuf[COMM_DATA_LEN_MAX] = {0};

        if (dataCtrl_->SendData(recvBuf, sizeof(recvBuf)))
        {
            uint8_t status = recvBuf[0]; // 假设 status 存在于 recvBuf[0]，请根据协议调整

            std::cout << "[OTA] Sent erase app partition command to MCU... Attempt " << retry + 1 << std::endl;

            if (status == STATUS_OK)
            {
                std::cout << "[OTA] MCU erase ready. Version: " << outVersion << std::endl;
                return true;
            }
            else if (status == STATUS_ERASING)
            {
                std::cout << "[OTA] MCU is erasing. Waiting..." << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_INTERVAL));
                continue;
            }
            else if (status == STATUS_NG)
            {
                std::cerr << "[OTA] MCU erase failed. Status NG." << std::endl;
                break;
            }
            else
            {
                std::cerr << "[OTA] Unknown response status: " << static_cast<int>(status) << std::endl;
                break;
            }
        }
        else
        {
            std::cerr << "[OTA] Failed to send erase command. Retrying..." << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_INTERVAL));
    }

    std::cerr << "[OTA] Erase command failed after " << RETRY_LIMIT << " attempts." << std::endl;
    return false;
}

bool RbtOtaServiceImpl::WaitForMcuReadyToDownload(uint16_t totalPackages, uint32_t totalLength)
{
    const uint8_t kDownloadRequestType = 0x02;
    uint8_t payload[7] = {
        kDownloadRequestType,
        static_cast<uint8_t>((totalPackages >> 8) & 0xFF),
        static_cast<uint8_t>(totalPackages & 0xFF),
        static_cast<uint8_t>((totalLength >> 24) & 0xFF),
        static_cast<uint8_t>((totalLength >> 16) & 0xFF),
        static_cast<uint8_t>((totalLength >> 8) & 0xFF),
        static_cast<uint8_t>(totalLength & 0xFF)};

    int retry = 0;
    while (retry < RETRY_LIMIT)
    {
        std::cout << "[OTA] Sending APP download request to MCU...\n";
        dataCtrl_->PrivateProtocolEncap(DATA_TYPE_DOWNLOAD_REQUEST, payload, sizeof(payload));

        uint8_t recvBuf[COMM_DATA_LEN_MAX] = {0};
        if (dataCtrl_->SendData(recvBuf, sizeof(recvBuf)) && recvBuf[0] == 0x00)
        {
            std::cout << "[OTA] MCU confirmed download request.\n";
            return true;
        }

        retry++;
        std::this_thread::sleep_for(std::chrono::milliseconds(kPeriodMs));
    }

    std::cerr << "[OTA] Download request confirmation failed.\n";
    return false;
}

bool RbtOtaServiceImpl::TransmitAppDataWithRetry(const std::string &appPath)
{
    size_t offset = 0;
    uint16_t index = 0;

    while (offset < fileData.size())
    {
        size_t payloadSize = std::min(static_cast<size_t>(COMM_DATA_LEN_MAX - 12), fileData.size() - offset);
        std::vector<uint8_t> packet = {
            static_cast<uint8_t>((index >> 8) & 0xFF),
            static_cast<uint8_t>(index & 0xFF)};

        packet.insert(packet.end(), fileData.begin() + offset, fileData.begin() + offset + payloadSize);
        dataCtrl_->PrivateProtocolEncap(DATA_TYPE_DOWNLOAD_DATA, packet.data(), packet.size());

        uint8_t recvBuf[COMM_DATA_LEN_MAX] = {0};
        if (!dataCtrl_->SendData(recvBuf, sizeof(recvBuf)))
        {
            std::cerr << "[OTA] Send failed at packet " << index << ".\n";
            return false;
        }

        uint16_t recvIndex = (recvBuf[0] << 8) | recvBuf[1];
        uint8_t status = recvBuf[2];

        if (recvIndex != index || status != 0x00)
        {
            std::cerr << "[OTA] Packet mismatch or CRC error at index " << index << ".\n";
            return false;
        }

        offset += payloadSize;
        ++index;
    }

    return true;
}

bool RbtOtaServiceImpl::WaitForMcuMd5CheckAck(const std::string &appPath)
{
    constexpr int MAX_RETRIES = 6;
    constexpr int INTERVAL_MS = 50;

    std::string md5 = CalculateFileMd5(appPath);

    for (int i = 0; i < MAX_RETRIES; ++i)
    {
        if (SendMd5CheckRequest(md5))
        {
            uint8_t response[COMM_DATA_LEN_MAX] = {0};
            if (dataCtrl_->ReceiveData(response, sizeof(response)))
            {
                if (response[0] == 0x00)
                {
                    std::cout << "[OTA] MD5 check passed." << std::endl;
                    return true;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(INTERVAL_MS));
    }

    return false;
}

void RbtOtaServiceImpl::EnterForceMode(const std::string &appPath, const std::string &reason)
{
    std::cerr << "[OTA] " << reason << " Switching to FORCE mode." << std::endl;
    flashMode_ = FORCE;
    WriteOtaJson("flash_mode", "FORCE");
    ForceFlash(appPath);
}

bool RbtOtaServiceImpl::PerformMcuAppFlash(const std::string &appPath)
{
    std::cout << "[OTA] === Start MCU App Flash Procedure ===" << std::endl;

    Version currentVersion = ParseVersionFromPath();
    Version newVersion = ParseVersionFromPath(appPath);

    if (newVersion < currentVersion)
    {
        std::cerr << "[MCU] New version < current version. Rejecting flash." << std::endl;
        return false;
    }

    if (newVersion == currentVersion)
    {
        std::cout << "[MCU] Version equal. Skip flashing." << std::endl;
        return true;
    }

    if (!WriteOtaJson("mcu_app_version", currentVersion.ToString()))
        return false;

    std::vector<uint8_t> fileData = ReadFileToBuffer(appPath);
    uint32_t totalLength = static_cast<uint32_t>(fileData.size());
    uint16_t totalPackages = CalculatePackageCount(appPath);
    // 4.1 ~ 4.2 擦除 APP 分区（周期500ms，最多6次）
    if (!WaitForMcuEraseAppPartitionAck())
    {
        EnterForceMode(appPath, "Erase app partition failed.");
        return true;
    }

    // 4.3 ~ 4.4 请求下载 APP（周期50ms，最多6次）
    if (!WaitForMcuReadyToDownload(totalPackages, totalLength))
    {
        EnterForceMode(appPath, "MCU not ready to download.");
        return true;
    }

    // 4.5 ~ 4.6 发送 APP 程序包
    if (!TransmitAppDataWithRetry(appPath))
    {
        EnterForceMode(appPath, "Transmit APP data failed.");
        return true;
    }

    // 4.7 ~ 4.8 请求整包校验（周期50ms，最多6次）
    if (!WaitForMcuMd5CheckAck(appPath))
    {
        EnterForceMode(appPath, "MD5 check failed.");
        return true;
    }

    std::cout << "[OTA] === MCU App Flash Completed Successfully ===" << std::endl;
    return true;
}

bool RbtOtaServiceImpl::WaitForMcuResetAck()
{
    constexpr uint8_t RESET_CMD = 0x01;
    bool success = false;

    for (int retry = 0; retry < RETRY_LIMIT; ++retry)
    {
        std::cout << "[OTA] Sending MCU reset request, attempt " << (retry + 1) << std::endl;

        // 封装复位指令
        dataCtrl_->PrivateProtocolEncap(DataPlaneType_e::DATA_TYPE_RESET, &RESET_CMD, sizeof(RESET_CMD));

        // 只发送数据，不接收响应
        if (dataCtrl_->SendData(nullptr, 0))
        {
            success = true;
            break;
        }
        else
        {
            std::cerr << "[OTA] Failed to send reset command. Retrying..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_INTERVAL));
        }
    }

    return success;
}

bool ReadOtaJson(const std::string& key)
{
    const std::string otaFilePath = "/data/config/ota/ota_info.json";  // 或根据你的系统路径更改
    std::ifstream file(otaFilePath);
    
    if (!file.is_open())
    {
        std::cerr << "[OTA] Failed to open OTA JSON file: " << otaFilePath << std::endl;
        return false;
    }

    try
    {
        nlohmann::json j;
        file >> j;

        if (j.contains(key) && j[key].is_boolean())
        {
            return j[key].get<bool>();
        }
        else
        {
            std::cerr << "[OTA] Key \"" << key << "\" not found or not boolean in OTA JSON." << std::endl;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[OTA] JSON parsing error: " << e.what() << std::endl;
    }

    return false;
}


bool RbtOtaServiceImpl::WaitForMcuFlashCompleteAck(std::string& outVersion)
{
    constexpr uint8_t FLASH_CMD = 0x01;

    for (int retry = 0; retry < RETRY_LIMIT; ++retry)
    {
        dataCtrl_->PrivateProtocolEncap(DataPlaneType_e::DATA_TYPE_FLASH_FINISH, &FLASH_CMD, sizeof(FLASH_CMD));

        uint8_t recvBuf[COMM_DATA_LEN_MAX] = {0};

        if (dataCtrl_->SendData(recvBuf, sizeof(recvBuf)))
        {
            uint8_t status = recvBuf[24];  // 状态在第25字节

            std::cout << "[OTA] Flash complete ack received. Attempt: " << retry + 1 << ", Status: 0x"
                      << std::hex << static_cast<int>(status) << std::dec << std::endl;

            if (status == 0x00)
            {
                // 提取前24字节为版本号
                outVersion = std::string(reinterpret_cast<char*>(recvBuf), 24);
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
        else
        {
            std::cerr << "[OTA] Failed to send flash complete command. Retrying..." << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_INTERVAL));
    }

    std::cerr << "[OTA] Flash complete command failed after " << RETRY_LIMIT << " attempts." << std::endl;
    return false;
}




bool RbtOtaServiceImpl::PerformMcuResetAndConfirmPhase(const Version &expectedVersion)
{
    std::cout << "[OTA] === Start MCU Reset & Confirm Phase ===" << std::endl;

    // 5.1 标记复位阶段并持久化
    if (!WriteOtaJson("mcu_reset_phase", true))
    {
        std::cerr << "[MCU] Failed to mark reset phase to persistent storage." << std::endl;
        return false;
    }

    // 5.2 周期发送“请求复位”指令（50ms*6）
    if (!WaitForMcuResetAck())
    {
        EnterForceMode(expectedVersion.ToString(), "MCU reset command timeout.");
        return true;
    }

    std::cout << "[MCU] Reset ACK received. Waiting for MCU to reboot..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5)); // 模拟MCU重启时间

    // 5.4 检查是否仍处于复位阶段（SOC重启场景）
    bool stillInReset = ReadOtaJson("mcu_reset_phase");
    if (!stillInReset)
    {
        std::cerr << "[MCU] Not in reset phase after reboot. Abort confirm." << std::endl;
        return false;
    }

    // 5.4 周期发送“请求完成刷写”指令（50ms*6）
    Version mcuReportedVersion;
    if (!WaitForMcuFlashCompleteAck(mcuReportedVersion))
    {
        EnterForceMode(expectedVersion.ToString(), "MCU flash complete ACK timeout.");
        return true;
    }

    // 5.6 对比MCU版本号
    if (mcuReportedVersion == expectedVersion)
    {
        std::cout << "[MCU] Version match. Flash confirmed." << std::endl;
        WriteOtaJson("mcu_reset_phase", false);
        WriteOtaJson("mcu_app_version", mcuReportedVersion.ToString());
        std::cout << "[OTA] === MCU Reset & Confirm Phase Completed ===" << std::endl;
        return true;
    }
    else
    {
        std::cerr << "[MCU] Version mismatch. Expected: "
                  << expectedVersion.ToString() << ", Got: "
                  << mcuReportedVersion.ToString() << std::endl;
        return false;
    }
}

bool RbtOtaServiceImpl::GetUpdateStatus(UpdateSta_s *status)
{
    bool result = false; // 最终返回值

    if (!status)
    {
        return false;
    }

    // 设置默认值
    status->stage = 1;
    status->process = 0;

    try
    {
        std::ifstream ifs("/data/config/ota/ota_info.json");
        if (ifs.is_open())
        {
            nlohmann::json j;
            ifs >> j;

            if (j.contains("ota_mode_flag") && j["ota_mode_flag"].is_boolean() && j["ota_mode_flag"] == true)
            {
                if (j.contains("stage") && j["stage"].is_number_integer())
                {
                    status->stage = j["stage"];
                }
                if (j.contains("progress") && j["progress"].is_number_integer())
                {
                    status->process = j["progress"];
                }
                result = true; // 成功读取并处理
            }
        }
        else
        {
            std::cerr << "[OTA] Failed to open ota_info.json" << std::endl;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "[OTA] Exception while reading ota_info.json: " << e.what() << std::endl;
    }

    return result;
}

bool RbtOtaServiceImpl::SetActive()
{
    std::cout << "[OTA] Activating update..." << std::endl;

    if (!WriteOtaJson("active_flag", "1"))
    {
        std::cerr << "[OTA] Failed to write active status" << std::endl;
        return false;
    }

    NotifyMcuReboot(); // 通知MCU准备重启
    std::cout << "[OTA] Activation started. MCU reboot notified." << std::endl;

    return true;
}


bool RbtOtaServiceImpl::GetActive(ActiveSta_s *status)
{
    if (!status)
        return false;

    status->result = ReadOtaJsonInt("result", 1);  // 默认激活中

    if (status->result == 0 || status->result == 2)
    {
        std::cout << "[OTA] Activation complete. Exiting OTA mode." << std::endl;
        //ota_mode_flag
        if (!WriteOtaJson("ota_mode_flag", "0"))
        {
            std::cerr << "[OTA] Failed to write active status" << std::endl;
            return false;
        }
        NotifyMcuExitOta();
    }
    return true;
}

