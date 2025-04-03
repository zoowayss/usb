#include "../include/server.h"
#include "../include/usb_device.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <functional>
#include <iomanip>
#include <signal.h>
#include <atomic>
#include <cstring>

// 全局变量，用于控制程序运行状态
std::atomic<bool> g_running(true);

// 信号处理函数
void signal_handler(int sig) {
    std::cout << "\n收到信号 " << sig << "，准备优雅退出..." << std::endl;
    g_running = false;
}

USBIPServer::USBIPServer(int port)
    : port_(port), running_(false) {
}

USBIPServer::~USBIPServer() {
    stop();
}

bool USBIPServer::start() {
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 扫描USB设备
    if (!scanUSBDevices()) {
        std::cerr << "警告：没有找到可用的USB大容量存储设备" << std::endl;
    }
    
    // 创建并启动TCP服务器
    server_ = std::make_unique<Server>(port_);
    
    // 设置连接处理函数
    server_->setConnectionHandler([this](std::shared_ptr<TCPSocket> clientSocket) {
        // 为每个客户端创建一个线程处理请求
        std::thread clientThread(&USBIPServer::handleClient, this, clientSocket);
        clientThread.detach();
    });
    
    if (!server_->start()) {
        std::cerr << "启动服务器失败" << std::endl;
        return false;
    }
    
    running_ = true;
    std::cout << "服务端已完全启动，等待客户端连接..." << std::endl;
    
    // 保持主线程运行，直到收到停止信号
    try {
        while (running_ && g_running) {
            // 每5秒重新扫描一次USB设备，检测新设备或已移除设备
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            // 重新扫描设备（可选功能）
            // scanUSBDevices();
        }
    } catch (const std::exception& e) {
        std::cerr << "服务端运行时遇到异常: " << e.what() << std::endl;
    }
    
    std::cout << "服务端主循环退出" << std::endl;
    return true;
}

void USBIPServer::stop() {
    if (running_) {
        std::cout << "正在停止服务端..." << std::endl;
        running_ = false;
        
        if (server_) {
            server_->stop();
        }
        
        // 清理资源
        std::lock_guard<std::mutex> lock(deviceMutex_);
        usbDevices_.clear();
        exportedDevices_.clear();
        
        // 清理libusb资源
        libusb::USBDeviceManager::getInstance().cleanup();
        
        std::cout << "服务端已停止" << std::endl;
    }
}

bool USBIPServer::scanUSBDevices() {
    std::lock_guard<std::mutex> lock(deviceMutex_);
    
    // 使用单例获取USB设备管理器
    auto& deviceManager = libusb::USBDeviceManager::getInstance();
    if (!deviceManager.init()) {
        std::cerr << "初始化USB设备管理器失败" << std::endl;
        return false;
    }
    
    // 扫描USB设备
    std::cout << "正在扫描USB大容量存储设备..." << std::endl;
    usbDevices_ = deviceManager.scanDevices();
    
    std::cout << "扫描完成，找到 " << usbDevices_.size() << " 个USB大容量存储设备" << std::endl;
    
    // 打印设备信息
    if (!usbDevices_.empty()) {
        std::cout << "\n可导出的设备列表：" << std::endl;
        std::cout << "------------------------" << std::endl;
        int index = 1;
        for (const auto& device : usbDevices_) {
            std::cout << index++ << ". 设备ID: " << device->getBusID() << std::endl;
            std::cout << "   厂商ID: 0x" << std::hex << std::setw(4) << std::setfill('0') 
                      << device->getVendorID() << std::endl;
            std::cout << "   产品ID: 0x" << std::hex << std::setw(4) << std::setfill('0') 
                      << device->getProductID() << std::dec << std::endl;
            
            // 尝试打开设备并获取更多信息
            if (device->open()) {
                usb_device_info info;
                device->fillDeviceInfo(info);
                std::cout << "   接口数: " << static_cast<int>(info.bNumInterfaces) << std::endl;
                std::cout << "   配置数: " << static_cast<int>(info.bNumConfigurations) << std::endl;
                device->close();
            }
            std::cout << "------------------------" << std::endl;
        }
    } else {
        std::cout << "未找到任何USB大容量存储设备" << std::endl;
        std::cout << "请确保：" << std::endl;
        std::cout << "1. USB设备已正确插入" << std::endl;
        std::cout << "2. 当前用户有权限访问USB设备" << std::endl;
        std::cout << "3. 设备是大容量存储类型（如U盘）" << std::endl;
    }
    
    return !usbDevices_.empty();
}

void USBIPServer::handleClient(std::shared_ptr<TCPSocket> clientSocket) {
    std::cout << "新客户端连接" << std::endl;
    
    // 持续处理客户端请求，直到连接关闭
    while (running_ && clientSocket->isValid()) {
        usbip_packet packet;
        
        // 接收请求
        if (!clientSocket->receivePacket(packet)) {
            std::cerr << "接收数据包失败，关闭连接" << std::endl;
            break;
        }
        
        // 根据命令类型处理请求
        bool success = false;
        switch (packet.header.command) {
            case USBIP_OP_REQ_DEVLIST:
                success = handleDeviceListRequest(clientSocket, packet);
                break;
                
            case USBIP_OP_REQ_IMPORT:
                success = handleImportRequest(clientSocket, packet);
                break;
                
            case USBIP_CMD_SUBMIT:
                success = handleURBRequest(clientSocket, packet);
                break;
                
            case 0: // 处理可能的版本检查请求
                std::cout << "处理可能的版本检查请求" << std::endl;
                // 准备并发送版本信息
                {
                    usbip_packet versionReply;
                    versionReply.header.version = USBIP_VERSION;
                    versionReply.header.command = 0; // 响应的命令与请求相同
                    versionReply.header.status = 0;
                    
                    // 以4字节整数形式发送版本号
                    versionReply.data.resize(4);
                    uint32_t version = usbip_utils::htonl_wrap(USBIP_VERSION);
                    memcpy(versionReply.data.data(), &version, sizeof(version));
                    
                    success = clientSocket->sendPacket(versionReply);
                }
                break;
                
            default:
                std::cerr << "未知命令: " << std::hex << packet.header.command << std::dec << std::endl;
                // 对于未知命令，尝试继续而不是立即断开连接
                success = true; // 允许连接继续
                break;
        }
        
        if (!success) {
            std::cerr << "处理请求失败，关闭连接" << std::endl;
            break;
        }
    }
    
    std::cout << "客户端连接已关闭" << std::endl;
}

bool USBIPServer::handleDeviceListRequest(std::shared_ptr<TCPSocket> clientSocket, const usbip_packet& packet) {
    std::cout << "收到设备列表请求，USBIP版本: " << std::hex << packet.header.version << std::dec << std::endl;
    
    // 扫描设备
    scanUSBDevices();
    
    std::cout << "扫描到 " << usbDevices_.size() << " 个USB设备" << std::endl;
    
    // 准备回复数据包
    usbip_packet reply;
    memset(&reply, 0, sizeof(reply)); // 确保完全初始化
    
    reply.header.version = USBIP_VERSION;
    reply.header.command = USBIP_OP_REP_DEVLIST;
    reply.header.status = 0;
    
    std::cout << "准备发送回复数据包: 版本=" << std::hex << reply.header.version 
              << ", 命令=" << reply.header.command
              << ", 状态=" << reply.header.status << std::dec << std::endl;
    
    // 发送头部
    if (!clientSocket->sendPacket(reply)) {
        std::cerr << "发送设备列表头部失败" << std::endl;
        return false;
    }
    
    // 发送设备数量 (单独发送，不是数据包的一部分)
    uint32_t numDevices = usbip_utils::htonl_wrap(usbDevices_.size());
    if (!clientSocket->send(&numDevices, sizeof(numDevices))) {
        std::cerr << "发送设备数量失败" << std::endl;
        return false;
    }
    
    std::cout << "发送 " << usbDevices_.size() << " 个设备的信息" << std::endl;
    
    // 逐个发送设备信息
    std::lock_guard<std::mutex> lock(deviceMutex_);
    int deviceIndex = 0;
    for (const auto& device : usbDevices_) {
        deviceIndex++;
        
        // 填充设备信息
        usb_device_info devInfo;
        device->fillDeviceInfo(devInfo);
        
        // 转换为网络字节序
        devInfo.busnum = usbip_utils::htonl_wrap(devInfo.busnum);
        devInfo.devnum = usbip_utils::htonl_wrap(devInfo.devnum);
        devInfo.speed = usbip_utils::htonl_wrap(devInfo.speed);
        devInfo.idVendor = usbip_utils::htons_wrap(devInfo.idVendor);
        devInfo.idProduct = usbip_utils::htons_wrap(devInfo.idProduct);
        devInfo.bcdDevice = usbip_utils::htons_wrap(devInfo.bcdDevice);
        
        // 发送设备基本信息结构
        if (!clientSocket->send(&devInfo, sizeof(devInfo))) {
            std::cerr << "发送设备 " << deviceIndex << " 信息失败" << std::endl;
            return false;
        }
        
        // 发送接口数量
        uint8_t numInterfaces = devInfo.bNumInterfaces;
        if (!clientSocket->send(&numInterfaces, sizeof(numInterfaces))) {
            std::cerr << "发送设备 " << deviceIndex << " 接口数量失败" << std::endl;
            return false;
        }
        
        std::cout << "发送设备 " << deviceIndex << " 的 " << (int)numInterfaces << " 个接口信息" << std::endl;
        
        // 发送每个接口的信息
        for (uint8_t i = 0; i < numInterfaces; i++) {
            // 接口描述 (4字节: 类,子类,协议,填充)
            uint8_t interfaceData[4] = {
                static_cast<uint8_t>((device->isMassStorage()) ? USB_CLASS_MASS_STORAGE : 0), // 类
                0, // 子类
                0, // 协议
                0  // 填充字节
            };
            
            if (!clientSocket->send(interfaceData, sizeof(interfaceData))) {
                std::cerr << "发送设备 " << deviceIndex << " 接口 " << (int)i << " 信息失败" << std::endl;
                return false;
            }
        }
        
        std::cout << "设备 " << deviceIndex << " 信息发送成功" << std::endl;
    }
    
    std::cout << "设备列表响应发送成功" << std::endl;
    return true;
}

bool USBIPServer::handleImportRequest(std::shared_ptr<TCPSocket> clientSocket, const usbip_packet& packet) {
    std::string busID(packet.import_req.busid);
    std::cout << "收到导入设备请求: " << busID << std::endl;
    
    // 打印详细的请求数据进行调试
    std::cout << "导入请求详情: 版本=0x" << std::hex << packet.header.version << std::dec 
              << ", 总线ID=[" << packet.import_req.busid << "]" << std::endl;
    
    // 准备回复数据包
    usbip_packet reply;
    memset(&reply, 0, sizeof(reply)); // 确保所有字段都初始化为0
    
    reply.header.version = USBIP_VERSION;
    reply.header.command = USBIP_OP_REP_IMPORT;
    reply.header.status = 0;
    
    // 初始化导入响应结构体，避免未初始化的值
    reply.import_rep.version = USBIP_VERSION;
    reply.import_rep.status = 0;
    memset(&reply.import_rep.udev, 0, sizeof(reply.import_rep.udev));
    
    // 查找请求的设备
    std::shared_ptr<libusb::USBDevice> targetDevice = nullptr;
    
    {
        std::lock_guard<std::mutex> lock(deviceMutex_);
        for (const auto& device : usbDevices_) {
            std::string deviceBusID = device->getBusID();
            std::cout << "检查设备: " << deviceBusID << std::endl;
            
            if (deviceBusID == busID) {
                std::cout << "找到匹配设备!" << std::endl;
                targetDevice = device;
                break;
            }
        }
    }
    
    if (!targetDevice) {
        std::cerr << "找不到请求的设备: " << busID << std::endl;
        
        // 为失败情况准备响应
        reply.import_rep.version = USBIP_VERSION;
        reply.import_rep.status = -19; // -ENODEV (设备不存在) 的负值
        
        std::cout << "发送导入失败响应，状态=-19 (ENODEV)" << std::endl;
        return clientSocket->sendPacket(reply);
    }
    
    // 设置回复信息
    reply.import_rep.version = USBIP_VERSION;
    reply.import_rep.status = 0; // 成功
    
    // 填充设备信息
    bool fillSuccess = targetDevice->fillDeviceInfo(reply.import_rep.udev);
    if (!fillSuccess) {
        std::cerr << "填充设备信息失败" << std::endl;
        reply.import_rep.status = -22; // -EINVAL (参数无效) 的负值
    } else {
        // 为确保设备信息有效，再次检查关键字段
        if (reply.import_rep.udev.busid[0] == '\0') {
            strncpy(reply.import_rep.udev.busid, busID.c_str(), sizeof(reply.import_rep.udev.busid) - 1);
        }
        
        if (reply.import_rep.udev.idVendor == 0 || reply.import_rep.udev.idProduct == 0) {
            reply.import_rep.udev.idVendor = targetDevice->getVendorID();
            reply.import_rep.udev.idProduct = targetDevice->getProductID();
        }
        
        // 为应答设置网络字节序
        reply.import_rep.udev.busnum = usbip_utils::htonl_wrap(reply.import_rep.udev.busnum);
        reply.import_rep.udev.devnum = usbip_utils::htonl_wrap(reply.import_rep.udev.devnum);
        reply.import_rep.udev.speed = usbip_utils::htonl_wrap(reply.import_rep.udev.speed);
        reply.import_rep.udev.idVendor = usbip_utils::htons_wrap(reply.import_rep.udev.idVendor);
        reply.import_rep.udev.idProduct = usbip_utils::htons_wrap(reply.import_rep.udev.idProduct);
        reply.import_rep.udev.bcdDevice = usbip_utils::htons_wrap(reply.import_rep.udev.bcdDevice);
        
        // 将设备添加到已导出列表
        std::lock_guard<std::mutex> lock(deviceMutex_);
        exportedDevices_[busID] = targetDevice;
        
        std::cout << "成功导出设备 " << busID << std::endl;
        
        // 打印设备信息，便于调试
        std::cout << "===设备详细信息===" << std::endl;
        std::cout << "设备ID: " << reply.import_rep.udev.busid << std::endl;
        std::cout << "路径: " << reply.import_rep.udev.path << std::endl;
        std::cout << "总线号: " << usbip_utils::ntohl_wrap(reply.import_rep.udev.busnum) << std::endl;
        std::cout << "设备号: " << usbip_utils::ntohl_wrap(reply.import_rep.udev.devnum) << std::endl;
        std::cout << "速度: " << usbip_utils::ntohl_wrap(reply.import_rep.udev.speed) << std::endl;
        std::cout << "厂商ID: 0x" << std::hex << usbip_utils::ntohs_wrap(reply.import_rep.udev.idVendor) << std::endl;
        std::cout << "产品ID: 0x" << usbip_utils::ntohs_wrap(reply.import_rep.udev.idProduct) << std::dec << std::endl;
        std::cout << "设备类: " << static_cast<int>(reply.import_rep.udev.bDeviceClass) << std::endl;
        std::cout << "接口数: " << static_cast<int>(reply.import_rep.udev.bNumInterfaces) << std::endl;
        std::cout << "===================" << std::endl;
    }
    
    // 确保回复状态被设置为网络字节序
    reply.import_rep.status = usbip_utils::htonl_wrap(reply.import_rep.status);
    
    // 确保发送正确格式的响应
    std::cout << "发送导入设备响应，状态=" << usbip_utils::ntohl_wrap(reply.import_rep.status) << std::endl;
    return clientSocket->sendPacket(reply);
}

bool USBIPServer::handleURBRequest(std::shared_ptr<TCPSocket> clientSocket, const usbip_packet& packet) {
    uint32_t seqnum = packet.cmd_submit_data.seqnum;
    uint32_t devid = packet.cmd_submit_data.devid;
    uint32_t direction = packet.cmd_submit_data.direction;
    uint32_t ep = packet.cmd_submit_data.ep;
    
    std::cout << "收到URB请求: 序列号=" << seqnum 
              << ", 设备ID=" << devid 
              << ", 方向=" << (direction == USBIP_DIR_IN ? "IN" : "OUT")
              << ", 端点=" << ep << std::endl;
    
    // 准备回复数据包
    usbip_packet reply;
    reply.header.version = USBIP_VERSION;
    reply.header.command = USBIP_RET_SUBMIT;
    reply.header.status = 0;
    
    // 设置回复URB字段
    reply.ret_submit_data.seqnum = seqnum;
    reply.ret_submit_data.devid = devid;
    reply.ret_submit_data.direction = direction;
    reply.ret_submit_data.ep = ep;
    reply.ret_submit_data.status = 0; // 成功
    reply.ret_submit_data.actual_length = 0;
    reply.ret_submit_data.start_frame = 0;
    reply.ret_submit_data.number_of_packets = 0;
    reply.ret_submit_data.error_count = 0;
    
    // 查找导出的设备
    std::shared_ptr<libusb::USBDevice> targetDevice = nullptr;
    std::string busID;
    
    {
        std::lock_guard<std::mutex> lock(deviceMutex_);
        // 在实际实现中，需要根据devid查找设备
        // 这里简化处理，假设只有一个设备
        if (!exportedDevices_.empty()) {
            auto it = exportedDevices_.begin();
            busID = it->first;
            targetDevice = it->second;
        }
    }
    
    if (!targetDevice) {
        std::cerr << "找不到请求的设备" << std::endl;
        reply.ret_submit_data.status = -1; // 错误
        return clientSocket->sendPacket(reply);
    }
    
    // 处理不同类型的传输
    if (ep == 0) {
        // 控制传输
        uint8_t requestType = packet.cmd_submit_data.setup[0];
        uint8_t request = packet.cmd_submit_data.setup[1];
        uint16_t value = (packet.cmd_submit_data.setup[3] << 8) | packet.cmd_submit_data.setup[2];
        uint16_t index = (packet.cmd_submit_data.setup[5] << 8) | packet.cmd_submit_data.setup[4];
        uint16_t length = (packet.cmd_submit_data.setup[7] << 8) | packet.cmd_submit_data.setup[6];
        
        std::cout << "控制传输: requestType=" << (int)requestType 
                  << ", request=" << (int)request
                  << ", value=" << value
                  << ", index=" << index
                  << ", length=" << length << std::endl;
        
        // 分配数据缓冲区
        std::vector<uint8_t> data(length, 0);
        
        // 如果是OUT传输，数据来自客户端
        if (direction == USBIP_DIR_OUT && !packet.data.empty()) {
            data = packet.data;
        }
        
        // 执行控制传输
        int result = targetDevice->controlTransfer(
            requestType, request, value, index, 
            data.data(), length);
        
        if (result < 0) {
            std::cerr << "控制传输失败: " << result << std::endl;
            reply.ret_submit_data.status = result;
        } else {
            // 如果是IN传输，将获取的数据返回给客户端
            if (direction == USBIP_DIR_IN) {
                reply.data = data;
                reply.ret_submit_data.actual_length = result;
            } else {
                reply.ret_submit_data.actual_length = result;
            }
        }
    } else {
        // 批量传输
        int actualLength = 0;
        int result = 0;
        
        if (direction == USBIP_DIR_IN) {
            // 读取数据
            std::vector<uint8_t> data(packet.cmd_submit_data.transfer_buffer_length, 0);
            
            result = targetDevice->bulkTransfer(
                ep | 0x80, // IN端点设置高位
                data.data(), 
                packet.cmd_submit_data.transfer_buffer_length,
                &actualLength);
            
            if (result == 0) {
                // 成功读取数据
                data.resize(actualLength);
                reply.data = data;
                reply.ret_submit_data.actual_length = actualLength;
            }
        } else {
            // 写入数据
            result = targetDevice->bulkTransfer(
                ep, // OUT端点
                const_cast<unsigned char*>(packet.data.data()),
                packet.data.size(),
                &actualLength);
            
            if (result == 0) {
                reply.ret_submit_data.actual_length = actualLength;
            }
        }
        
        if (result != 0) {
            std::cerr << "批量传输失败: " << result << std::endl;
            reply.ret_submit_data.status = result;
        }
    }
    
    // 发送回复
    return clientSocket->sendPacket(reply);
} 