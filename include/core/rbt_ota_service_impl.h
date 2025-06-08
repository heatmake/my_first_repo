#pragma once
#include <cstring>
#include <nlohmann/json.hpp>
#include "hal_spi.h"
#include "data_ctrl.h"
#include "ota_util.h"
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include "json_file_writer.h"

kMaxChunkSize = 166;
#define MAX_CHUNK_SIZE 166
#define RETRY_INTERVAL 50
#define RETRY_LIMIT 6

// 整机软件信息：版本号
typedef struct
{
    char version[64];
} RobotInfo_s;

// OTA 模式设置
typedef enum
{
    OTA_MODE_CANCEL = 0, // 取消 OTA 模式
    OTA_MODE_SET = 1     // 设置 OTA 模式
} otaMode_e;

// 升级前获取 CCU 信息
typedef struct
{
    char ecuHsn[64];       // ECU 序列号
    char supplierCode[64]; // ECU 供应商编码
    char ecuSVer[64];      // ECU 软件版本
    char ecuHVer[64];      // ECU 硬件版本
} VersionInfo_s;

// 升级进度状态
typedef struct
{
    int32_t stage;   // 0 成功，1 升级中，2 失败
    int32_t process; // 进度百分比：0-100%
} UpdateSta_s;

// 激活状态
typedef struct
{
    int32_t result; // 0 成功，1 激活中，2 失败
} ActiveSta_s;


enum FlashMode {
    NORMAL = 0, // 普通刷写模式
    FORCE  = 1  // 强制刷写模式
};

struct Version {
    int major;
    int minor;
    int patch;
    int build;

    bool operator>(const Version& other) const {
        if (major != other.major) return major > other.major;
        if (minor != other.minor) return minor > other.minor;
        if (patch != other.patch) return patch > other.patch;
        return build > other.build;
    }

    bool operator==(const Version& other) const {
        return major == other.major && minor == other.minor &&
               patch == other.patch && build == other.build;
    }

    std::string ToString() const {
        return std::to_string(major) + "." +
               std::to_string(minor) + "." +
               std::to_string(patch) + "." +
               std::to_string(build);
    }
};


class RbtOtaServiceImpl
{
public:
    static RbtOtaServiceImpl &Instance();

    bool SetRobotInfo(const RobotInfo_s *info);
    bool SetOtaMode(otaMode_e mode);
    bool StartUpdate(const char *path);
    bool GetUpdateStatus(UpdateSta_s *status);
    bool PrivateProtocolEncap(DataPlaneType_e dataType, const uint8_t *data, uint32_t len);
    uint16_t CalculatePackageCount(const std::string &filePath);
    uint32_t CalculateTotalLength(const std::string &filePath);
    std::vector<uint8_t> ReadFileToBuffer(const std::string& filePath)
    bool PerformMcuFlashDriver(const std::string &driverPath);
    bool PerformMcuAppFlash(const std::string &appPath);
    bool SetActive();
    bool GetActive(ActiveSta_s *status);

private:
    RbtOtaServiceImpl();
    void RunOtaUpdateThread(const std::string &path);
    bool otaModeFlag_;                   // OTA 模式标志位
    std::shared_ptr<DataCtrl> dataCtrl_; // 数据控制类实例
    uint8_t kRetryLimit = 6;
    FlashMode flashMode_ ;
};
