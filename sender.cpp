#include "sender.h"
#include "utils.h"
#include <iostream>
#include <fstream>
#include <ws2tcpip.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "ws2_32.lib")

Sender::Sender(std::string target_ip, int target_port, int local_port, std::string input_file, int window_size)
    : target_ip(target_ip), target_port(target_port), local_port(local_port), input_file(input_file), window_size(window_size) {
    
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    
    // 允许端口重用，避免快速重启时绑定失败
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    // 绑定到固定的本地端口
    sockaddr_in local_addr;
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(local_port);

    if (bind(sockfd, (sockaddr*)&local_addr, sizeof(local_addr)) == SOCKET_ERROR) {
        std::cerr << "Sender bind failed on port " << local_port << std::endl;
        exit(1);
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(target_port);
    inet_pton(AF_INET, target_ip.c_str(), &server_addr.sin_addr);

    cwnd = MSS;
    ssthresh = 64 * MSS;
    dup_acks = 0;
    base = 0;
    next_seq_num = 0;
}

Sender::~Sender() {
    closesocket(sockfd);
    WSACleanup();
}

void Sender::load_file() {
    std::ifstream infile(input_file, std::ios::binary);
    if (!infile.is_open()) {
        std::cerr << "Failed to open input file" << std::endl;
        exit(1);
    }

    // 读取文件并创建数据包，序列号从1开始
    uint32_t seq = 1;
    while (!infile.eof()) {
        Packet pkt;
        memset(&pkt, 0, sizeof(pkt));
        infile.read(pkt.data, MSS);
        pkt.header.data_len = (uint16_t)infile.gcount();
        if (pkt.header.data_len == 0) break;
        
        pkt.header.seq = seq++;
        pkt.header.flags = 0; // 数据包
        packets.push_back(pkt);
    }
    
    acked.resize(packets.size(), false);
    send_times.resize(packets.size(), 0.0);
    std::cout << "Loaded " << packets.size() << " packets." << std::endl;
}

void Sender::send_packet_by_index(int index) {
    if (index >= packets.size()) return;
    
    Packet& pkt = packets[index];
    pkt.header.checksum = 0;
    pkt.header.checksum = calculate_checksum(pkt.header, pkt.data);
    
    sendto(sockfd, (char*)&pkt, sizeof(PacketHeader) + pkt.header.data_len, 0, (sockaddr*)&server_addr, sizeof(server_addr));
    send_times[index] = get_timestamp();
}

void Sender::handshake() {
    // 发送 SYN 建立连接
    Packet syn_pkt;
    memset(&syn_pkt, 0, sizeof(syn_pkt));
    syn_pkt.header.seq = 0;
    syn_pkt.header.flags = FLAG_SYN;
    syn_pkt.header.checksum = calculate_checksum(syn_pkt.header, syn_pkt.data);
    
    while (true) {
        sendto(sockfd, (char*)&syn_pkt, sizeof(PacketHeader), 0, (sockaddr*)&server_addr, sizeof(server_addr));
        std::cout << "Sent SYN" << std::endl;
        
        // 等待 SYN-ACK
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        
        timeval timeout;
        timeout.tv_sec = 1; // 超时1秒
        timeout.tv_usec = 0;
        
        int activity = select(0, &readfds, NULL, NULL, &timeout);
        
        if (activity > 0) {
            Packet ack_pkt;
            sockaddr_in from;
            int fromlen = sizeof(from);
            int len = recvfrom(sockfd, (char*)&ack_pkt, sizeof(Packet), 0, (sockaddr*)&from, &fromlen);
            
            if (len > 0 && verify_checksum(ack_pkt) && (ack_pkt.header.flags & (FLAG_SYN | FLAG_ACK))) {
                std::cout << "Received SYN-ACK" << std::endl;
                break;
            }
        } else {
            std::cout << "Timeout, resending SYN..." << std::endl;
        }
    }
}

void Sender::teardown() {
    // 发送 FIN 断开连接
    Packet fin_pkt;
    memset(&fin_pkt, 0, sizeof(fin_pkt));
    fin_pkt.header.seq = packets.size() + 1; // 下一个序列号
    fin_pkt.header.flags = FLAG_FIN;
    fin_pkt.header.checksum = calculate_checksum(fin_pkt.header, fin_pkt.data);
    
    int retries = 0;
    while (retries < 5) {
        sendto(sockfd, (char*)&fin_pkt, sizeof(PacketHeader), 0, (sockaddr*)&server_addr, sizeof(server_addr));
        std::cout << "Sent FIN" << std::endl;
        
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        
        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int activity = select(0, &readfds, NULL, NULL, &timeout);
        
        if (activity > 0) {
            Packet ack_pkt;
            sockaddr_in from;
            int fromlen = sizeof(from);
            int len = recvfrom(sockfd, (char*)&ack_pkt, sizeof(Packet), 0, (sockaddr*)&from, &fromlen);
            
            if (len > 0 && verify_checksum(ack_pkt) && (ack_pkt.header.flags & FLAG_ACK)) {
                std::cout << "Received FIN-ACK" << std::endl;
                break;
            }
        }
        retries++;
    }
}

void Sender::send_data() {
    double start_time = get_timestamp();
    
    while (base < packets.size()) {
        // 发送新数据包
        int effective_window = std::min((int)window_size, (int)(cwnd / MSS));
        if (effective_window < 1) effective_window = 1;
        
        while (next_seq_num < packets.size() && next_seq_num < base + effective_window) {
            if (!acked[next_seq_num]) {
                 send_packet_by_index(next_seq_num);
            }
            next_seq_num++;
        }
        
        // 等待 ACK 或超时
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        
        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 10000; // 10ms检查
        
        int activity = select(0, &readfds, NULL, NULL, &timeout);
        
        if (activity > 0) {
            Packet ack_pkt;
            sockaddr_in from;
            int fromlen = sizeof(from);
            int len = recvfrom(sockfd, (char*)&ack_pkt, sizeof(Packet), 0, (sockaddr*)&from, &fromlen);
            
            if (len > 0 && verify_checksum(ack_pkt) && (ack_pkt.header.flags & FLAG_ACK)) {
                uint32_t ack_seq = ack_pkt.header.ack;
                
                // 处理累计确认号：标记所有序号 < ack_seq 的包为已确认
                for (uint32_t seq = 1; seq < ack_seq && seq <= packets.size(); seq++) {
                    int idx = seq - 1; // 序号转索引
                    if (idx >= 0 && idx < packets.size()) {
                        acked[idx] = true;
                    }
                }
                
                // 处理SACK信息：标记已收到的乱序包
                for (int i = 0; i < ack_pkt.header.sack_count && i < MAX_SACK_BLOCKS; i++) {
                    uint32_t sack_start = ack_pkt.header.sack_blocks[i * 2];
                    uint32_t sack_end = ack_pkt.header.sack_blocks[i * 2 + 1];
                    
                    // 标记SACK范围内的包为已确认 [sack_start, sack_end)
                    for (uint32_t seq = sack_start; seq < sack_end && seq <= packets.size(); seq++) {
                        int idx = seq - 1; // 序号转索引
                        if (idx >= 0 && idx < packets.size() && !acked[idx]) {
                            acked[idx] = true;
                            std::cout << "SACK received for packet " << seq << std::endl;
                        }
                    }
                }
                
                // 处理窗口滑动
                int old_base = base;
                // 滑动窗口到第一个未确认的包
                while (base < packets.size() && acked[base]) {
                    base++;
                }
                
                // 判断是否有新的确认
                if (base > old_base) {
                    // 有新的累计确认，窗口向前滑动+.
                    // TCP Reno 拥塞控制
                    if (cwnd < ssthresh) {
                        cwnd += MSS * (base - old_base); // 慢启动
                    } else {
                        cwnd += MSS * MSS / cwnd * (base - old_base); // 拥塞避免
                    }
                    dup_acks = 0;
                } else {
                    // 没有新的累计确认（重复ACK）
                    dup_acks++;
                    if (dup_acks == 3) {
                        // 快速重传
                        if (base < packets.size() && !acked[base]) {
                            std::cout << "Fast Retransmit packet " << packets[base].header.seq << std::endl;
                            send_packet_by_index(base);
                            ssthresh = std::max((double)MSS * 2, cwnd / 2);
                            cwnd = ssthresh + 3 * MSS;
                        }
                    }
                }
            }
        }
        
        // 检查超时
        double current_time = get_timestamp();
        if (base < packets.size() && !acked[base]) {
            if (send_times[base] > 0 && (current_time - send_times[base] > 0.5)) { // 500ms超时
                std::cout << "Timeout for packet " << packets[base].header.seq << std::endl;
                send_packet_by_index(base);
                
                // TCP Reno 超时处理
                ssthresh = std::max((double)MSS * 2, cwnd / 2);
                cwnd = MSS;
                dup_acks = 0;
                send_times[base] = current_time;
            }
        }
    }
    
    double end_time = get_timestamp();
    double duration = end_time - start_time;
    double total_bytes = packets.size() * MSS;
    double throughput = (total_bytes * 8) / duration / 1000000.0; // Mbps
    
    std::cout << "Transfer completed in " << duration << " seconds." << std::endl;
    std::cout << "Average Throughput: " << throughput << " Mbps" << std::endl;
}

void Sender::start() {
    load_file();
    handshake();
    send_data();
    teardown();
}
