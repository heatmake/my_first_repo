/**
 * @copyright ©Chongqing SERES Phoenix Intelligent Creation Technology Co., Ltd.
 * @file data_ctrl.cpp
 * @brief 设备接入层数据平面层控制实现文件
 * @author xlwu
 * @version 1.1（优化版）
 */
#include <iostream>
#include <array>
#include "data_ctrl.h"
#include "hal_spi.h"
#include "log.h"
#include "checksum.h"

DataCtrl::DataCtrl()
    : sendLen_(0)
{
    spi_ = std::make_shared<HalSpi>();
    std::cout << "DataCtrl init success " << std::endl;
}

// 常量定义
static constexpr size_t kHeaderLen = sizeof(DataPro_t);
static constexpr size_t kChecksumLen = CHECK_SUM_LEN;

bool DataCtrl::PrivateProtocolEncap(DataPlaneType_e dataType, const uint8_t* data, uint32_t len)
{
    if (data == nullptr || len == 0) {
        RERROR << "Invalid input data for protocol encapsulation.";
        return false;
    }

    if (sendLen_ != 0) {
        RERROR << "Send buffer not cleared. Previous data exists.";
        return false;
    }

    if ((kHeaderLen + len + kChecksumLen) > COMM_DATA_LEN_MAX) {
        RERROR << "Send buffer overflow. data-len = " << len;
        return false;
    }
    // 在构造新数据之前，清理整个缓冲区
    memset(sendBuff_.get(), 0, COMM_DATA_LEN_MAX);
    DataPro_t* dataPtr = reinterpret_cast<DataPro_t*>(sendBuff_.data());
    dataPtr->sof = START_OF_FRAME;
    dataPtr->type = static_cast<uint8_t>(dataType);
    dataPtr->length = htons(len);

    uint8_t* value = reinterpret_cast<uint8_t*>(sendBuff_.data() + kHeaderLen);
    memcpy(value, data, len);
    sendLen_ = kHeaderLen + len;

    RDEBUG << "Private protocol encapsulation success, data-type: " << static_cast<uint8_t>(dataType)
           << " data-len: " << len;
    return true;
}

bool DataCtrl::SendData(uint8_t* outBuf, size_t outBufSize)
{
    if (sendLen_ == 0 || outBuf == nullptr || outBufSize < sendLen_ + kChecksumLen) {
        RERROR << "Invalid send parameters. sendLen = " << sendLen_;
        return false;
    }

    // 计算 CRC 校验并追加
    uint16_t checkSum = Crc16CcittFalse(sendBuff_.data(), sendLen_);
    sendBuff_[sendLen_] = (checkSum >> 8) & 0xFF;
    sendBuff_[sendLen_ + 1] = checkSum & 0xFF;
    sendLen_ += kChecksumLen;

    // 传输数据
    SpiReturnType ret = spi_->SpiTransfer(sendBuff_.data(), outBuf);

    if (ret != SPI_OK) {
        RERROR << "SPI transfer failed!";
    }

    sendLen_ = 0;  // 清空缓存状态
    return (ret == SPI_OK);
}

bool DataCtrl::SendAndRecv(DataPlaneType_e type, const void* data, uint32_t len,
                           const uint8_t*& payloadPtr, uint16_t& payloadLen)
{
    static uint8_t recvBuf[COMM_DATA_LEN_MAX] = {0};
    payloadPtr = nullptr;
    payloadLen = 0;

    // 1. 封装数据帧
    if (!PrivateProtocolEncap(type, static_cast<const uint8_t*>(data), len)) {
        RERROR << "[SendAndRecv] Protocol encapsulation failed.";
        return false;
    }

    // 2. 发送并接收
    if (!SendData(recvBuf, sizeof(recvBuf))) {
        RERROR << "[SendAndRecv] SPI transmission failed.";
        return false;
    }

    // 3. 校验帧头
    DataPro_t* header = reinterpret_cast<DataPro_t*>(recvBuf);
    if (header->sof != START_OF_FRAME) {
        RERROR << "[SendAndRecv] Invalid start of frame.";
        return false;
    }

    // 4. 提取长度
    uint16_t lenNet = header->length;
    payloadLen = ntohs(lenNet);

    if (payloadLen == 0) {
        RDEBUG << "[SendAndRecv] Response has no payload.";
        return true;  // 返回成功，但 payload 长度为 0
    }

    // 5. 边界检查
    if ((kHeaderLen + payloadLen + kChecksumLen) > COMM_DATA_LEN_MAX) {
        RERROR << "[SendAndRecv] Payload length out of bounds: " << payloadLen;
        return false;
    }

    payloadPtr = recvBuf + kHeaderLen;
    return true;
}

