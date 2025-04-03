#include "../include/network.h"
#include <iostream>
#include <cstring>

// TCPSocket实现
TCPSocket::~TCPSocket() {
    close();
}

bool TCPSocket::create() {
    sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_ < 0) {
        std::cerr << "创建套接字失败: " << strerror(errno) << std::endl;
        return false;
    }
    
    // 设置套接字选项以重用地址
    int reuse = 1;
    if (setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "设置套接字选项失败: " << strerror(errno) << std::endl;
        close();
        return false;
    }
    
    return true;
}

bool TCPSocket::bind(int port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    
    if (::bind(sockfd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "绑定端口失败: " << strerror(errno) << std::endl;
        return false;
    }
    
    return true;
}

bool TCPSocket::listen(int backlog) {
    if (::listen(sockfd_, backlog) < 0) {
        std::cerr << "监听失败: " << strerror(errno) << std::endl;
        return false;
    }
    
    return true;
}

bool TCPSocket::connect(const std::string& host, int port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "无效的IP地址: " << host << std::endl;
        return false;
    }
    
    if (::connect(sockfd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "连接失败: " << strerror(errno) << std::endl;
        return false;
    }
    
    return true;
}

std::shared_ptr<TCPSocket> TCPSocket::accept() {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    int client_sockfd = ::accept(sockfd_, (struct sockaddr*)&client_addr, &client_len);
    if (client_sockfd < 0) {
        std::cerr << "接受连接失败: " << strerror(errno) << std::endl;
        return nullptr;
    }
    
    return std::make_shared<TCPSocket>(client_sockfd);
}

bool TCPSocket::send(const void* data, size_t size) {
    const char* p = static_cast<const char*>(data);
    size_t total_sent = 0;
    
    while (total_sent < size) {
        ssize_t sent = ::send(sockfd_, p + total_sent, size - total_sent, 0);
        if (sent < 0) {
            if (errno == EINTR) continue; // 被信号中断，重试
            std::cerr << "发送数据失败: " << strerror(errno) << std::endl;
            return false;
        } else if (sent == 0) {
            std::cerr << "连接已关闭" << std::endl;
            return false;
        }
        
        total_sent += sent;
    }
    
    return true;
}

bool TCPSocket::receive(void* buffer, size_t size, size_t& bytesRead) {
    char* p = static_cast<char*>(buffer);
    size_t total_read = 0;
    
    while (total_read < size) {
        ssize_t received = ::recv(sockfd_, p + total_read, size - total_read, 0);
        if (received < 0) {
            if (errno == EINTR) continue; // 被信号中断，重试
            std::cerr << "接收数据失败: " << strerror(errno) << std::endl;
            bytesRead = total_read;
            return false;
        } else if (received == 0) {
            std::cerr << "连接已关闭" << std::endl;
            bytesRead = total_read;
            return false;
        }
        
        total_read += received;
    }
    
    bytesRead = total_read;
    return true;
}

void TCPSocket::close() {
    if (sockfd_ >= 0) {
        ::close(sockfd_);
        sockfd_ = -1;
    }
}

// 发送USBIP数据包
bool TCPSocket::sendPacket(const usbip_packet& packet) {
    // 发送头部
    usbip_header header = packet.header;
    header.version = usbip_utils::htonl_wrap(header.version);
    header.command = usbip_utils::htonl_wrap(header.command);
    header.status = usbip_utils::htonl_wrap(header.status);
    
    if (!send(&header, sizeof(header))) {
        return false;
    }
    
    // 根据命令类型发送不同的数据
    switch (packet.header.command) {
        case USBIP_CMD_SUBMIT: {
            cmd_submit cmd = packet.cmd_submit;
            cmd.seqnum = usbip_utils::htonl_wrap(cmd.seqnum);
            cmd.devid = usbip_utils::htonl_wrap(cmd.devid);
            cmd.direction = usbip_utils::htonl_wrap(cmd.direction);
            cmd.ep = usbip_utils::htonl_wrap(cmd.ep);
            cmd.transfer_flags = usbip_utils::htonl_wrap(cmd.transfer_flags);
            cmd.transfer_buffer_length = usbip_utils::htonl_wrap(cmd.transfer_buffer_length);
            cmd.start_frame = usbip_utils::htonl_wrap(cmd.start_frame);
            cmd.number_of_packets = usbip_utils::htonl_wrap(cmd.number_of_packets);
            cmd.interval = usbip_utils::htonl_wrap(cmd.interval);
            
            if (!send(&cmd, sizeof(cmd))) {
                return false;
            }
            break;
        }
        case USBIP_RET_SUBMIT: {
            ret_submit ret = packet.ret_submit;
            ret.seqnum = usbip_utils::htonl_wrap(ret.seqnum);
            ret.devid = usbip_utils::htonl_wrap(ret.devid);
            ret.direction = usbip_utils::htonl_wrap(ret.direction);
            ret.ep = usbip_utils::htonl_wrap(ret.ep);
            ret.status = usbip_utils::htonl_wrap(ret.status);
            ret.actual_length = usbip_utils::htonl_wrap(ret.actual_length);
            ret.start_frame = usbip_utils::htonl_wrap(ret.start_frame);
            ret.number_of_packets = usbip_utils::htonl_wrap(ret.number_of_packets);
            ret.error_count = usbip_utils::htonl_wrap(ret.error_count);
            
            if (!send(&ret, sizeof(ret))) {
                return false;
            }
            break;
        }
        case USBIP_OP_REQ_DEVLIST: {
            op_devlist_request req = packet.devlist_req;
            req.version = usbip_utils::htonl_wrap(req.version);
            
            if (!send(&req, sizeof(req))) {
                return false;
            }
            break;
        }
        case USBIP_OP_REQ_IMPORT: {
            op_import_request req = packet.import_req;
            req.version = usbip_utils::htonl_wrap(req.version);
            
            if (!send(&req, sizeof(req))) {
                return false;
            }
            break;
        }
        case USBIP_OP_REP_IMPORT: {
            op_import_reply rep = packet.import_rep;
            rep.version = usbip_utils::htonl_wrap(rep.version);
            rep.status = usbip_utils::htonl_wrap(rep.status);
            
            if (!send(&rep, sizeof(rep))) {
                return false;
            }
            break;
        }
    }
    
    // 如果有数据，发送数据
    if (!packet.data.empty()) {
        if (!send(packet.data.data(), packet.data.size())) {
            return false;
        }
    }
    
    return true;
}

// 接收USBIP数据包
bool TCPSocket::receivePacket(usbip_packet& packet) {
    // 接收头部
    usbip_header header;
    size_t bytesRead;
    
    if (!receive(&header, sizeof(header), bytesRead)) {
        return false;
    }
    
    packet.header.version = usbip_utils::ntohl_wrap(header.version);
    packet.header.command = usbip_utils::ntohl_wrap(header.command);
    packet.header.status = usbip_utils::ntohl_wrap(header.status);
    
    // 根据命令类型接收不同的数据
    switch (packet.header.command) {
        case USBIP_CMD_SUBMIT: {
            cmd_submit cmd;
            if (!receive(&cmd, sizeof(cmd), bytesRead)) {
                return false;
            }
            
            packet.cmd_submit.seqnum = usbip_utils::ntohl_wrap(cmd.seqnum);
            packet.cmd_submit.devid = usbip_utils::ntohl_wrap(cmd.devid);
            packet.cmd_submit.direction = usbip_utils::ntohl_wrap(cmd.direction);
            packet.cmd_submit.ep = usbip_utils::ntohl_wrap(cmd.ep);
            packet.cmd_submit.transfer_flags = usbip_utils::ntohl_wrap(cmd.transfer_flags);
            packet.cmd_submit.transfer_buffer_length = usbip_utils::ntohl_wrap(cmd.transfer_buffer_length);
            packet.cmd_submit.start_frame = usbip_utils::ntohl_wrap(cmd.start_frame);
            packet.cmd_submit.number_of_packets = usbip_utils::ntohl_wrap(cmd.number_of_packets);
            packet.cmd_submit.interval = usbip_utils::ntohl_wrap(cmd.interval);
            memcpy(packet.cmd_submit.setup, cmd.setup, 8);
            
            // 如果是OUT方向，接收数据
            if (packet.cmd_submit.direction == USBIP_DIR_OUT && packet.cmd_submit.transfer_buffer_length > 0) {
                packet.data.resize(packet.cmd_submit.transfer_buffer_length);
                if (!receive(packet.data.data(), packet.data.size(), bytesRead)) {
                    return false;
                }
            }
            break;
        }
        case USBIP_RET_SUBMIT: {
            ret_submit ret;
            if (!receive(&ret, sizeof(ret), bytesRead)) {
                return false;
            }
            
            packet.ret_submit.seqnum = usbip_utils::ntohl_wrap(ret.seqnum);
            packet.ret_submit.devid = usbip_utils::ntohl_wrap(ret.devid);
            packet.ret_submit.direction = usbip_utils::ntohl_wrap(ret.direction);
            packet.ret_submit.ep = usbip_utils::ntohl_wrap(ret.ep);
            packet.ret_submit.status = usbip_utils::ntohl_wrap(ret.status);
            packet.ret_submit.actual_length = usbip_utils::ntohl_wrap(ret.actual_length);
            packet.ret_submit.start_frame = usbip_utils::ntohl_wrap(ret.start_frame);
            packet.ret_submit.number_of_packets = usbip_utils::ntohl_wrap(ret.number_of_packets);
            packet.ret_submit.error_count = usbip_utils::ntohl_wrap(ret.error_count);
            
            // 如果是IN方向，接收数据
            if (packet.ret_submit.direction == USBIP_DIR_IN && packet.ret_submit.actual_length > 0) {
                packet.data.resize(packet.ret_submit.actual_length);
                if (!receive(packet.data.data(), packet.data.size(), bytesRead)) {
                    return false;
                }
            }
            break;
        }
        case USBIP_OP_REQ_DEVLIST: {
            op_devlist_request req;
            if (!receive(&req, sizeof(req), bytesRead)) {
                return false;
            }
            
            packet.devlist_req.version = usbip_utils::ntohl_wrap(req.version);
            break;
        }
        case USBIP_OP_REQ_IMPORT: {
            op_import_request req;
            if (!receive(&req, sizeof(req), bytesRead)) {
                return false;
            }
            
            packet.import_req.version = usbip_utils::ntohl_wrap(req.version);
            memcpy(packet.import_req.busid, req.busid, 32);
            break;
        }
        case USBIP_OP_REP_IMPORT: {
            op_import_reply rep;
            if (!receive(&rep, sizeof(rep), bytesRead)) {
                return false;
            }
            
            packet.import_rep.version = usbip_utils::ntohl_wrap(rep.version);
            packet.import_rep.status = usbip_utils::ntohl_wrap(rep.status);
            packet.import_rep.udev = rep.udev;
            break;
        }
    }
    
    return true;
}

// Server实现
Server::Server(int port)
    : port_(port), running_(false) {
}

Server::~Server() {
    stop();
}

bool Server::start() {
    if (!serverSocket_.create()) {
        return false;
    }
    
    if (!serverSocket_.bind(port_)) {
        return false;
    }
    
    if (!serverSocket_.listen()) {
        return false;
    }
    
    running_ = true;
    acceptThread_ = std::thread(&Server::acceptLoop, this);
    
    std::cout << "服务器已启动，监听端口: " << port_ << std::endl;
    return true;
}

void Server::stop() {
    running_ = false;
    serverSocket_.close();
    
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
    
    std::cout << "服务器已停止" << std::endl;
}

void Server::acceptLoop() {
    while (running_) {
        std::shared_ptr<TCPSocket> clientSocket = serverSocket_.accept();
        if (clientSocket && connectionHandler_) {
            connectionHandler_(clientSocket);
        }
    }
}

// Client实现
Client::Client() 
    : socket_(std::make_shared<TCPSocket>()) {
}

Client::~Client() {
    disconnect();
}

bool Client::connect(const std::string& host, int port) {
    if (!socket_->create()) {
        return false;
    }
    
    if (!socket_->connect(host, port)) {
        return false;
    }
    
    std::cout << "已连接到服务器: " << host << ":" << port << std::endl;
    return true;
}

void Client::disconnect() {
    if (socket_) {
        socket_->close();
    }
    
    std::cout << "已断开连接" << std::endl;
}

bool Client::sendPacket(const usbip_packet& packet) {
    return socket_->sendPacket(packet);
}

bool Client::receivePacket(usbip_packet& packet) {
    return socket_->receivePacket(packet);
} 