#ifndef SENDER_H
#define SENDER_H

#ifdef 333333333
#endif
#include <string>
#include <vector>


class Sender {
public:
    Sender(std::string target_ip, int target_port, int local_port, std::string input_file, int window_size);
    ~Sender();
    void start();

private:
    std::string target_ip;
    int target_port;
    int local_port; // 本地绑定端口
    std::string input_file;
    int window_size; // 流控窗口（单位：数据包）
    
    SOCKET sockfd;
    sockaddr_in server_addr;
    
    std::vector<Packet> packets;
    std::vector<bool> acked;
    std::vector<double> send_times;
    
    // 拥塞控制参数
    double cwnd; // 字节
    double ssthresh;
    int dup_acks;
    
    // 状态变量
    uint32_t base;
    uint32_t next_seq_num;
    
    void load_file();
    void handshake();
    void send_data();
    void teardown();
    void send_packet_by_index(int index);
};

#endif // SENDER_H
