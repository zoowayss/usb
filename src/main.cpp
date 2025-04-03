#include <iostream>
#include <string>
#include <unistd.h>
#include <getopt.h>
#include <csignal>
#include <thread>
#include "../include/client.h"
#include "../include/server.h"

// 全局指针，用于在信号处理中停止服务
USBIPServer* g_server = nullptr;
USBIPClient* g_client = nullptr;

// 信号处理函数
void main_signal_handler(int sig) {
    std::cout << "\n收到信号 " << sig << "，准备关闭程序..." << std::endl;
    
    if (g_server) {
        g_server->stop();
    }
    
    if (g_client) {
        g_client->stop();
    }
}

void print_usage() {
    std::cout << "用法: usbip [-c|-s] -p <port> [-i <ip>]\n"
              << "  -c, --client         以客户端模式运行 (Ubuntu)\n"
              << "  -s, --server         以服务端模式运行 (Mac)\n"
              << "  -p, --port <port>    指定端口号\n"
              << "  -i, --ip <ip>        客户端模式下指定服务端IP地址 (默认: 127.0.0.1)\n"
              << "  -h, --help           显示此帮助信息\n";
}

int main(int argc, char* argv[]) {
    bool is_client = false;
    bool is_server = false;
    int port = 3240; // USBIP默认端口
    std::string server_ip = "127.0.0.1"; // 默认IP地址
    
    // 定义长选项
    static struct option long_options[] = {
        {"client", no_argument,       0, 'c'},
        {"server", no_argument,       0, 's'},
        {"port",   required_argument, 0, 'p'},
        {"ip",     required_argument, 0, 'i'},
        {"help",   no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    // 注册信号处理
    signal(SIGINT, main_signal_handler);
    signal(SIGTERM, main_signal_handler);
    
    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "csp:i:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'c':
                is_client = true;
                break;
            case 's':
                is_server = true;
                break;
            case 'p':
                port = std::stoi(optarg);
                break;
            case 'i':
                server_ip = optarg;
                break;
            case 'h':
                print_usage();
                return 0;
            default:
                print_usage();
                return 1;
        }
    }
    
    // 检查是否有非选项参数作为IP地址（兼容旧用法，如 usbip -c 192.168.1.100）
    if (optind < argc && is_client) {
        server_ip = argv[optind];
    }
    
    if (is_client && is_server) {
        std::cerr << "错误: 不能同时指定客户端和服务端模式\n";
        print_usage();
        return 1;
    }
    
    if (!is_client && !is_server) {
        std::cerr << "错误: 必须指定客户端或服务端模式\n";
        print_usage();
        return 1;
    }
    
    try {
        if (is_client) {
            std::cout << "以客户端模式启动，连接服务端: " << server_ip << ":" << port << std::endl;
            USBIPClient client(port, server_ip);
            g_client = &client;
            client.start();
            
            // 等待客户端完成工作（会在客户端代码中控制运行和退出）
            std::cout << "客户端运行中，按 Ctrl+C 停止..." << std::endl;
            
            // 阻止主线程退出，直到客户端完成
            while (g_client && g_client->isRunning()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        } else {
            std::cout << "以服务端模式启动，端口: " << port << std::endl;
            USBIPServer server(port);
            g_server = &server;
            server.start();
            
            // 服务端start()方法已经包含了主循环，会一直运行到停止信号
            g_server = nullptr; // 服务端已经停止
        }
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "程序正常退出" << std::endl;
    return 0;
} 