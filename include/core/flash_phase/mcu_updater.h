#pragma once
#include <string>
#include <memory>
#include "data_ctrl.h"
#include "json_file_writer.h"
#include "md5.h"
#include "ota_types.h"


#define MAX_CHUNK_SIZE 166
enum class McuFlashMode {
    NORMAL,
    FORCED
};

enum class DownloadRequestType
{
    FLASH_DRIVER = 0x01
    APP = 0x02
};


class McuUpdater {
public:
    McuUpdater();
    // OtaStatus_e Init(const std::string &flash_diver_path, const std::string &app_path);
    bool Update(const std::vector<fs::path>& socPackages,UpdateSta_s &updateStatus,uint8_t totalFiles);

private:
    std::string ExtractVersionFromFilename(const std::string& filename);
    OtaStatus_e IsVersionNewer(const std::string& socVersion, const std::string& mcuVersion);
    OtaStatus_e PreUpdate(const std::string &appPath);
    uint16_t CalculatePackageCount(const std::string &filePath)
    OtaStatus_e SendDownloadRequest(DownloadRequestType requestType, uint16_t totalPackages, uint32_t totalLength);
    std::vector<uint8_t> ReadFileToBuffer(const std::string &filePath);
    OtaStatus_e FlashDriverUpdate(const std::string &flashDiverPath);
    OtaStatus_e AppUpdate(const std::string &appPath );
    OtaStatus_e Flashing(const std::string &flashDiverPath, const std::string &appPath);
    OtaStatus_e Reboot();

    bool TryUpdateOnce(std::string &flashDiverPath, std::string &appPath, UpdateSta_s &updateStatus, uint8_t totalFiles);
    const int kRetryIntervalMs = 50;
    const int kMaxRetries = 6;
    McuFlashMode currentMode_ ;
    std::shared_ptr<DataCtrl> dataCtrl_;
    std::string flashDriverName_;
    std::string appFileName_;
    
};
