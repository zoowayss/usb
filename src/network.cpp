#include "../include/network.h"
#include <iostream>
#include <cstring>
#include <iomanip>
#include <fcntl.h>
#include <cctype>

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
    
    std::cout << "准备接收 " << size << " 字节数据..." << std::endl;
    
    while (total_read < size) {
        std::cout << "尝试接收剩余 " << (size - total_read) << " 字节数据..." << std::endl;
        ssize_t received = ::recv(sockfd_, p + total_read, size - total_read, 0);
        
        if (received < 0) {
            if (errno == EINTR) {
                std::cout << "接收被信号中断，重试..." << std::endl;
                continue; // 被信号中断，重试
            }
            std::cerr << "接收数据失败: " << strerror(errno) << std::endl;
            bytesRead = total_read;
            return false;
        } else if (received == 0) {
            std::cerr << "连接已关闭" << std::endl;
            bytesRead = total_read;
            return false;
        }
        
        std::cout << "成功接收 " << received << " 字节数据" << std::endl;
        
        // 打印前10个字节的十六进制值（如果有）
        if (received > 0) {
            std::cout << "接收的数据前 " << std::min(10, static_cast<int>(received)) << " 字节: ";
            for (int i = 0; i < std::min(10, static_cast<int>(received)); i++) {
                std::cout << std::hex << std::setfill('0') << std::setw(2) 
                          << static_cast<int>(static_cast<unsigned char>(*(p + total_read + i))) << " ";
            }
            std::cout << std::dec << std::endl;
        }
        
        total_read += received;
    }
    
    std::cout << "总共接收了 " << total_read << " 字节数据" << std::endl;
    
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
    // 打印发送的包信息
    std::cout << "准备发送数据包: 版本=0x" << std::hex << packet.header.version
              << ", 命令=0x" << packet.header.command
              << ", 状态=0x" << packet.header.status << std::dec << std::endl;
    
    // 在发送头部时正确处理字节序
    // 注意：对于官方客户端兼容性，需要小心处理版本和命令的字节序
    usbip_header header;
    
    // 官方USBIP要求：版本和命令都需要是网络字节序
    // 但内部表示与发送格式不同
    uint8_t* headerBytes = reinterpret_cast<uint8_t*>(&header);
    
    // 手动构建头部，以确保与官方客户端兼容
    // 版本和命令需要手动转换为正确的格式
    headerBytes[0] = (packet.header.version >> 8) & 0xff;
    headerBytes[1] = packet.header.version & 0xff;
    headerBytes[2] = (packet.header.command >> 8) & 0xff;
    headerBytes[3] = packet.header.command & 0xff;
    
    // 状态字段是一个完整的uint32_t，使用标准转换
    header.status = usbip_utils::htonl_wrap(packet.header.status);
    
    // 发送头部
    if (!send(&header, sizeof(header))) {
        return false;
    }
    
    // 根据命令类型，发送不同的数据
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
        // 已注释掉 USBIP_RET_SUBMIT 的 case
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
            
            // 构建特定的响应格式，确保字节序正确
            std::cout << "发送导入设备响应结构，版本=" << std::hex << rep.version
                      << "，状态=" << rep.status << std::dec 
                      << "，命令码=0x0003" << std::endl;
            
            // 确保状态码在网络字节序中为0（成功）或标准错误码
            uint32_t status_value = rep.status;
            
            // 为了匹配官方客户端预期，确保状态码为0（成功）或为正确的错误值
            if (rep.status != 0) {
                // 如果失败，使用一个标准错误代码
                status_value = -22; // EINVAL对应的负值，常见的错误码
                std::cout << "设备导入失败，使用错误码: " << status_value << std::endl;
            }
            
            // 1. 版本 (4字节)
            uint32_t version_n = usbip_utils::htonl_wrap(rep.version);
            if (!send(&version_n, sizeof(version_n))) {
                return false;
            }
            
            // 2. 状态 (4字节)
            uint32_t status_n = usbip_utils::htonl_wrap(status_value);
            if (!send(&status_n, sizeof(status_n))) {
                return false;
            }
            
            // 3. 如果成功，发送设备信息
            if (rep.status == 0) {
                std::cout << "发送设备信息, 总线ID=" << rep.udev.busid << std::endl;
                
                // 手动构建设备信息结构，确保字节序正确
                // 注意：path 和 busid 是字符串，保持原样
                // 步骤 1: 发送 path (256字节)
                if (!send(rep.udev.path, 256)) {
                    return false;
                }
                
                // 步骤 2: 发送 busid (32字节)
                if (!send(rep.udev.busid, 32)) {
                    return false;
                }
                
                // 步骤 3: 发送 busnum (4字节)
                uint32_t busnum_n = usbip_utils::htonl_wrap(rep.udev.busnum);
                if (!send(&busnum_n, sizeof(busnum_n))) {
                    return false;
                }
                
                // 步骤 4: 发送 devnum (4字节)
                uint32_t devnum_n = usbip_utils::htonl_wrap(rep.udev.devnum);
                if (!send(&devnum_n, sizeof(devnum_n))) {
                    return false;
                }
                
                // 步骤 5: 发送 speed (4字节)
                uint32_t speed_n = usbip_utils::htonl_wrap(rep.udev.speed);
                if (!send(&speed_n, sizeof(speed_n))) {
                    return false;
                }
                
                // 步骤 6: 发送 idVendor (2字节)
                uint16_t idVendor_n = usbip_utils::htons_wrap(rep.udev.idVendor);
                if (!send(&idVendor_n, sizeof(idVendor_n))) {
                    return false;
                }
                
                // 步骤 7: 发送 idProduct (2字节)
                uint16_t idProduct_n = usbip_utils::htons_wrap(rep.udev.idProduct);
                if (!send(&idProduct_n, sizeof(idProduct_n))) {
                    return false;
                }
                
                // 步骤 8: 发送 bcdDevice (2字节)
                uint16_t bcdDevice_n = usbip_utils::htons_wrap(rep.udev.bcdDevice);
                if (!send(&bcdDevice_n, sizeof(bcdDevice_n))) {
                    return false;
                }
                
                // 步骤 9: 发送 bDeviceClass (1字节)
                if (!send(&rep.udev.bDeviceClass, 1)) {
                    return false;
                }
                
                // 步骤 10: 发送 bDeviceSubClass (1字节)
                if (!send(&rep.udev.bDeviceSubClass, 1)) {
                    return false;
                }
                
                // 步骤 11: 发送 bDeviceProtocol (1字节)
                if (!send(&rep.udev.bDeviceProtocol, 1)) {
                    return false;
                }
                
                // 步骤 12: 发送 bConfigurationValue (1字节)
                if (!send(&rep.udev.bConfigurationValue, 1)) {
                    return false;
                }
                
                // 步骤 13: 发送 bNumConfigurations (1字节)
                if (!send(&rep.udev.bNumConfigurations, 1)) {
                    return false;
                }
                
                // 步骤 14: 发送 bNumInterfaces (1字节)
                if (!send(&rep.udev.bNumInterfaces, 1)) {
                    return false;
                }
            }
            
            std::cout << "导入设备响应发送完成" << std::endl;
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
    
    // 打印收到的原始头部数据（十六进制）
    std::cout << "原始头部数据: ";
    uint8_t* headerBytes = reinterpret_cast<uint8_t*>(&header);
    for (size_t i = 0; i < sizeof(header); i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(headerBytes[i]) << " ";
    }
    std::cout << std::dec << std::endl;
    
    // 手动以正确的方式处理字节序
    // USBIP协议的头部是两个字节一组的小端序，但整个32位是网络字节序(大端)
    packet.header.version = (headerBytes[0] << 8) | headerBytes[1];
    packet.header.command = (headerBytes[2] << 8) | headerBytes[3];
    packet.header.status = 0;  // 前两个字段后面的8字节在某些官方客户端命令中可能是数据部分
    
    std::cout << "正确解析结果: 版本=0x" << std::hex << packet.header.version
              << ", 命令=0x" << packet.header.command 
              << std::dec << std::endl;
    
    // 处理官方USBIP客户端的特殊命令格式
    if (packet.header.command == USBIP_OP_REQ_IMPORT) {
        std::cout << "检测到导入请求" << std::endl;
        // 总线ID紧跟在头部后面，已经在前12字节接收了
        packet.import_req.version = packet.header.version;
        
        // 从头部中提取总线ID（通常是第8-12字节，格式如"1-5\0"）
        char busid[32] = {0};
        for (int i = 0; i < std::min(32, static_cast<int>(bytesRead - 8)); i++) {
            busid[i] = static_cast<char>(headerBytes[8 + i]);
        }
        
        std::strncpy(packet.import_req.busid, busid, sizeof(packet.import_req.busid) - 1);
        std::cout << "解析总线ID: [" << packet.import_req.busid << "]" << std::endl;
        
        return true;
    }
    
    // 其他标准USBIP命令的处理...
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
            // 处理官方USBIP客户端可能发送的其他命令
            std::cout << "收到未知命令: 0x" << std::hex << packet.header.command << std::dec << std::endl;
            
            // 尝试读取额外数据作为调试信息
            std::vector<uint8_t> additionalData(256);
            try {
                size_t additionalBytesRead = 0;
                if (receive(additionalData.data(), additionalData.size(), additionalBytesRead)) {
                    std::cout << "额外读取了 " << additionalBytesRead << " 字节数据" << std::endl;
                    
                    // 打印所有额外读取的数据
                    if (additionalBytesRead > 0) {
                        std::cout << "额外数据: ";
                        for (size_t i = 0; i < additionalBytesRead; i++) {
                            std::cout << std::hex << std::setw(2) << std::setfill('0') 
                                      << static_cast<int>(additionalData[i]) << " ";
                        }
                        std::cout << std::dec << std::endl;
                    }
                    
                    // 修改：对于命令0，可能是客户端的版本检查请求
                    if (packet.header.command == 0) {
                        std::cout << "可能是客户端版本检查请求，将尝试发送版本响应" << std::endl;
                        
                        // 准备一个虚拟的版本响应包
                        usbip_packet versionReply;
                        versionReply.header.version = USBIP_VERSION; // 使用我们的版本
                        versionReply.header.command = 0; // 回复同样的命令
                        versionReply.header.status = 0; // 成功状态
                        
                        // 添加版本相关数据
                        versionReply.data.resize(4);
                        uint32_t version = usbip_utils::htonl_wrap(USBIP_VERSION);
                        memcpy(versionReply.data.data(), &version, sizeof(version));
                        
                        // 发送响应
                        if (sendPacket(versionReply)) {
                            std::cout << "版本响应发送成功" << std::endl;
                            return true; // 继续保持连接
                        }
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "尝试读取额外数据时发生异常: " << e.what() << std::endl;
            }
            
            // 为了兼容性，不要立即断开连接
            std::cout << "收到不支持的命令，但将继续保持连接" << std::endl;
            return true; // 继续处理后续请求
        }
    }
    
    return true;
}

bool TCPSocket::setTimeout(int seconds) {
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    
    // 设置接收超时
    if (setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        std::cerr << "设置套接字接收超时失败: " << strerror(errno) << std::endl;
        return false;
    }
    
    // 设置发送超时
    if (setsockopt(sockfd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        std::cerr << "设置套接字发送超时失败: " << strerror(errno) << std::endl;
        return false;
    }
    
    return true;
}

bool TCPSocket::receiveWithTimeout(void* buffer, size_t size, size_t& bytesRead, int timeoutSec) {
    // 保存当前的套接字标志
    int flags = fcntl(sockfd_, F_GETFL, 0);
    if (flags == -1) {
        std::cerr << "获取套接字标志失败: " << strerror(errno) << std::endl;
        return false;
    }
    
    // 设置超时
    struct timeval tv;
    tv.tv_sec = timeoutSec;
    tv.tv_usec = 0;
    
    if (setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        std::cerr << "设置套接字接收超时失败: " << strerror(errno) << std::endl;
        return false;
    }
    
    // 执行带超时的接收
    bool result = receive(buffer, size, bytesRead);
    
    // 恢复原始标志
    if (fcntl(sockfd_, F_SETFL, flags) == -1) {
        std::cerr << "恢复套接字标志失败: " << strerror(errno) << std::endl;
    }
    
    return result;
}

bool TCPSocket::receivePacketWithTimeout(usbip_packet& packet, int timeoutSec) {
    // 设置接收超时
    struct timeval tv;
    tv.tv_sec = timeoutSec;
    tv.tv_usec = 0;
    
    if (setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        std::cerr << "设置套接字接收超时失败: " << strerror(errno) << std::endl;
        return false;
    }
    
    // 尝试接收数据包
    bool result = receivePacket(packet);
    
    return result;
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
    
    // 设置5秒超时
    socket_->setTimeout(5);
    
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

bool Client::receivePacketWithTimeout(usbip_packet& packet, int timeoutSec) {
    return socket_->receivePacketWithTimeout(packet, timeoutSec);
} 