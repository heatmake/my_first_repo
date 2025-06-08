#pragma once
#include "ota_types.h"
#include "json_helper.h"

class PrefabricationManager {
public:
    bool SetRobotInfo(const RobotInfo_s *info);
    bool SetOtaMode(otaMode_e mode);
};