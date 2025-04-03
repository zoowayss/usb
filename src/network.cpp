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
            cmd_submit cmd = packet.cmd_submit_data;
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
            ret_submit ret = packet.ret_submit_data;
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
    
    std::cout << "准备接收数据包头部..." << std::endl;
    if (!receive(&header, sizeof(header), bytesRead)) {
        std::cerr << "接收数据包头部失败，实际接收 " << bytesRead << " 字节" << std::endl;
        return false;
    }
    
    // 网络字节序转换为本地字节序
    packet.header.version = usbip_utils::ntohl_wrap(header.version);
    packet.header.command = usbip_utils::ntohl_wrap(header.command);
    packet.header.status = usbip_utils::ntohl_wrap(header.status);
    
    std::cout << "接收到数据包头部: 版本=" << std::hex << packet.header.version
              << ", 命令=" << packet.header.command
              << ", 状态=" << packet.header.status << std::dec << std::endl;
    
    // 根据命令类型接收不同的数据
    switch (packet.header.command) {
        case USBIP_CMD_SUBMIT: {
            std::cout << "接收CMD_SUBMIT数据..." << std::endl;
            cmd_submit cmd;
            if (!receive(&cmd, sizeof(cmd), bytesRead)) {
                std::cerr << "接收CMD_SUBMIT数据失败，实际接收 " << bytesRead << " 字节" << std::endl;
                return false;
            }
            
            packet.cmd_submit_data.seqnum = usbip_utils::ntohl_wrap(cmd.seqnum);
            packet.cmd_submit_data.devid = usbip_utils::ntohl_wrap(cmd.devid);
            packet.cmd_submit_data.direction = usbip_utils::ntohl_wrap(cmd.direction);
            packet.cmd_submit_data.ep = usbip_utils::ntohl_wrap(cmd.ep);
            packet.cmd_submit_data.transfer_flags = usbip_utils::ntohl_wrap(cmd.transfer_flags);
            packet.cmd_submit_data.transfer_buffer_length = usbip_utils::ntohl_wrap(cmd.transfer_buffer_length);
            packet.cmd_submit_data.start_frame = usbip_utils::ntohl_wrap(cmd.start_frame);
            packet.cmd_submit_data.number_of_packets = usbip_utils::ntohl_wrap(cmd.number_of_packets);
            packet.cmd_submit_data.interval = usbip_utils::ntohl_wrap(cmd.interval);
            memcpy(packet.cmd_submit_data.setup, cmd.setup, 8);
            
            // 如果是OUT方向，接收数据
            if (packet.cmd_submit_data.direction == USBIP_DIR_OUT && packet.cmd_submit_data.transfer_buffer_length > 0) {
                std::cout << "接收CMD_SUBMIT OUT数据，大小: " << packet.cmd_submit_data.transfer_buffer_length << " 字节" << std::endl;
                packet.data.resize(packet.cmd_submit_data.transfer_buffer_length);
                if (!receive(packet.data.data(), packet.data.size(), bytesRead)) {
                    std::cerr << "接收CMD_SUBMIT OUT数据失败，实际接收 " << bytesRead << " 字节" << std::endl;
                    return false;
                }
            }
            break;
        }
        case USBIP_RET_SUBMIT: {
            std::cout << "接收RET_SUBMIT数据..." << std::endl;
            ret_submit ret;
            if (!receive(&ret, sizeof(ret), bytesRead)) {
                std::cerr << "接收RET_SUBMIT数据失败，实际接收 " << bytesRead << " 字节" << std::endl;
                return false;
            }
            
            packet.ret_submit_data.seqnum = usbip_utils::ntohl_wrap(ret.seqnum);
            packet.ret_submit_data.devid = usbip_utils::ntohl_wrap(ret.devid);
            packet.ret_submit_data.direction = usbip_utils::ntohl_wrap(ret.direction);
            packet.ret_submit_data.ep = usbip_utils::ntohl_wrap(ret.ep);
            packet.ret_submit_data.status = usbip_utils::ntohl_wrap(ret.status);
            packet.ret_submit_data.actual_length = usbip_utils::ntohl_wrap(ret.actual_length);
            packet.ret_submit_data.start_frame = usbip_utils::ntohl_wrap(ret.start_frame);
            packet.ret_submit_data.number_of_packets = usbip_utils::ntohl_wrap(ret.number_of_packets);
            packet.ret_submit_data.error_count = usbip_utils::ntohl_wrap(ret.error_count);
            
            // 如果是IN方向，接收数据
            if (packet.ret_submit_data.direction == USBIP_DIR_IN && packet.ret_submit_data.actual_length > 0) {
                std::cout << "接收RET_SUBMIT IN数据，大小: " << packet.ret_submit_data.actual_length << " 字节" << std::endl;
                packet.data.resize(packet.ret_submit_data.actual_length);
                if (!receive(packet.data.data(), packet.data.size(), bytesRead)) {
                    std::cerr << "接收RET_SUBMIT IN数据失败，实际接收 " << bytesRead << " 字节" << std::endl;
                    return false;
                }
            }
            break;
        }
        case USBIP_OP_REQ_DEVLIST: {
            std::cout << "接收OP_REQ_DEVLIST数据..." << std::endl;
            op_devlist_request req;
            if (!receive(&req, sizeof(req), bytesRead)) {
                std::cerr << "接收OP_REQ_DEVLIST数据失败，实际接收 " << bytesRead << " 字节" << std::endl;
                return false;
            }
            
            packet.devlist_req.version = usbip_utils::ntohl_wrap(req.version);
            break;
        }
        case USBIP_OP_REQ_IMPORT: {
            std::cout << "接收OP_REQ_IMPORT数据..." << std::endl;
            op_import_request req;
            if (!receive(&req, sizeof(req), bytesRead)) {
                std::cerr << "接收OP_REQ_IMPORT数据失败，实际接收 " << bytesRead << " 字节" << std::endl;
                return false;
            }
            
            packet.import_req.version = usbip_utils::ntohl_wrap(req.version);
            memcpy(packet.import_req.busid, req.busid, 32);
            break;
        }
        case USBIP_OP_REP_IMPORT: {
            std::cout << "接收OP_REP_IMPORT数据..." << std::endl;
            op_import_reply rep;
            if (!receive(&rep, sizeof(rep), bytesRead)) {
                std::cerr << "接收OP_REP_IMPORT数据失败，实际接收 " << bytesRead << " 字节" << std::endl;
                return false;
            }
            
            packet.import_rep.version = usbip_utils::ntohl_wrap(rep.version);
            packet.import_rep.status = usbip_utils::ntohl_wrap(rep.status);
            packet.import_rep.udev = rep.udev;
            break;
        }
        case USBIP_OP_REP_DEVLIST: {
            std::cout << "接收OP_REP_DEVLIST数据..." << std::endl;
            
            // 首先接收设备数量（4字节）
            uint32_t numDevices = 0;
            if (!receive(&numDevices, sizeof(numDevices), bytesRead)) {
                std::cerr << "接收设备数量失败，实际接收 " << bytesRead << " 字节" << std::endl;
                return false;
            }
            
            numDevices = usbip_utils::ntohl_wrap(numDevices);
            std::cout << "设备列表包含 " << numDevices << " 个设备" << std::endl;
            
            // 计算需要接收的数据总大小
            // 对于每个设备: usb_device_info + 1字节接口数量 + 接口数量*4字节
            size_t totalSize = sizeof(uint32_t); // 设备数量
            packet.data.resize(totalSize);
            memcpy(packet.data.data(), &numDevices, sizeof(numDevices));
            
            // 如果有设备，继续接收设备信息
            if (numDevices > 0) {
                // 尝试读取设备信息并动态调整缓冲区大小
                for (uint32_t i = 0; i < numDevices; i++) {
                    // 接收设备基本信息
                    usb_device_info devInfo;
                    if (!receive(&devInfo, sizeof(devInfo), bytesRead)) {
                        std::cerr << "接收设备 " << i+1 << " 信息失败，实际接收 " << bytesRead << " 字节" << std::endl;
                        return false;
                    }
                    
                    // 添加设备信息到数据包
                    size_t oldSize = packet.data.size();
                    packet.data.resize(oldSize + sizeof(devInfo));
                    memcpy(packet.data.data() + oldSize, &devInfo, sizeof(devInfo));
                    
                    // 接收接口数量
                    uint8_t numInterfaces = 0;
                    if (!receive(&numInterfaces, sizeof(numInterfaces), bytesRead)) {
                        std::cerr << "接收设备 " << i+1 << " 接口数量失败" << std::endl;
                        return false;
                    }
                    
                    std::cout << "设备 " << i+1 << " 接口数量: " << static_cast<int>(numInterfaces) << std::endl;
                    
                    // 添加接口数量到数据包
                    oldSize = packet.data.size();
                    packet.data.resize(oldSize + sizeof(numInterfaces));
                    memcpy(packet.data.data() + oldSize, &numInterfaces, sizeof(numInterfaces));
                    
                    // 接收接口信息 (每个接口4字节)
                    if (numInterfaces > 0) {
                        size_t interfaceDataSize = numInterfaces * 4;
                        std::vector<uint8_t> interfaceData(interfaceDataSize);
                        
                        if (!receive(interfaceData.data(), interfaceDataSize, bytesRead)) {
                            std::cerr << "接收设备 " << i+1 << " 接口信息失败，实际接收 " << bytesRead << " 字节" << std::endl;
                            return false;
                        }
                        
                        // 添加接口信息到数据包
                        oldSize = packet.data.size();
                        packet.data.resize(oldSize + interfaceDataSize);
                        memcpy(packet.data.data() + oldSize, interfaceData.data(), interfaceDataSize);
                    }
                }
            }
            
            std::cout << "成功接收设备列表数据，总大小: " << packet.data.size() << " 字节" << std::endl;
            break;
        }
        default: {
            std::cerr << "未知的命令类型: " << std::hex << packet.header.command << std::dec << std::endl;
            return false;
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