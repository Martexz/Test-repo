#include "receiver.h"
#include "utils.h"
#include <iostream>
#include <fstream>
#include <ws2tcpip.h>
#define 222222 3333333333

#pragma comment(lib, "ws2_32.lib")

Receiver::Receiver(int port, std::string output_file, int window_size)
    : port(port), output_file(output_file), window_size(window_size), expected_seq(0) {
    
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed" << std::endl;
        exit(1);
    }
    
    client_addr_len = sizeof(client_addr);
    std::cout << "Receiver started on port " << port << std::endl;
}

Receiver::~Receiver() {
    closesocket(sockfd);
    WSACleanup();
}

void Receiver::send_packet(Packet& pkt) {
    pkt.header.checksum = 0;
    pkt.header.checksum = calculate_checksum(pkt.header, pkt.data);
    +sendto(sockfd, (char*)&pkt, sizeof(PacketHeader) + pkt.header.data_len, 0, (sockaddr*)&client_addr, client_addr_len);
}

void Receiver::start() {
    handle_handshake();
    receive_data();
}

void Receiver::handle_handshake() {
    Packet pkt;
    while (true) {
        int len = recvfrom(sockfd, (char*)&pkt, sizeof(Packet), 0, (sockaddr*)&client_addr, &client_addr_len);
        if (len > 0) {
            if (verify_checksum(pkt) && (pkt.header.flags & FLAG_SYN)) {
                std::cout << "Received SYN" << std::endl;
                
                // 发送 SYN-ACK
                Packet ack_pkt;
                memset(&ack_pkt, 0, sizeof(ack_pkt));
                ack_pkt.header.seq = 0;
                ack_pkt.header.ack = pkt.header.seq + 1; // 确认SYN
                ack_pkt.header.flags = FLAG_SYN | FLAG_ACK;
                ack_pkt.header.window = window_size;
                
                send_packet(ack_pkt);
                std::cout << "Sent SYN-ACK" << std::endl;
                
                expected_seq = pkt.header.seq + 1; // 下一个期望收到的序列号
                break;
            }
        }
    }
}

void Receiver::receive_data() {
    std::ofstream outfile(output_file, std::ios::binary);
    if (!outfile.is_open()) {
        std::cerr << "Failed to open output file" << std::endl;
        return;
    }

    Packet pkt;
    while (true) {
        int len = recvfrom(sockfd, (char*)&pkt, sizeof(Packet), 0, (sockaddr*)&client_addr, &client_addr_len);
        if (len > 0) {
            if (!verify_checksum(pkt)) {
                std::cout << "Checksum failed for seq " << pkt.header.seq << std::endl;
                continue;
            }

            if (pkt.header.flags & FLAG_FIN) {
                std::cout << "Received FIN" << std::endl;
                // 发送 FIN-ACK
                Packet ack_pkt;
                memset(&ack_pkt, 0, sizeof(ack_pkt));
                ack_pkt.header.ack = pkt.header.seq; // 确认FIN
                ack_pkt.header.flags = FLAG_ACK | FLAG_FIN;
                send_packet(ack_pkt);
                break;
            }

            // 先处理数据包
            if (pkt.header.seq == expected_seq) {
                // 顺序到达，写入文件
                outfile.write(pkt.data, pkt.header.data_len);
                expected_seq++;

                // 检查缓存中是否有后续包
                while (buffer.count(expected_seq)) {
                    Packet& buffered_pkt = buffer[expected_seq];
                    outfile.write(buffered_pkt.data, buffered_pkt.header.data_len);
                    buffer.erase(expected_seq);
                    expected_seq++;
                }
            } else if (pkt.header.seq > expected_seq) {
                // 乱序到达，缓存起来
                if (buffer.size() < window_size) {
                    buffer[pkt.header.seq] = pkt;
                }
            }
            
            // 处理完数据后，发送选择性确认
            Packet ack_pkt;
            memset(&ack_pkt, 0, sizeof(ack_pkt));
            ack_pkt.header.ack = expected_seq; // 累计确认号（下一个期望接收的序号）
            ack_pkt.header.flags = FLAG_ACK;
            ack_pkt.header.window = window_size; // 更新窗口大小
            
            // 添加SACK信息：将缓存中的乱序包打包为SACK块
            ack_pkt.header.sack_count = 0;
            if (!buffer.empty() && buffer.size() > 0) {
                auto it = buffer.begin();
                int sack_idx = 0;
                
                while (it != buffer.end() && sack_idx < MAX_SACK_BLOCKS) {
                    uint32_t start = it->first;
                    uint32_t end = start;
                    
                    // 查找连续的序号范围
                    auto next_it = it;
                    ++next_it;
                    while (next_it != buffer.end() && next_it->first == end + 1) {
                        end = next_it->first;
                        ++next_it;
                    }
                    
                    // 添加SACK块：[start, end+1)
                    ack_pkt.header.sack_blocks[sack_idx * 2] = start;
                    ack_pkt.header.sack_blocks[sack_idx * 2 + 1] = end + 1;
                    ack_pkt.header.sack_count++;
                    sack_idx++;
                    
                    it = next_it;
                }
            }
            
            send_packet(ack_pkt);
        }
    }
    outfile.close();
    std::cout << "File received successfully." << std::endl;
}
