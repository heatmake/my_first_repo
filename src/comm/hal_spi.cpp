/**
 * @copyright ©Chongqing SERES FengHuangZhiChuang New Energy Vehicle Design institute Co.,Ltd 2025-2026. ALL rights
 * reserved.
 * @file hal_spi.cpp
 * @brief  SPI硬件操作抽象层API
 * @author Fuwei.Zhao
 * @version 1.0
 */
#include "hal/hal_spi.h"
#include "log/log.h"
#include "file_manager_api.h"

HalSpi::HalSpi() :
    spiFd_(-1), devName_("SOC_SPI2_PATH"), spiSpeed_(S_10M), spiFrameDataLen_(SPI2_FRAME_DATA_LEN), spiDelay_(0),
    spiMode_(SPI_MODE3), spiBitsType_(SPI_8BITS)
{}

HalSpi::~HalSpi()
{
    SpiReturnType ret = SpiClose();
    if (ret != SPI_OK) {
        RERROR << "SPI init failed, error code = " << ret << " .";
        // throw std::runtime_error("SPI init failed.");
    }
    RINFO << "HalSpi close success .";
}

SpiReturnType HalSpi::SpiInit(uint8_t spiId)
{
    RINFO << "spiId = " << spiId << " .";
    ReadSpiConfigFromFile(spiId);
    // 打开设备
    spiFd_ = open(devName_.c_str(), O_RDWR);
    if (spiFd_ < 0) {
        RERROR << "HalSpi open failed .";
        return SPI_OPEN_FAILED;
    }
    // 设置SPI模式参数
    if (ioctl(spiFd_, SPI_IOC_WR_MODE, &spiMode_) < 0) {
        RERROR << "HalSpi mode set failed .";
        close(spiFd_);
        spiFd_ = -1;
        return SPI_MODE_SETWR_ERR;
    }

    // 设置SPI速度参数
    if (ioctl(spiFd_, SPI_IOC_WR_MAX_SPEED_HZ, &spiSpeed_) < 0) {
        RERROR << "HalSpi speed set failed .";
        close(spiFd_);
        spiFd_ = -1;
        return SPI_SPEED_SETWR_ERR;
    }

    // 设置SPI位宽参数
    if (ioctl(spiFd_, SPI_IOC_WR_BITS_PER_WORD, &spiBitsType_) < 0) {
        RERROR << "HalSpi bits per word set failed .";
        close(spiFd_);
        spiFd_ = -1;
        return SPI_BITS_SETWR_ERR;
    }
    RINFO << "HalSpi init success .";
    return SPI_OK;
}

SpiReturnType HalSpi::SpiTransfer(const uint8_t* txBuf, uint8_t* rxBuf)
{
    if (spiFd_ < 0) {
        return SPI_NOT_INITIALIZED;
    }
    if (txBuf == nullptr || rxBuf == nullptr) {
        return SPI_XFER_INVALID_ARG;
    }
    struct spi_ioc_transfer tr;
    memset(&tr, 0x00, sizeof(tr));
    tr.tx_buf = (unsigned long)txBuf; // 发送缓冲区地址
    tr.rx_buf = (unsigned long)rxBuf; // 接收缓冲区地址
    tr.len = spiFrameDataLen_;        // 传输长度
    tr.speed_hz = spiSpeed_;          // 传输速度
    tr.bits_per_word = spiBitsType_;  // 传输位宽
    tr.delay_usecs = spiDelay_;       // 传输一次完成后，等待延时时间

    if (ioctl(spiFd_, SPI_IOC_MESSAGE(1), &tr) <= 0) {
        RERROR << "HalSpi transfer data failed .";
        return SPI_XFER_MSG_ERR;
    }
    return SPI_OK;
}

SpiReturnType HalSpi::SpiClose()
{
    if (spiFd_ >= 0) {
        close(spiFd_);
        spiFd_ = -1;
        return SPI_OK;
    }
    return SPI_NOT_INITIALIZED;
}
