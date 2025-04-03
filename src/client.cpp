#include "../include/client.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>

// VHCI相关常量和结构体定义
#define USBIP_VHCI_PATH "/dev/usbip-vhci"

// VHCI设备实现
VHCIDevice::VHCIDevice()
    : fd_(-1), isCreated_(false) {
}

VHCIDevice::~VHCIDevice() {
    destroy();
}

bool VHCIDevice::loadVHCIModule() {
    // 检查vhci_hcd模块是否已加载
    std::ifstream lsmod("/proc/modules");
    std::string line;
    bool moduleLoaded = false;
    
    while (std::getline(lsmod, line)) {
        if (line.find("vhci_hcd") != std::string::npos) {
            moduleLoaded = true;
            break;
        }
    }
    
    if (!moduleLoaded) {
        std::cout << "加载vhci_hcd模块..." << std::endl;
        int ret = system("modprobe vhci-hcd");
        if (ret != 0) {
            std::cerr << "加载vhci_hcd模块失败" << std::endl;
            return false;
        }
    }
    
    return true;
}

bool VHCIDevice::create(const USBDeviceInfo& deviceInfo) {
    if (isCreated_) {
        std::cout << "虚拟设备已创建" << std::endl;
        return true;
    }
    
    // 加载vhci_hcd模块
    if (!loadVHCIModule()) {
        return false;
    }
    
    // 记录设备信息
    deviceInfo_ = deviceInfo;
    
    // 打开VHCI设备
    fd_ = open(USBIP_VHCI_PATH, O_RDWR);
    if (fd_ < 0) {
        std::cerr << "打开VHCI设备失败: " << strerror(errno) << std::endl;
        return false;
    }
    
    std::cout << "成功创建虚拟USB设备: " << deviceInfo.busid << std::endl;
    std::cout << "  厂商ID: " << std::hex << deviceInfo.idVendor << std::endl;
    std::cout << "  产品ID: " << std::hex << deviceInfo.idProduct << std::dec << std::endl;
    
    isCreated_ = true;
    return true;
}

void VHCIDevice::destroy() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    
    isCreated_ = false;
}

bool VHCIDevice::isCreated() const {
    return isCreated_;
}

bool VHCIDevice::handleURBResponse(const usbip_packet& packet) {
    // 简单模拟处理，实际实现需要根据VHCI的ioctl接口进行
    std::cout << "处理URB响应: 序列号=" << packet.ret_submit.seqnum
              << ", 状态=" << packet.ret_submit.status
              << ", 数据长度=" << packet.ret_submit.actual_length << std::endl;
    
    // 在实际实现中，这里需要使用ioctl将数据传递给内核VHCI驱动
    // 由于需要访问Linux内核接口，这里仅简单模拟
    
    return true;
}

// USBIPClient实现
USBIPClient::USBIPClient(int port, const std::string& serverHost)
    : serverHost_(serverHost), port_(port), running_(false) {
    // 创建VHCI设备
    virtualDevice_ = std::make_unique<VHCIDevice>();
}

USBIPClient::~USBIPClient() {
    stop();
}

bool USBIPClient::start() {
    // 创建并连接客户端
    client_ = std::make_unique<Client>();
    if (!client_->connect(serverHost_, port_)) {
        std::cerr << "连接服务器失败: " << serverHost_ << ":" << port_ << std::endl;
        return false;
    }
    
    // 获取服务端设备列表
    if (!getDeviceList()) {
        std::cerr << "获取设备列表失败" << std::endl;
        return false;
    }
    
    // 检查是否有可用设备
    {
        std::lock_guard<std::mutex> lock(deviceListMutex_);
        if (deviceList_.empty()) {
            std::cerr << "服务端没有可用的USB设备" << std::endl;
            return false;
        }
        
        // 导入第一个设备
        if (!importDevice(deviceList_[0].busid)) {
            std::cerr << "导入设备失败" << std::endl;
            return false;
        }
    }
    
    // 启动通信线程
    running_ = true;
    commThread_ = std::thread(&USBIPClient::communicationThread, this);
    
    return true;
}

void USBIPClient::stop() {
    if (running_) {
        running_ = false;
        
        if (commThread_.joinable()) {
            commThread_.join();
        }
        
        // 销毁虚拟设备
        std::lock_guard<std::mutex> lock(virtualDeviceMutex_);
        if (virtualDevice_ && virtualDevice_->isCreated()) {
            virtualDevice_->destroy();
        }
        
        // 断开连接
        if (client_) {
            client_->disconnect();
        }
    }
}

bool USBIPClient::getDeviceList() {
    std::cout << "获取服务端设备列表..." << std::endl;
    
    // 准备请求数据包
    usbip_packet packet;
    packet.header.version = USBIP_VERSION;
    packet.header.command = USBIP_OP_REQ_DEVLIST;
    packet.header.status = 0;
    packet.devlist_req.version = USBIP_VERSION;
    
    // 发送请求
    if (!client_->sendPacket(packet)) {
        std::cerr << "发送设备列表请求失败" << std::endl;
        return false;
    }
    
    // 接收响应
    usbip_packet reply;
    if (!client_->receivePacket(reply)) {
        std::cerr << "接收设备列表响应失败" << std::endl;
        return false;
    }
    
    // 检查响应类型
    if (reply.header.command != USBIP_OP_REP_DEVLIST) {
        std::cerr << "收到错误的响应类型: " << reply.header.command << std::endl;
        return false;
    }
    
    // 解析设备列表
    std::lock_guard<std::mutex> lock(deviceListMutex_);
    deviceList_.clear();
    
    // 解析设备数量
    if (reply.data.size() < sizeof(uint32_t)) {
        std::cerr << "设备列表数据不完整" << std::endl;
        return false;
    }
    
    uint32_t numDevices = 0;
    memcpy(&numDevices, reply.data.data(), sizeof(numDevices));
    numDevices = usbip_utils::ntohl_wrap(numDevices);
    
    std::cout << "发现 " << numDevices << " 个设备" << std::endl;
    
    // 解析每个设备信息
    size_t offset = sizeof(uint32_t);
    for (uint32_t i = 0; i < numDevices && offset + sizeof(usb_device_info) <= reply.data.size(); i++) {
        usb_device_info devInfo;
        memcpy(&devInfo, reply.data.data() + offset, sizeof(devInfo));
        offset += sizeof(devInfo);
        
        // 创建设备信息对象
        USBDeviceInfo info;
        info.busid = devInfo.busid;
        info.path = devInfo.path;
        info.idVendor = devInfo.idVendor;
        info.idProduct = devInfo.idProduct;
        info.bDeviceClass = devInfo.bDeviceClass;
        info.isMassStorage = (devInfo.bDeviceClass == 0x08); // 检查是否为大容量存储设备
        
        // 添加到列表
        deviceList_.push_back(info);
        
        // 跳过接口信息
        if (offset < reply.data.size()) {
            uint8_t numInterfaces = reply.data[offset++];
            // 每个接口有4个字节
            offset += numInterfaces * 4;
        }
        
        std::cout << "设备 " << i + 1 << ": " << info.busid
                  << " (VID:" << std::hex << info.idVendor
                  << ", PID:" << info.idProduct << std::dec << ")" << std::endl;
    }
    
    return !deviceList_.empty();
}

bool USBIPClient::importDevice(const std::string& busid) {
    std::cout << "导入设备: " << busid << std::endl;
    
    // 准备请求数据包
    usbip_packet packet;
    packet.header.version = USBIP_VERSION;
    packet.header.command = USBIP_OP_REQ_IMPORT;
    packet.header.status = 0;
    packet.import_req.version = USBIP_VERSION;
    strncpy(packet.import_req.busid, busid.c_str(), sizeof(packet.import_req.busid) - 1);
    
    // 发送请求
    if (!client_->sendPacket(packet)) {
        std::cerr << "发送导入设备请求失败" << std::endl;
        return false;
    }
    
    // 接收响应
    usbip_packet reply;
    if (!client_->receivePacket(reply)) {
        std::cerr << "接收导入设备响应失败" << std::endl;
        return false;
    }
    
    // 检查响应类型
    if (reply.header.command != USBIP_OP_REP_IMPORT) {
        std::cerr << "收到错误的响应类型: " << reply.header.command << std::endl;
        return false;
    }
    
    // 检查状态
    if (reply.header.status != 0) {
        std::cerr << "导入设备失败: 状态 " << reply.header.status << std::endl;
        return false;
    }
    
    // 提取设备信息
    USBDeviceInfo deviceInfo;
    deviceInfo.busid = reply.import_rep.udev.busid;
    deviceInfo.path = reply.import_rep.udev.path;
    deviceInfo.idVendor = reply.import_rep.udev.idVendor;
    deviceInfo.idProduct = reply.import_rep.udev.idProduct;
    deviceInfo.bDeviceClass = reply.import_rep.udev.bDeviceClass;
    deviceInfo.isMassStorage = (reply.import_rep.udev.bDeviceClass == 0x08);
    
    // 创建虚拟设备
    std::lock_guard<std::mutex> lock(virtualDeviceMutex_);
    if (!virtualDevice_->create(deviceInfo)) {
        std::cerr << "创建虚拟设备失败" << std::endl;
        return false;
    }
    
    std::cout << "成功导入设备: " << deviceInfo.busid << std::endl;
    return true;
}

void USBIPClient::communicationThread() {
    std::cout << "通信线程启动" << std::endl;
    
    // 简单模拟URB提交
    // 在实际实现中，应该监听VHCI设备的请求，并转发给服务端
    while (running_) {
        // 这里应该从VHCI设备读取URB请求，然后发送给服务端
        // 由于需要访问Linux内核接口，这里仅简单模拟
        
        // 假设收到了服务端的URB响应
        usbip_packet packet;
        
        // 尝试接收服务端数据
        if (client_->receivePacket(packet)) {
            // 处理响应
            std::lock_guard<std::mutex> lock(virtualDeviceMutex_);
            if (virtualDevice_ && virtualDevice_->isCreated()) {
                virtualDevice_->handleURBResponse(packet);
            }
        } else {
            // 如果接收失败，可能是连接断开，尝试重新连接
            std::cerr << "与服务端的连接断开，尝试重新连接..." << std::endl;
            
            // 重连逻辑...
            break;
        }
    }
    
    std::cout << "通信线程结束" << std::endl;
} 