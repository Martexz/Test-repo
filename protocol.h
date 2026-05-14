#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <cstring>

// 数据包标志位
const uint8_t FLAG_SYN = 0x01;
const uint8_t FLAG_ACK = 0x02;
const uint8_t FLAG_FIN = 0x04;

// 协议常量
const int MSS = 1024; // 最大段大小
const int RTO_INITIAL = 1000; // 初始超时重传时间
const int MAX_SACK_BLOCKS = 4; // SACK块的最大数量

#pragma pack(push, 1)
struct PacketHeader {
    uint32_t seq;       // 序列号
    uint32_t ack;       // 确认号
    uint16_t window;    // 接收窗口大小
    uint16_t checksum;  // 校验和
    uint16_t data_len;  // 数据长度
    uint8_t flags;      // 控制标志
    uint8_t sack_count; // SACK块数量
    uint32_t sack_blocks[MAX_SACK_BLOCKS * 2]; // SACK块
};
#pragma pack(pop)

struct Packet {
    PacketHeader header;
    char data[MSS];
};

#endif // PROTOCOL_H
