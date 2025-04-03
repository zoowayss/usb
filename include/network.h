#ifndef NETWORK_H
#define NETWORK_H

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include "usbip_protocol.h"

class TCPSocket {
public:
    TCPSocket() : sockfd_(-1) {}
    explicit TCPSocket(int sockfd) : sockfd_(sockfd) {}
    ~TCPSocket();

    bool create();
    bool bind(int port);
    bool listen(int backlog = 5);
    bool connect(const std::string& host, int port);
    std::shared_ptr<TCPSocket> accept();
    
    bool send(const void* data, size_t size);
    bool receive(void* buffer, size_t size, size_t& bytesRead);
    
    // 新增：带超时的接收方法
    bool receiveWithTimeout(void* buffer, size_t size, size_t& bytesRead, int timeoutSec = 5);
    
    // 新增：设置套接字超时
    bool setTimeout(int seconds);
    
    bool isValid() const { return sockfd_ >= 0; }
    void close();
    
    // 发送和接收完整的USBIP包
    bool sendPacket(const usbip_packet& packet);
    bool receivePacket(usbip_packet& packet);
    
    // 新增：带超时的接收包方法
    bool receivePacketWithTimeout(usbip_packet& packet, int timeoutSec = 5);

private:
    int sockfd_;
};

class Server {
public:
    explicit Server(int port);
    ~Server();
    
    bool start();
    void stop();
    
    // 设置新连接回调
    void setConnectionHandler(std::function<void(std::shared_ptr<TCPSocket>)> handler) {
        connectionHandler_ = std::move(handler);
    }

private:
    void acceptLoop();
    
    int port_;
    std::atomic<bool> running_;
    TCPSocket serverSocket_;
    std::thread acceptThread_;
    std::function<void(std::shared_ptr<TCPSocket>)> connectionHandler_;
};

class Client {
public:
    Client();
    ~Client();
    
    bool connect(const std::string& host, int port);
    void disconnect();
    
    // 发送和接收USBIP包
    bool sendPacket(const usbip_packet& packet);
    bool receivePacket(usbip_packet& packet);
    
    // 新增：带超时的接收包方法
    bool receivePacketWithTimeout(usbip_packet& packet, int timeoutSec = 5);
    
    bool isConnected() const { return socket_ && socket_->isValid(); }

private:
    std::shared_ptr<TCPSocket> socket_;
};

#endif // NETWORK_H 