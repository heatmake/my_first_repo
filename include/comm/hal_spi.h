/**
 * @copyright ©Chongqing SERES FengHuangZhiChuang New Energy Vehicle Design institute Co.,Ltd 2025-2026. ALL rights
 * reserved.
 * @file hal_spi.h
 * @brief  SPI硬件操作抽象层API
 * @author Fuwei.Zhao
 * @version 1.0
 */
#ifndef __HAL_SPI_H__
#define __HAL_SPI_H__

#include <iostream>
#include <vector>
#include <cstring>
#include <cstdio>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>

#define SPI1_FRAME_DATA_LEN 200              // 最大帧数据长度
#define SPI2_FRAME_DATA_LEN 174              // 最大帧数据长度
#define SOC_SPI1_PATH       "/dev/spidev0.0" // spi1的设备文件路径
#define SOC_SPI2_PATH       "/dev/spidev1.0" // spi2的设备文件路径

// SPI工作模式定义
enum SpiMode_t {
    SPI_MODE0 = SPI_MODE_0, // CPOL=0, CPHA=0
    SPI_MODE1 = SPI_MODE_1, // CPOL=0, CPHA=1
    SPI_MODE2 = SPI_MODE_2, // CPOL=1, CPHA=0
    SPI_MODE3 = SPI_MODE_3  // CPOL=1, CPHA=1
};

// SPI传输位宽定义
enum SpiBitsType_t { SPI_8BITS = 8, SPI_16BITS = 16 };

// SPI工作速度定义
enum SpiSpeedType_t {
    S_960K = 960000,
    S_2M = 2000000,
    S_4M = 4000000,
    S_5M = 5000000,
    S_8M = 8000000,
    S_10M = 10000000
};

// SPI错误码定义
enum SpiReturnType {
    SPI_OK = 0,           // 操作成功
    SPI_OPEN_FAILED,      // SPI设备打开失败
    SPI_MODE_SETWR_ERR,   // SPI工作模式设置失败
    SPI_SPEED_SETWR_ERR,  // SPI工作速度设置失败
    SPI_BITS_SETWR_ERR,   // SPI传输位宽设置失败
    SPI_XFER_INVALID_ARG, // 无效传输参数,NULL指针等
    SPI_XFER_MSG_ERR,     // SPI消息传输失败
    SPI_NOT_INITIALIZED,  // SPI设备未初始化
    SPI_UNKNOWN_ERR       // 其他错误,包括通道函数使用错误等等
};

class HalSpi {
public:
    HalSpi();
    ~HalSpi();

    SpiReturnType SpiInit(uint8_t spiId);
    SpiReturnType SpiTransfer(const uint8_t* txBuf, uint8_t* rxBuf);
    SpiReturnType SpiClose();

private:
    int spiFd_;                 // SPI 文件描述符
    std::string devName_ = "";  // 设备名称
    SpiSpeedType_t spiSpeed_;   // SPI 传输的速度，单位为赫兹
    uint16_t spiFrameDataLen_;  // 发送和接收的数据长度
    uint16_t spiDelay_;         // 传输后延时的微秒数,单位us
    SpiMode_t spiMode_;         // SPI工作模式
    SpiBitsType_t spiBitsType_; // SPI传输位宽
};

#endif /* __HAL_SPI_H__ */
