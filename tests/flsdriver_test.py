import socket
import time
import os
import struct
import hashlib

def crc16_ccitt(data: bytes, init=0xFFFF, poly=0x1021, refin=False, refout=False, xorout=0x0000) -> int:
    crc = init
    for byte in data:
        if refin:
            byte = (byte << 8) & 0xFF00 | (byte >> 8)  # 反转字节内比特位（可选）
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ poly
            else:
                crc <<= 1
            crc &= 0xFFFF  # 保持 16 位
    if refout:
        crc = (crc >> 8) | (crc << 8)  # 反转整个 CRC 的比特位（可选）
    crc ^= xorout
    return crc & 0xFFFF

def send_bin_with_index(file_path, dest_ip, dest_port, max_chunk_size=166, interval=0.005):
    """
    通过 UDP 发送带索引和校验的分段 BIN 文件
    
    :param file_path:       BIN 文件路径
    :param dest_ip:         目标 IP 地址（例如 "10.0.0.33"）
    :param dest_port:       目标端口（例如 5001）
    :param max_chunk_size:  数据段最大长度（默认 166 字节）
    :param interval:        发包间隔（默认 5ms）
    """
    
    # 创建 UDP 套接字
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dest_addr = (dest_ip, dest_port)
    packet_count = 0
    md5_hash = hashlib.md5()
    current_index = 0  # 当前包索引（两字节，范围 0~65535）

    try:
        with open(file_path, 'rb') as f:
            total_size = os.path.getsize(file_path)
            
            # 分段发送文件内容
            while True:
                data = f.read(max_chunk_size)
                if not data:
                    break  # 文件读取完成
                
                # 更新 MD5 哈希
                md5_hash.update(data)
                
                # 构造数据包：头 + 长度 + 索引 + 数据 + CRC
                header = struct.pack('>H', 0x5B01)            # 固定头 0x5566
                data_length = struct.pack('>H', (len(data)+2))    # 数据长度（2字节大端）
                index_bytes = struct.pack('>H', current_index) # 包索引（2字节大端）
                payload = header + data_length + index_bytes + data
                crc_value = crc16_ccitt(payload, init=0xFFFF, refin=False, refout=False, xorout=0x0000)
                crc_bytes = crc_value.to_bytes(2, byteorder='big')
                packet = payload + crc_bytes
                
                # 发送数据包
                sock.sendto(packet, dest_addr)
                packet_count += 1
                current_index = (current_index + 1) % 65536   # 处理索引溢出
                
                # 打印进度
                progress = f.tell() / total_size * 100
                print(f"\rSent Packet {packet_count} [Index {current_index-1}] [{progress:.2f}%]", end='')
                
                time.sleep(interval)
            
            # 文件发送完成后，发送 MD5 校验包（独立格式）
            # ---------------------------------------------------------------------
            # 计算 MD5 哈希值（16 字节）
            md5_digest = md5_hash.digest()
            
            # 构造 MD5 校验包：头（0x6677） + 长度（16）+ MD5 + CRC
            header = struct.pack('>H', 0x5B02)
            data_length = struct.pack('>H', 16)              # MD5 长度固定 16 字节
            payload = header + data_length + md5_digest
            crc_value = crc16_ccitt(payload, init=0xFFFF, refin=False, refout=False, xorout=0x0000)
            crc_bytes = crc_value.to_bytes(2, byteorder='big')
            md5_packet = payload + crc_bytes
            
            # 发送 MD5 包
            sock.sendto(md5_packet, dest_addr)
            print(f"\nSent MD5 Packet: {md5_digest.hex()}")
            
    except FileNotFoundError:
        print(f"Error: File {file_path} not found!")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        sock.close()
        print("Transfer completed.")

if __name__ == "__main__":
    BIN_FILE = "FlashDriver_U2A8_V250429.bin"  # 替换为你的 BIN 文件路径
    TARGET_IP = "10.0.0.33"
    TARGET_PORT = 5001

    send_bin_with_index(BIN_FILE, TARGET_IP, TARGET_PORT)