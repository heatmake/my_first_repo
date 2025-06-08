/**
 * @copyright ©Chongqing SERES Phoenix Intelligent Creation Technology Co., Ltd. All rights reserved.
 * @file checksum.h
 * @brief  计算校验和的头文件（计算方式 CRC-16-CCITT）
 * @author xlwu
 * @version 1.0
*/
#pragma once

#include <stdint.h>

uint16_t Crc16CcittUpdate(uint16_t crc, const uint8_t* data, size_t len);

uint16_t Crc16CcittFalse(uint8_t* pData, uint16_t len);