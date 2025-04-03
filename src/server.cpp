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
                
            default:
                std::cerr << "未知命令: " << packet.header.command << std::endl;
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
    reply.header.version = USBIP_VERSION;
    reply.header.command = USBIP_OP_REP_DEVLIST;
    reply.header.status = 0;
    
    std::cout << "准备发送回复数据包: 版本=" << std::hex << reply.header.version 
              << ", 命令=" << reply.header.command
              << ", 状态=" << reply.header.status << std::dec << std::endl;
    
    // 设备列表数据
    std::vector<uint8_t> deviceData;
    
    // 添加设备数量
    uint32_t numDevices = usbDevices_.size();
    std::cout << "设备列表包含 " << numDevices << " 个设备" << std::endl;
    
    uint32_t numDevicesNetwork = usbip_utils::htonl_wrap(numDevices);
    deviceData.insert(deviceData.end(), (uint8_t*)&numDevicesNetwork, (uint8_t*)&numDevicesNetwork + sizeof(numDevicesNetwork));
    
    std::cout << "设备列表数据头部大小: " << deviceData.size() << " 字节" << std::endl;
    
    // 添加每个设备的信息
    std::lock_guard<std::mutex> lock(deviceMutex_);
    int deviceIndex = 0;
    for (const auto& device : usbDevices_) {
        deviceIndex++;
        
        // 打印设备基本信息
        std::cout << "设备 " << deviceIndex << ": VID=" << std::hex << device->getVendorID() 
                  << ", PID=" << device->getProductID() 
                  << ", BusID=" << std::dec << device->getBusID() 
                  << ", 是否为存储设备: " << (device->isMassStorage() ? "是" : "否") << std::endl;
        
        // 设备信息
        usb_device_info devInfo;
        device->fillDeviceInfo(devInfo);
        
        size_t oldSize = deviceData.size();
        
        // 将设备信息添加到数据中
        deviceData.insert(deviceData.end(), (uint8_t*)&devInfo, (uint8_t*)&devInfo + sizeof(devInfo));
        
        std::cout << "添加设备 " << deviceIndex << " 的基本信息: " << sizeof(devInfo) << " 字节" << std::endl;
        
        // 接口信息
        uint8_t numInterfaces = devInfo.bNumInterfaces;
        deviceData.insert(deviceData.end(), &numInterfaces, &numInterfaces + 1);
        
        std::cout << "设备 " << deviceIndex << " 有 " << (int)numInterfaces << " 个接口" << std::endl;
        
        // 每个接口的信息
        for (uint8_t i = 0; i < numInterfaces; i++) {
            // 接口类，子类和协议（使用通用值）
            uint8_t interfaceClass = (device->isMassStorage()) ? USB_CLASS_MASS_STORAGE : 0;
            uint8_t interfaceSubClass = 0;
            uint8_t interfaceProtocol = 0;
            
            deviceData.push_back(interfaceClass);
            deviceData.push_back(interfaceSubClass);
            deviceData.push_back(interfaceProtocol);
            
            // 填充保留字段
            uint8_t padding = 0;
            deviceData.push_back(padding);
            
            std::cout << "接口 " << (int)i << ": 类=" << (int)interfaceClass 
                      << ", 子类=" << (int)interfaceSubClass
                      << ", 协议=" << (int)interfaceProtocol << std::endl;
        }
        
        std::cout << "设备 " << deviceIndex << " 添加了 " << (deviceData.size() - oldSize) << " 字节数据" << std::endl;
    }
    
    // 设置回复数据
    reply.data = deviceData;
    
    std::cout << "设备列表总数据大小: " << deviceData.size() << " 字节" << std::endl;
    
    // 打印前40个字节的十六进制表示（如果可用）
    if (!deviceData.empty()) {
        std::cout << "设备列表数据前 " << std::min(40, static_cast<int>(deviceData.size())) << " 字节: ";
        for (int i = 0; i < std::min(40, static_cast<int>(deviceData.size())); i++) {
            std::cout << std::hex << std::setfill('0') << std::setw(2) 
                      << static_cast<int>(deviceData[i]) << " ";
        }
        std::cout << std::dec << std::endl;
    }
    
    // 发送回复
    std::cout << "开始发送设备列表响应..." << std::endl;
    bool success = clientSocket->sendPacket(reply);
    std::cout << "设备列表响应发送" << (success ? "成功" : "失败") << std::endl;
    return success;
}

bool USBIPServer::handleImportRequest(std::shared_ptr<TCPSocket> clientSocket, const usbip_packet& packet) {
    std::string busID(packet.import_req.busid);
    std::cout << "收到导入设备请求: " << busID << std::endl;
    
    // 准备回复数据包
    usbip_packet reply;
    reply.header.version = USBIP_VERSION;
    reply.header.command = USBIP_OP_REP_IMPORT;
    reply.header.status = 0;
    
    // 查找请求的设备
    std::shared_ptr<libusb::USBDevice> targetDevice = nullptr;
    
    {
        std::lock_guard<std::mutex> lock(deviceMutex_);
        for (const auto& device : usbDevices_) {
            if (device->getBusID() == busID) {
                targetDevice = device;
                break;
            }
        }
    }
    
    if (!targetDevice) {
        std::cerr << "找不到请求的设备: " << busID << std::endl;
        reply.header.status = 1; // 错误
        return clientSocket->sendPacket(reply);
    }
    
    // 设置回复状态为成功
    reply.header.status = 0;
    
    // 填充设备信息
    targetDevice->fillDeviceInfo(reply.import_rep.udev);
    
    // 将设备添加到已导出列表
    {
        std::lock_guard<std::mutex> lock(deviceMutex_);
        exportedDevices_[busID] = targetDevice;
    }
    
    // 发送回复
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