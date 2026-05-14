#include <iostream>
#include <string>
#include "sender.h"
#include "receiver.h"

void print_usage() {
    std::cout << "Usage:" << std::endl;
    std::cout << "  Sender:   Lab2 -s <target_ip> <target_port> <local_port> <input_file> <window_size>" << std::endl;
    std::cout << "  Receiver: Lab2 -r <listen_port> <output_file> <window_size>" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        print_usage();
        return 1;
    }

    std::string mode = argv[1];

    if (mode == "-s") {
        if (argc != 7) {
            print_usage();
            return 1;
        }
        std::string ip = argv[2];
        int port = std::stoi(argv[3]);
        int local_port = std::stoi(argv[4]);
        std::string file = argv[5];
        int window = std::stoi(argv[6]);

        Sender sender(ip, port, local_port, file, window);
        sender.start();

    } else if (mode == "-r") {
        if (argc != 5) {
            print_usage();
            return 1;
        }
        int port = std::stoi(argv[2]);
        std::string file = argv[3];
        int window = std::stoi(argv[4]);

        Receiver receiver(port, file, window);
        receiver.start();

    } else {
        print_usage();
        return 1;
    }

    return 0;
}

