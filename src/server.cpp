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
        std::cout << "服务端已停止" << std::endl;
    }
}

bool USBIPServer::scanUSBDevices() {
    std::lock_guard<std::mutex> lock(deviceMutex_);
    
    // 创建USB设备管理器
    libusb::USBDeviceManager deviceManager;
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
    std::cout << "收到设备列表请求" << std::endl;
    
    // 扫描设备
    scanUSBDevices();
    
    // 准备回复数据包
    usbip_packet reply;
    reply.header.version = USBIP_VERSION;
    reply.header.command = USBIP_OP_REP_DEVLIST;
    reply.header.status = 0;
    
    // 设备列表数据
    std::vector<uint8_t> deviceData;
    
    // 添加设备数量
    uint32_t numDevices = usbip_utils::htonl_wrap(usbDevices_.size());
    deviceData.insert(deviceData.end(), (uint8_t*)&numDevices, (uint8_t*)&numDevices + sizeof(numDevices));
    
    // 添加每个设备的信息
    std::lock_guard<std::mutex> lock(deviceMutex_);
    for (const auto& device : usbDevices_) {
        // 设备信息
        usb_device_info devInfo;
        device->fillDeviceInfo(devInfo);
        
        // 将设备信息添加到数据中
        deviceData.insert(deviceData.end(), (uint8_t*)&devInfo, (uint8_t*)&devInfo + sizeof(devInfo));
        
        // 接口信息（简化处理）
        uint8_t numInterfaces = devInfo.bNumInterfaces;
        deviceData.insert(deviceData.end(), &numInterfaces, &numInterfaces + 1);
        
        // 每个接口的信息（这里简化处理，实际应该根据设备获取）
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
        }
    }
    
    // 设置回复数据
    reply.data = deviceData;
    
    // 发送回复
    return clientSocket->sendPacket(reply);
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
    uint32_t seqnum = packet.cmd_submit.seqnum;
    uint32_t devid = packet.cmd_submit.devid;
    uint32_t direction = packet.cmd_submit.direction;
    uint32_t ep = packet.cmd_submit.ep;
    
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
    reply.ret_submit.seqnum = seqnum;
    reply.ret_submit.devid = devid;
    reply.ret_submit.direction = direction;
    reply.ret_submit.ep = ep;
    reply.ret_submit.status = 0; // 成功
    reply.ret_submit.actual_length = 0;
    reply.ret_submit.start_frame = 0;
    reply.ret_submit.number_of_packets = 0;
    reply.ret_submit.error_count = 0;
    
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
        reply.ret_submit.status = -1; // 错误
        return clientSocket->sendPacket(reply);
    }
    
    // 处理不同类型的传输
    if (ep == 0) {
        // 控制传输
        uint8_t requestType = packet.cmd_submit.setup[0];
        uint8_t request = packet.cmd_submit.setup[1];
        uint16_t value = (packet.cmd_submit.setup[3] << 8) | packet.cmd_submit.setup[2];
        uint16_t index = (packet.cmd_submit.setup[5] << 8) | packet.cmd_submit.setup[4];
        uint16_t length = (packet.cmd_submit.setup[7] << 8) | packet.cmd_submit.setup[6];
        
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
            reply.ret_submit.status = result;
        } else {
            // 如果是IN传输，将获取的数据返回给客户端
            if (direction == USBIP_DIR_IN) {
                reply.data = data;
                reply.ret_submit.actual_length = result;
            } else {
                reply.ret_submit.actual_length = result;
            }
        }
    } else {
        // 批量传输
        int actualLength = 0;
        int result = 0;
        
        if (direction == USBIP_DIR_IN) {
            // 读取数据
            std::vector<uint8_t> data(packet.cmd_submit.transfer_buffer_length, 0);
            
            result = targetDevice->bulkTransfer(
                ep | 0x80, // IN端点设置高位
                data.data(), 
                packet.cmd_submit.transfer_buffer_length,
                &actualLength);
            
            if (result == 0) {
                // 成功读取数据
                data.resize(actualLength);
                reply.data = data;
                reply.ret_submit.actual_length = actualLength;
            }
        } else {
            // 写入数据
            result = targetDevice->bulkTransfer(
                ep, // OUT端点
                const_cast<unsigned char*>(packet.data.data()),
                packet.data.size(),
                &actualLength);
            
            if (result == 0) {
                reply.ret_submit.actual_length = actualLength;
            }
        }
        
        if (result != 0) {
            std::cerr << "批量传输失败: " << result << std::endl;
            reply.ret_submit.status = result;
        }
    }
    
    // 发送回复
    return clientSocket->sendPacket(reply);
} 