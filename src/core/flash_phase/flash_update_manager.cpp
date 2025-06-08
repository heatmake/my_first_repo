#include "flash_update_manager.h"
#include <iostream>
#include <regex>
#include <filesystem>

namespace fs = std::filesystem;
FlashUpdateManager::FlashUpdateManager()
{
    socUpdater_ = std::make_shared<SocUpdater>();
    mcuUpdater_ = std::make_shared<McuUpdater>();
}

FlashUpdateManager::~FlashUpdateManager()
{
    if (updateThread_.joinable())
    {
        updateThread_.join();
    }
}

OtaStatus_e FlashUpdateManager::StartUpdate(const char *path)
{
    OtaStatus_e result = OtaStatus_e::Success;
    if (!path)
    {
        result = OtaStatus_e::Failed;
        return result;
    }
    packagePath_ = path;
    updateThread_ = std::thread(&FlashUpdateManager::UpdateThreadFunc, this);
    return result;
}

OtaStatus_e FlashUpdateManager::GetUpdateStatus(UpdateSta_s &status)
{
    status = updateStatus_;
    return OtaStatus_e::Success;
}

void FlashUpdateManager::BackupConfigs()
{
    std::system("cp -r /opt/seres /opt/seres.bak");
    std::system("cp -r /data /data.bak");
}

bool FlashUpdateManager::UnzipPackage(const std::string &zip_path)
{
    std::string cmd = "unzip -o \"" + zip_path + "\" -d \"" + fs::path(zip_path).parent_path().string() + "\"";
    return std::system(cmd.c_str()) == 0;
}

void PrepareUpdateEnvironment(const std::string &zip_path,
                              std::vector<fs::path> &socPackages,
                              std::vector<fs::path> &mcuPackages)
{
    socPackages.clear();
    mcuPackages.clear();

    std::cout << "[SOC] 解压软件包: " << zip_path << std::endl;
    if (!UnzipPackage(zip_path))
    {
        std::cerr << "[SOC] 解压失败。\n";
        return;
    }

    std::cout << "[SOC] 备份 /opt/seres 与 /data 配置...\n";
    BackupConfigs();

    fs::path extracted_dir = fs::path(zip_path).parent_path();

    for (const auto &entry : fs::directory_iterator(extracted_dir))
    {
        const std::string filename = entry.path().filename().string();

        if (std::regex_match(filename, std::regex(R"(app.*\.deb)")) ||
            std::regex_match(filename, std::regex(R"(robot.*\.deb)")) ||
            std::regex_match(filename, std::regex(R"(navi.*\.deb)")) ||
            std::regex_match(filename, std::regex(R"(mc.*\.deb)")) ||
            std::regex_match(filename, std::regex(R"(rte.*\.deb)")) ||
            std::regex_match(filename, std::regex(R"(SOC.*\.tar\.gz)")))
        {
            socPackages.push_back(entry.path());
        }
        else if (std::regex_match(filename, std::regex(R"(MCU_FLASH_DRIVER.*\.bin)")) ||
                 std::regex_match(filename, std::regex(R"(MCU_APP.*\.bin)")))
        {
            mcuPackages.push_back(entry.path());
        }
    }
}

void FlashUpdateManager::UpdateThreadFunc()
{
    std::vector<fs::path> socPackages;
    std::vector<fs::path> mcuPackages;
    PrepareUpdateEnvironment(packagePath_, socPackages, mcuPackages);
    const uint8_t totalPackageCount = socPackages.size() + mcuPackages.size();

    std::cout << "[UpdateManager] Starting SOC update...\n";
    if (!socUpdater_.Update(socPackages,updateStatus_,totalPackageCount))
    {
        std::cerr << "[UpdateManager] SOC update failed.\n";
        return;
    }

    std::cout << "[UpdateManager] Starting MCU update...\n";
    if (!mcuUpdater_.Update(mcuPackages,updateStatus_,totalPackageCount))
    {
        std::cerr << "[UpdateManager] MCU update failed.\n";
        return;
    }

    std::cout << "[UpdateManager] Update process completed successfully.\n";
}

