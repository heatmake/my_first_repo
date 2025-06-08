/**
 * @copyright ©Chongqing SERES Phoenix Intelligent Creation Technology Co., Ltd. All rights reserved.
 * @file checksum.cpp
 * @brief   CRC-16-CCITT 校验和计算
 * @author xlwu
 * @version 1.0
 */

#include <cstdint>
#include <iostream>
#include "checksum.h"
// CRC-16-CCITT 预定义多项式
#define CRC16_POLYNOMIAL  0x1021 // x^16 + x^12 + x^5 + 1
#define DATA_HSB_TRAVERSE 7
#define CRC_HSB_FIRST     15
#define ONE_BYTE          8

//
/*********************************************************************
 * @fn      Crc16CcittUpdate
 * @brief:  追加方式计算 CRC-16-CCITT
 * @param:  crc - 校验和
 *          data - 数据
 *          len - 数据长度
 * @return: 校验和
 **********************************************************************/
uint16_t Crc16CcittUpdate(uint16_t crc, const uint8_t* data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        crc ^= (data[i] << 8); // 先 XOR 高 8 位
        for (uint8_t j = 0; j < 8; ++j) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ CRC16_POLYNOMIAL;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/*********************************************************************
 * @function: Crc16CcittFalse
 * @brief: 基于ccitt-flase参数模型计算数据校验和
 * @param:  pData - 待校验数据
 *          len - 待校验数据长度
 * @return: uint16_t - 2字节校验和
 **********************************************************************/
uint16_t Crc16CcittFalse(uint8_t* pData, uint16_t len)
{
    uint16_t crc = 0;
    if (pData != NULL && len != 0) {
        crc = 0xffff;
        for (int index = 0; index < len; index++) {
            uint8_t b = pData[index];
            for (int i = 0; i < ONE_BYTE; i++) {
                int bit = ((b >> (DATA_HSB_TRAVERSE - i) & 1) == 1);
                int c15 = ((crc >> CRC_HSB_FIRST & 1) == 1);
                crc <<= 1;
                if (c15 ^ bit) {
                    crc ^= CRC16_POLYNOMIAL;
                }
            }
        }
        crc &= 0xffff;
    }
    return crc;
}

// int main() {
//     // 示例数据
//     uint8_t data1[] = {0x12, 0x34, 0x56};
//     uint8_t data2[] = {0x78, 0x9A};

//     // 第一次计算
//     uint16_t crc = 0xFFFF;  // CCITT 初始化值
//     crc = Crc16CcittUpdate(crc, data1, sizeof(data1));

//     // 追加计算
//     crc = Crc16CcittUpdate(crc, data2, sizeof(data2));

//     // 输出最终 CRC 值
//     std::cout << "Final CRC: 0x" << std::hex << crc << std::endl;

//     return 0;
// }
