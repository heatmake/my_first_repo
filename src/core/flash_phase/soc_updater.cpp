#include "soc_updater.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <regex>
#include <vector>
#include <cstdlib>

namespace fs = std::filesystem;

bool SocUpdater::Update(const std::vector<fs::path>& socPackages,UpdateSta_s &updateStatus,uint8_t totalFiles)
{
    updateStatus.stage = 1;
    updateStatus.process = 0;
    uint8_t processed = 0;

    bool result = true;
    bool all_success = true;

    for (const auto& path : socPackages)
    {
        const std::string filename = path.filename().string();
        bool success = true;

        if (std::regex_match(filename, std::regex(R"(app.*\.deb)")))
        {
            success = FlashDeb("应用软件包", path.string());
        }
        else if (std::regex_match(filename, std::regex(R"(robot.*\.deb)")))
        {
            success = FlashDeb("平台软件包", path.string());
        }
        else if (std::regex_match(filename, std::regex(R"(navi.*\.deb)")))
        {
            success = FlashDeb("导航软件包", path.string());
        }
        else if (std::regex_match(filename, std::regex(R"(mc.*\.deb)")))
        {
            success = FlashDeb("运控软件包", path.string());
        }
        else if (std::regex_match(filename, std::regex(R"(rte.*\.deb)")))
        {
            success = FlashDeb("底软软件包", path.string());
        }
        else if (std::regex_match(filename, std::regex(R"(SOC.*\.tar\.gz)")))
        {
            success = FlashSocSystem(path.string());
        }

        all_success &= success;
        ++updateStatus.processed;
        updateStatus.process = static_cast<int>((updateStatus.processed * 100.0) / totalFiles);
    }

    if (!all_success)
    {
        std::cerr << "[SOC] 有包刷写失败。\n";
        updateStatus.stage = 2;
        RestoreConfigs();
        result = false;
    }

    std::cout << "[SOC] SOC 软件包升级完成。\n";
    return result;
}

void SocUpdater::RestoreConfigs()
{
    std::system("rm -rf /opt/seres && mv /opt/seres.bak /opt/seres");
    std::system("rm -rf /data && mv /data.bak /data");
}

bool SocUpdater::FlashDeb(const std::string &description, const std::string &filepath)
{
    bool result = false;
    std::cout << "[SOC] 正在刷写 " << description << ": " << filepath << std::endl;
    std::string cmd = "dpkg -i \"" + filepath + "\"";
    
    if (system(cmd.c_str()) == 0)
    {
        std::cout << "  - " << description << " 升级成功\n";
        result = true;
    }

    std::cerr << "  - " << description << " 升级失败\n";
    return result;
}


bool SocUpdater::FlashSocSystem(const std::string &filepath)
{
    bool result = false;
    std::cout << "[SOC] 正在刷写 SOC 系统包: " << filepath << std::endl;

    std::string extractCmd = "tar xjpf /application/ota/ota_tools_R36.4.0_aarch64.tbz2";
    std::string nvOtaCmd = "/application/ota/Linux_for_Tegra/tools/ota_tools/version_upgrade/nv_ota_start.sh \"" + filepath + "\"";
    result = (system(extractCmd.c_str()) == 0 && system(nvOtaCmd.c_str()) == 0);

    if (result)
    {
        std::cout << "  - SOC 系统包刷写成功\n";
        result = true;
    }
    std::cerr << "  - SOC 系统包刷写失败\n";
    return result;
}


// 发送 MD5 校验请求，包含 16 字节原始 MD5 值
void SendMd5ChecksumRequest(const std::array<uint8_t, 16> &md5Bytes)
{
    dataCtrl_->PrivateProtocolEncap(DATA_TYPE_CHECK_DATA, md5Bytes.data(), md5Bytes.size());
    std::cout << "[OTA] Sent MD5 checksum for whole package.\n";
}