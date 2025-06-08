#include "rbt_ota_interface.h"
#include "ota_service.h"


extern "C" {

OtaStatus_e OtaAgentSetRobotInfo(RobotInfo_s* info) {
    return OtaService::GetInstance().SetRobotInfo(info);
}

OtaStatus_e OtaAgentSetOtamode(otaMode_e mode) {
    return OtaService::GetInstance().SetOtaMode(mode);
}

OtaStatus_e OtaAgentSetStartUpdate(char* path) {
    return OtaService::GetInstance().StartUpdate(path);
}

OtaStatus_e OtaAgentGetUpdateStatus(UpdateSta_s* status) {
    return OtaService::GetInstance().GetUpdateStatus(status);
}

OtaStatus_e OtaAgentSetActive(void) {
    return OtaService::GetInstance().SetActive();
}

OtaStatus_e OtaAgentGetActivestatus(ActiveSta_s* status) {
    return OtaService::GetInstance().GetActive(status);
}

}
