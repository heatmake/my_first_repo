#include "soc_updater.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <regex>
#include <vector>
#include <cstdlib>
#include "otalog.h"

namespace fs = std::filesystem;

bool SocUpdater::Update(const std::vector<fs::path>& socPackages, std::function<void(int)> progressCallback)
{
    OTALOG(OlmInstall, OllInfo, "[SOC]start flash soc packages\n");
    bool result = true;
    bool all_success = true;

    for (const auto& path : socPackages) {
        const std::string filename = path.filename().string();
        bool success = true;
        OTALOG(OlmInstall, OllInfo, "[SOC]package name: %s\n", filename.c_str());
        if (std::regex_match(filename, std::regex(R"(app.*\.deb)"))) {
            success = FlashDeb("app", path.string());
        } else if (std::regex_match(filename, std::regex(R"(robot.*\.deb)"))) {
            success = FlashDeb("robot", path.string());
        } else if (std::regex_match(filename, std::regex(R"(navi.*\.deb)"))) {
            success = FlashDeb("navi", path.string());
        } else if (std::regex_match(filename, std::regex(R"(mc.*\.deb)"))) {
            success = FlashDeb("mc", path.string());
        } else if (std::regex_match(filename, std::regex(R"(rte.*\.deb)"))) {
            success = FlashDeb("rte", path.string());
        } else if (std::regex_match(filename, std::regex(R"(SOC.*\.tar\.gz)"))) {
            success = FlashSocSystem(path.string());
        }

        all_success &= success;

        // 通知外部整体进度 +1 包
        if (success) {
            if (progressCallback) {
                progressCallback(1);
            }
        }
    }

    if (!all_success) {
        OTALOG(OlmInstall, OllError, "[SOC]have package flash failed\n");
        RestoreConfigs();
        result = false;
    } else {
        OTALOG(OlmInstall, OllInfo, "[SOC]SOC package upgrade complete\n");
        bool socFlag = true;
    }
    return result;
}

void SocUpdater::RestoreConfigs()
{
    OTALOG(OlmInstall, OllInfo, "[SOC]start restore config\n");

    // 删除原始 /opt/seres/ 目录下的对应目录
    const std::string clean_opt =
        "rm -rf /opt/seres/start /opt/seres/packages /opt/seres/middleware /opt/seres/calibration /opt/seres/model";

    // 删除原始 /data/ 目录下的对应目录
    const std::string clean_data = "rm -rf /data/config /data/db";

    // 解压备份到原路径
    const std::string restore_opt = "tar -xzf /opt/seres/backup/seres_backup.tar.gz -C /opt/seres";
    const std::string restore_data = "tar -xzf /data/backup/data_backup.tar.gz -C /data";

    int ret1 = system(clean_opt.c_str());
    int ret2 = system(clean_data.c_str());
    int ret3 = system(restore_opt.c_str());
    int ret4 = system(restore_data.c_str());

    if (ret3 == 0 && ret4 == 0) {
        OTALOG(OlmInstall, OllInfo, "[SOC]config restore success\n");
    } else {
        OTALOG(OlmInstall, OllError, "[SOC]config restore failed\n");
    }
}

bool SocUpdater::FlashDeb(const std::string& description, const std::string& filepath)
{
    bool result = false;
    OTALOG(OlmInstall, OllInfo, "[SOC]start flash %s: %s\n", description.c_str(), filepath.c_str());

    std::string cmd = "dpkg -i \"" + filepath + "\" > /dev/null"; // 仅隐藏stdout，保留stderr

    if (system(cmd.c_str()) == 0) {
        OTALOG(OlmInstall, OllInfo, "[SOC]%s flash success\n", description.c_str());
        result = true;
    } else {
        OTALOG(OlmInstall, OllError, "[SOC]%s flash failed\n", description.c_str());
    }

    return result;
}

bool SocUpdater::FlashSocSystem(const std::string& filepath)
{
    bool result = false;
    OTALOG(OlmInstall, OllInfo, "[SOC]flashing SOC system package: %s\n", filepath.c_str());

    std::string extractCmd = "tar xjpf /application/ota/ota_tools_R36.4.0_aarch64.tbz2 > /dev/null";
    std::string nvOtaCmd = "cd /application/ota/Linux_for_Tegra/tools/ota_tools/version_upgrade && "
                           "./nv_ota_start.sh \""
                           + filepath + "\" > /dev/null";

    bool execResult = (system(extractCmd.c_str()) == 0 && system(nvOtaCmd.c_str()) == 0);

    if (execResult) {
        OTALOG(OlmInstall, OllInfo, "[SOC]SOC system package flash success\n");
        result = true;
    } else {
        OTALOG(OlmInstall, OllError, "[SOC]SOC system package flash failed\n");
    }

    return result;
}
