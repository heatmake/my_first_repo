#pragma once

#include <string>

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

enum class OtaStatus_e : int8_t
{
    Success = 0,
    Failed  = -1
};
