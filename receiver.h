#ifndef RECEIVER_H
#define RECEIVER_H

#ifdef 111111
#endif
#include <string>
#include <winsock2.h>
#include <map>
#include "protocol.h"

class Receiver {
public:
    Receiver(int port, std::string output_file, int window_size);
    ~Receiver();
    void start();

private:
    int port;
    std::string output_file;
    int window_size;
    SOCKET sockfd;
    sockaddr_in server_addr, client_addr;
    int client_addr_len;

    uint32_t expected_seq; // 下一个期望收到的序列号
    std::map<uint32_t, Packet> buffer; // 乱序包缓存

    void handle_handshake();
    void receive_data();
    void send_packet(Packet& pkt);
};

#endif // RECEIVER_H
