/**
 * @copyright ©Chongqing SERES Phoenix Intelligent Creation Technology Co., Ltd. All rights reserved.
 * @file DataCtrl.h
 * @brief  设备接入层数据平面Layer，负责与MCU进行数据交互，包括发送控制设备指令数据，控制指令封装类声明
 * @author xlwu
 * @version 1.0
 */
#ifndef __DATACTRL_H__
#define __DATACTRL_H__
#include <memory>
#include "hal_spi.h"

#define START_OF_FRAME 0x5A
#define CHECK_SUM_LEN     2
#define COMM_DATA_LEN_MAX 1472 // 最大发送数据长度

enum DataPlaneType_e : uint8_t {
    DATA_TYPE_NONE = 0X00, // 非私有协议时使用
    // 主动发起协议类型（本地主动发起数据）
    //预编程检测
    DATA_TYPE_PRE_PROGRAM_CHECK = 0x01,
    //请求下载
    DATA_TYPE_DOWNLOAD_REQUEST = 0x03,
    //下载数据
    DATA_TYPE_DOWNLOAD_DATA = 0x05,
    //校验数据
    DATA_TYPE_CHECK_DATA = 0x07,
    //复位
    DATA_TYPE_RESET = 0x09,
    //擦除APP
    DATA_TYPE_ERASE_APP = 0x0B,
    //刷写完成
    DATA_TYPE_FLASH_FINISH = 0x0D
};

typedef struct DataProtocol {
    uint8_t sof;     // START_OF_FRAME
    uint8_t type;    // 协议类型
    uint16_t length; // 协议长度
    DataProtocol()
    {
        sof = START_OF_FRAME;
        type = 0;
        length = 0;
    }
} DataPro_t;


class DataCtrl
{
public:
    DataCtrl();
    bool PrivateProtocolEncap(DataPlaneType_e dataType, const uint8_t* data, uint32_t len);
    bool SendData(uint8_t* outBuf, size_t outBufSize);
    bool DataCtrl::SendAndRecv(DataPlaneType_e type, const void* data, uint32_t len,
                           const uint8_t*& payloadPtr, uint16_t& payloadLen);

private:
    std::shared_ptr<HalSpi> spi_;
    std::array<uint8_t, COMM_DATA_LEN_MAX> sendBuff_;  // 发送缓冲区
    size_t sendLen_;  // 当前缓冲数据长度
};
#endif /* __DATACTRL_H__ */
