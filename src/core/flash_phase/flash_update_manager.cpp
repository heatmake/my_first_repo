#include "flash_update_manager.h"
#include <regex>
#include <filesystem>
#include <iostream>
#include "otalog.h"

namespace fs = std::filesystem;

FlashUpdateManager::FlashUpdateManager()
{
    socUpdater_ = std::make_shared<SocUpdater>();
    mcuUpdater_ = std::make_shared<McuUpdater>();
    updateStatus_.stage = 1;   // 升级未开始
    updateStatus_.process = 0; // 0%
}

FlashUpdateManager::~FlashUpdateManager()
{
    if (updateThread_.joinable()) {
        updateThread_.join();
    }
}

OtaStatus_e FlashUpdateManager::StartUpdate(const char* path)
{
    OtaStatus_e result = OTA_STATUS_SUCCESS;
    if (!path) {
        result = OTA_STATUS_FAILED;;
        return result;
    }
    packagePath_ = path;
    updateThread_ = std::thread(&FlashUpdateManager::UpdateThreadFunc, this);
    return result;
}

OtaStatus_e FlashUpdateManager::GetUpdateStatus(UpdateSta_s* status)
{
    if (!status) {
        OTALOG(OlmInstall, OllError, "[FlashUpdateManager]GetUpdateStatus failed, status is null.\n");
        return OTA_STATUS_FAILED;
    }
    status->stage = updateStatus_.stage;
    status->process = updateStatus_.process;
    return OTA_STATUS_SUCCESS;
}

// 创建目录（如果不存在）
bool FlashUpdateManager::EnsureDir(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return mkdir(path.c_str(), 0755) == 0;
    }
    return S_ISDIR(st.st_mode);
}

void FlashUpdateManager::BackupConfigs()
{
    OTALOG(OlmInstall, OllInfo, "[FlashUpdateManager]begin backup config\n");

    // 确保备份目录存在
    EnsureDir("/opt/seres/backup");
    EnsureDir("/data/backup");

    //备份opt/seres
    const std::vector<std::string> opt_dirs = {"start", "packages", "middleware", "calibration", "model"};
    std::string opt_cmd = "tar -czf /opt/seres/backup/seres_backup.tar.gz";
    bool opt_has_valid = false;

    for (const auto& dir : opt_dirs) {
        std::string full_path = "/opt/seres/" + dir;
        if (fs::exists(full_path)) {
            opt_cmd += " -C /opt/seres " + dir;
            opt_has_valid = true;
        }
    }

    int ret1 = 0;
    if (opt_has_valid) {
        ret1 = system((opt_cmd + " > /dev/null 2>&1").c_str());
    } else {
        OTALOG(OlmInstall, OllInfo, "[FlashUpdateManager]no valid /opt/seres directories to back up\n");
    }

    //备份/data
    const std::vector<std::string> data_dirs = {"config", "db"};
    std::string data_cmd = "tar -czf /data/backup/data_backup.tar.gz";
    bool data_has_valid = false;

    for (const auto& dir : data_dirs) {
        std::string full_path = "/data/" + dir;
        if (fs::exists(full_path)) {
            data_cmd += " -C /data " + dir;
            data_has_valid = true;
        }
    }

    int ret2 = 0;
    if (data_has_valid) {
        ret2 = system((data_cmd + " > /dev/null 2>&1").c_str());
    } else {
        OTALOG(OlmInstall, OllInfo, "[FlashUpdateManager]no valid /data directories to back up\n");
    }

    if ((opt_has_valid ? ret1 == 0 : true) && (data_has_valid ? ret2 == 0 : true)) {
        OTALOG(OlmInstall, OllInfo, "[FlashUpdateManager]backup success\n");
    } else {
        OTALOG(OlmInstall, OllError, "[FlashUpdateManager]backup process failed\n");
    }
}


bool FlashUpdateManager::UnzipPackage(const std::string& zip_path)
{
    std::string cmd = "unzip -oq \"" + zip_path + "\" -d \"" + fs::path(zip_path).parent_path().string() + "\"";
    return std::system(cmd.c_str()) == 0;
}

bool FlashUpdateManager::PrepareUpdateEnvironment(const std::string& zip_path,
                                                  std::vector<fs::path>& socPackages,
                                                  std::vector<fs::path>& mcuPackages)
{
    bool success = true;
    socPackages.clear();
    mcuPackages.clear();
    OTALOG(OlmInstall, OllInfo, "[FlashUpdateManager]unzip package: %s\n", zip_path.c_str());
    if (!UnzipPackage(zip_path)) {
        OTALOG(OlmInstall, OllError, "[FlashUpdateManager]unzip failed.\n");
        success = false;
    }

    if (success) {
        fs::path extracted_dir = fs::path(zip_path).parent_path();
        OTALOG(OlmInstall, OllInfo, "[FlashUpdateManager]unzip directory: %s\n", extracted_dir.string().c_str());
        try {
            for (const auto& entry : fs::directory_iterator(extracted_dir)) {
                const std::string filename = entry.path().filename().string();

                if (std::regex_match(filename, std::regex(R"(app.*\.deb)"))
                    || std::regex_match(filename, std::regex(R"(robot.*\.deb)"))
                    || std::regex_match(filename, std::regex(R"(navi.*\.deb)"))
                    || std::regex_match(filename, std::regex(R"(mc.*\.deb)"))
                    || std::regex_match(filename, std::regex(R"(rte.*\.deb)"))
                    || std::regex_match(filename, std::regex(R"(SOC.*\.tar.gz)"))) {
                    socPackages.push_back(entry.path());
                } else if (std::regex_match(filename, std::regex(R"(MCU_FLASH_DRIVER.*\.bin)"))
                           || std::regex_match(filename, std::regex(R"(MCU_APP.*\.bin)"))) {
                    mcuPackages.push_back(entry.path());
                }
            }
        } catch (const std::exception& e) {
            OTALOG(OlmInstall, OllError, "[FlashUpdateManager]forereach directory failed: %s\n", e.what());
            success = false;
        }
    }

    return success;
}

void FlashUpdateManager::UpdateThreadFunc()
{

    updateStatus_.stage = 1; // 升级开始
    std::vector<fs::path> socPackages;
    std::vector<fs::path> mcuPackages;
    if (!PrepareUpdateEnvironment(packagePath_, socPackages, mcuPackages)) {
        updateStatus_.stage = 2; // 升级失败
        return;
    }
    int totalPackageCount = socPackages.size() + mcuPackages.size();
    int finishedCount = 0;

    auto updateProgress = [&](int processed) {
        finishedCount += processed;
        updateStatus_.process = static_cast<int>(finishedCount * 100 / totalPackageCount);
        std::string log_msg = "[FlashUpdateManager]Overall progress: " + std::to_string(updateStatus_.process) + "%";
        OTALOG(OlmInstall, OllInfo, "%s\n", log_msg.c_str());
    };
    bool socFlag = true;
    if (!socPackages.empty()) {
        for (auto& package : socPackages) {
            OTALOG(OlmInstall, OllInfo, "[FlashUpdateManager]upgrade software package: %s\n", package.string().c_str());
        }
        if (!JsonHelper::GetInstance().WriteBool(K_OTA_INFO_JSON_PATH, "soc_flag", socFlag)) {
            OTALOG(OlmInstall, OllError, "[FlashUpdateManager]WriteBool soc_flag failed.\n");
        }
        // 开始备份
        BackupConfigs();
        if (!socUpdater_->Update(socPackages, updateProgress)) {
            OTALOG(OlmInstall, OllError, "[FlashUpdateManager]SOC update failed.\n");
            updateStatus_.stage = 2;
            return;
        }
    }

    if (!mcuPackages.empty()) {
        socFlag = false;
        if (!JsonHelper::GetInstance().WriteBool(K_OTA_INFO_JSON_PATH, "soc_flag", socFlag)) {
            OTALOG(OlmInstall, OllError, "[FlashUpdateManager]WriteBool soc_flag failed.\n");
        }
        if (!mcuUpdater_->Update(mcuPackages, updateProgress)) {
            OTALOG(OlmInstall, OllError, "[FlashUpdateManager]MCU update failed.\n");
            updateStatus_.stage = 2;
            return;
        }
    }
    updateStatus_.stage = 0; // 升级成功

    // 创建线程，重启系统
    std::thread rebootThread([this]() {
        if (!mcuUpdater_->Reboot()) {
            OTALOG(OlmInstall, OllError, "reboot failed\n");
        }
    });
    // 分离线程（后台运行）
    rebootThread.detach();

    OTALOG(OlmInstall, OllInfo, "[FlashUpdateManager]Update process completed successfully.\n");
}
