#include "../include/client.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctime>

// VHCI相关常量和结构体定义
#define USBIP_VHCI_PATH "/dev/vhci"  // 主要VHCI设备路径
#define USB_VHCI_PATH "/dev/usb-vhci/usb-vhci"  // 备用VHCI设备路径

// USB速度常量
#define USB_SPEED_UNKNOWN      0
#define USB_SPEED_LOW          1
#define USB_SPEED_FULL         2
#define USB_SPEED_HIGH         3
#define USB_SPEED_SUPER        4

// 自定义IOCTL命令，可能需要根据您的系统调整
#define USBIP_VHCI_IOCATTACH _IOW('U', 0, struct usbip_vhci_device)
#define USB_VHCI_IOCATTACH _IOW('U', 0, struct usb_vhci_device_info)

// USBIP设备连接结构
struct usbip_vhci_device {
    uint8_t port;
    uint8_t status;
    uint32_t speed;
    uint16_t devid;
    uint16_t busnum;
    uint16_t devnum;
    uint32_t vendor;
    uint32_t product;
};

// USB-VHCI设备连接结构
struct usb_vhci_device_info {
    uint8_t port;
    uint8_t status;
    uint32_t speed;
    uint16_t vendor;
    uint16_t product;
    uint16_t device;
    uint8_t dev_class;
    uint8_t dev_subclass;
    uint8_t dev_protocol;
};

// VHCI设备实现
VHCIDevice::VHCIDevice()
    : fd_(-1), isCreated_(false) {
}

VHCIDevice::~VHCIDevice() {
    destroy();
}

bool VHCIDevice::loadVHCIModule() {
    // 检查vhci_hcd和usb_vhci_hcd模块是否已加载
    std::ifstream lsmod("/proc/modules");
    std::string line;
    bool moduleLoaded = false;
    
    while (std::getline(lsmod, line)) {
        if (line.find("vhci_hcd") != std::string::npos || 
            line.find("usb_vhci_hcd") != std::string::npos) {
            moduleLoaded = true;
            break;
        }
    }
    
    if (!moduleLoaded) {
        std::cout << "加载vhci_hcd模块..." << std::endl;
        int ret = system("modprobe vhci-hcd");
        if (ret != 0) {
            std::cerr << "加载vhci_hcd模块失败，尝试加载usb_vhci_hcd..." << std::endl;
            ret = system("modprobe usb_vhci_hcd");
            if (ret != 0) {
                std::cerr << "加载usb_vhci_hcd模块也失败" << std::endl;
                return false;
            }
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
    
    // 尝试打开主要的VHCI设备
    fd_ = open(USBIP_VHCI_PATH, O_RDWR);
    if (fd_ < 0) {
        std::cerr << "打开主要VHCI设备失败: " << strerror(errno) << std::endl;
        
        // 尝试打开备用USB-VHCI设备
        fd_ = open(USB_VHCI_PATH, O_RDWR);
        if (fd_ < 0) {
            std::cerr << "打开备用USB-VHCI设备也失败: " << strerror(errno) << std::endl;
            
            // 检查设备文件是否存在及其权限
            struct stat st;
            if (stat(USBIP_VHCI_PATH, &st) == 0) {
                std::cerr << USBIP_VHCI_PATH << " 文件存在，但可能没有足够权限访问，请确保以root权限运行" << std::endl;
            } else {
                std::cerr << USBIP_VHCI_PATH << " 文件不存在，请确保已安装并加载正确的内核模块" << std::endl;
            }
            
            return false;  // 不能模拟，必须创建真实设备
        } else {
            std::cout << "成功打开备用USB-VHCI设备" << std::endl;
        }
    } else {
        std::cout << "成功打开主要VHCI设备" << std::endl;
    }
    
    // 根据打开的设备类型选择不同的IOCTL命令
    bool success = false;
    
    // 尝试使用USBIP_VHCI连接设备（针对/dev/vhci）
    if (fd_ >= 0) {
        // 准备USBIP_VHCI设备连接参数
        struct usbip_vhci_device attach_data;
        memset(&attach_data, 0, sizeof(attach_data));
        
        attach_data.port = 0;  // 使用第一个可用端口
        attach_data.status = 1; // 1表示已连接
        attach_data.speed = 2;  // USB 2.0高速
        attach_data.devid = 1;  // 设备ID为1
        attach_data.busnum = 1; // 总线号为1
        attach_data.devnum = 2; // 设备号为2
        attach_data.vendor = deviceInfo.idVendor;
        attach_data.product = deviceInfo.idProduct;
        
        std::cout << "尝试使用USBIP_VHCI_IOCATTACH连接设备..." << std::endl;
        int ret = ioctl(fd_, USBIP_VHCI_IOCATTACH, &attach_data);
        
        if (ret < 0) {
            std::cerr << "USBIP_VHCI_IOCATTACH失败: " << strerror(errno) << std::endl;
            
            // 尝试使用USB_VHCI连接设备（针对/dev/usb-vhci/usb-vhci）
            struct usb_vhci_device_info vhci_data;
            memset(&vhci_data, 0, sizeof(vhci_data));
            
            vhci_data.port = 0;  // 使用第一个可用端口
            vhci_data.status = 1; // 1表示已连接
            vhci_data.speed = 2;  // USB 2.0高速
            vhci_data.vendor = deviceInfo.idVendor;
            vhci_data.product = deviceInfo.idProduct;
            vhci_data.device = 0x0100;  // 设备版本1.0
            vhci_data.dev_class = deviceInfo.bDeviceClass;
            vhci_data.dev_subclass = 0;
            vhci_data.dev_protocol = 0;
            
            std::cout << "尝试使用USB_VHCI_IOCATTACH连接设备..." << std::endl;
            ret = ioctl(fd_, USB_VHCI_IOCATTACH, &vhci_data);
            
            if (ret < 0) {
                std::cerr << "USB_VHCI_IOCATTACH也失败: " << strerror(errno) << std::endl;
                
                // 尝试发送基本的调试信息到驱动
                int debug_cmd = _IO('U', 0xFF);  // 自定义调试命令
                ret = ioctl(fd_, debug_cmd, NULL);
                std::cerr << "尝试调试命令结果: " << ret << ", " << strerror(errno) << std::endl;
                
                close(fd_);
                fd_ = -1;
                return false;
            } else {
                success = true;
                std::cout << "USB_VHCI_IOCATTACH成功连接设备" << std::endl;
            }
        } else {
            success = true;
            std::cout << "USBIP_VHCI_IOCATTACH成功连接设备" << std::endl;
        }
    }
    
    if (success) {
        std::cout << "成功创建虚拟USB设备: " << deviceInfo.busid << std::endl;
        std::cout << "  厂商ID: 0x" << std::hex << deviceInfo.idVendor << std::endl;
        std::cout << "  产品ID: 0x" << std::hex << deviceInfo.idProduct << std::dec << std::endl;
        if (deviceInfo.isMassStorage) {
            std::cout << "  设备类型: 大容量存储设备" << std::endl;
        } else {
            std::cout << "  设备类型: 类代码 0x" << std::hex << static_cast<int>(deviceInfo.bDeviceClass) << std::dec << std::endl;
        }
        
        isCreated_ = true;
        return true;
    }
    
    return false;
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
    if (fd_ < 0 || !isCreated_) {
        std::cerr << "VHCI设备未创建或文件描述符无效，无法处理URB响应" << std::endl;
        return false;
    }
    
    std::cout << "处理URB响应: 序列号=" << packet.ret_submit_data.seqnum
              << ", 状态=" << packet.ret_submit_data.status
              << ", 数据长度=" << packet.ret_submit_data.actual_length << std::endl;
    
    // 定义VHCI响应结构
    struct vhci_response {
        uint32_t seqnum;
        uint32_t devid;
        uint32_t direction;
        uint32_t ep;
        uint32_t status;
        uint32_t actual_length;
        uint32_t start_frame;
        uint32_t number_of_packets;
        uint32_t error_count;
        char setup[8];
        char data[0];  // 可变长度数据
    };
    
    // 计算所需的总大小
    size_t resp_size = sizeof(vhci_response) + packet.data.size();
    
    // 分配内存
    vhci_response* resp = (vhci_response*)malloc(resp_size);
    if (!resp) {
        std::cerr << "无法分配内存用于URB响应" << std::endl;
        return false;
    }
    
    // 填充响应结构
    memset(resp, 0, resp_size);
    resp->seqnum = packet.ret_submit_data.seqnum;
    resp->devid = packet.ret_submit_data.devid;
    resp->direction = packet.ret_submit_data.direction;
    resp->ep = packet.ret_submit_data.ep;
    resp->status = packet.ret_submit_data.status;
    resp->actual_length = packet.ret_submit_data.actual_length;
    resp->start_frame = packet.ret_submit_data.start_frame;
    resp->number_of_packets = packet.ret_submit_data.number_of_packets;
    resp->error_count = packet.ret_submit_data.error_count;
    
    // 复制数据(如果有)
    if (packet.data.size() > 0) {
        memcpy(resp->data, packet.data.data(), packet.data.size());
    }
    
    // 定义IOCTL命令
    #define USBIP_VHCI_IOCSUBMIT_RESP _IOW('U', 1, struct vhci_response)
    #define USB_VHCI_IOCGIVEBACK _IOW('U', 1, struct vhci_response)
    
    // 发送响应到VHCI驱动
    int ret = ioctl(fd_, USBIP_VHCI_IOCSUBMIT_RESP, resp);
    if (ret < 0) {
        std::cerr << "USBIP_VHCI_IOCSUBMIT_RESP失败: " << strerror(errno) << std::endl;
        
        // 尝试备用命令
        ret = ioctl(fd_, USB_VHCI_IOCGIVEBACK, resp);
        if (ret < 0) {
            std::cerr << "USB_VHCI_IOCGIVEBACK也失败: " << strerror(errno) << std::endl;
            free(resp);
            return false;
        }
    }
    
    std::cout << "成功将URB响应发送到VHCI驱动" << std::endl;
    
    // 释放内存
    free(resp);
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
    
    std::cout << "已发送设备列表请求，等待响应..." << std::endl;
    
    // 接收响应
    usbip_packet reply;
    if (!client_->receivePacket(reply)) {
        std::cerr << "接收设备列表响应失败" << std::endl;
        return false;
    }
    
    std::cout << "收到响应数据包，大小: " << reply.data.size() << " 字节" << std::endl;
    std::cout << "响应头部: 版本=" << std::hex << reply.header.version 
              << ", 命令=" << reply.header.command 
              << ", 状态=" << reply.header.status << std::dec << std::endl;
    
    // 检查响应类型
    if (reply.header.command != USBIP_OP_REP_DEVLIST) {
        std::cerr << "收到错误的响应类型: " << std::hex << reply.header.command 
                  << "，期望: " << USBIP_OP_REP_DEVLIST << std::dec << std::endl;
        return false;
    }
    
    // 检查状态码
    if (reply.header.status != 0) {
        std::cerr << "响应状态码错误: " << reply.header.status << std::endl;
        return false;
    }
    
    // 解析设备列表
    std::lock_guard<std::mutex> lock(deviceListMutex_);
    deviceList_.clear();
    
    // 打印收到的原始数据前32字节（调试用）
    std::cout << "收到的设备列表原始数据前32字节: ";
    for (size_t i = 0; i < reply.data.size() && i < 32; ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(reply.data[i]) << " ";
    }
    std::cout << std::dec << std::endl;
    
    // 解析设备数量
    if (reply.data.size() < sizeof(uint32_t)) {
        std::cerr << "设备列表数据不完整: 需要至少 " << sizeof(uint32_t) 
                  << " 字节来包含设备数量，但只收到 " << reply.data.size() << " 字节" << std::endl;
        
        // 打印收到的原始数据（十六进制）
        std::cerr << "收到的原始数据: ";
        for (size_t i = 0; i < reply.data.size() && i < 32; ++i) {
            std::cerr << std::hex << std::setw(2) << std::setfill('0') 
                      << static_cast<int>(reply.data[i]) << " ";
        }
        std::cerr << std::dec << std::endl;
        
        return false;
    }
    
    // 直接使用第一个字节作为设备数量 - 根据日志分析这是正确的方法
    // 01 00 00 00 表示设备数量为1
    uint32_t numDevices = reply.data[0];
    
    std::cout << "设备列表中包含 " << numDevices << " 个设备" << std::endl;
    
    // 计算预期的数据大小
    size_t expectedSize = sizeof(uint32_t); // 设备数量字段
    
    // 检查数据是否足够
    if (numDevices > 0) {
        expectedSize += numDevices * sizeof(usb_device_info); // 每个设备的基本信息
        
        if (reply.data.size() < expectedSize) {
            std::cerr << "设备信息数据不完整: 需要至少 " << expectedSize 
                      << " 字节，但只收到 " << reply.data.size() << " 字节" << std::endl;
            return false;
        }
    }
    
    // 解析每个设备信息
    size_t offset = sizeof(uint32_t);
    for (uint32_t i = 0; i < numDevices && offset + sizeof(usb_device_info) <= reply.data.size(); i++) {
        usb_device_info devInfo;
        memcpy(&devInfo, reply.data.data() + offset, sizeof(devInfo));
        offset += sizeof(devInfo);
        
        std::cout << "解析设备 " << i + 1 << " 信息，偏移量: " << offset << std::endl;
        
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
            std::cout << "设备 " << i + 1 << " 有 " << static_cast<int>(numInterfaces) << " 个接口" << std::endl;
            
            // 检查是否有足够的数据来包含所有接口
            size_t interfacesSize = numInterfaces * 4; // 每个接口4字节
            if (offset + interfacesSize > reply.data.size()) {
                std::cerr << "接口信息数据不完整: 需要 " << interfacesSize 
                          << " 字节，但只剩余 " << (reply.data.size() - offset) << " 字节" << std::endl;
                // 继续处理已有数据，不中断
            }
            
            // 每个接口有4个字节
            offset += numInterfaces * 4;
        }
        
        std::cout << "设备 " << i + 1 << ": " << info.busid
                  << " (VID:" << std::hex << info.idVendor
                  << ", PID:" << info.idProduct << std::dec << ")" << std::endl;
    }
    
    if (deviceList_.empty()) {
        std::cerr << "未找到可用设备" << std::endl;
        return false;
    }
    
    std::cout << "成功解析设备列表，找到 " << deviceList_.size() << " 个设备" << std::endl;
    return true;
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
    std::cout << "通信线程启动，等待USB请求和响应..." << std::endl;
    
    // 为信号处理准备
    bool localRunning = true;
    
    // 设置定期检查退出标志和发送请求
    const int TIMEOUT_SECONDS = 2;  // 2秒超时
    int noDataCount = 0;
    const int MAX_NO_DATA = 5;      // 5次无数据后提示用户
    int requestInterval = 0;        // 请求间隔计数器
    
    while (running_ && localRunning) {
        try {
            // 检查是否有退出请求
            if (!running_) {
                std::cout << "收到退出请求，通信线程终止" << std::endl;
                break;
            }
            
            // 每隔一段时间主动向服务端发送一个URB请求
            if (requestInterval <= 0) {
                // 创建一个测试URB请求（控制传输）
                usbip_packet request;
                request.header.version = USBIP_VERSION;
                request.header.command = USBIP_CMD_SUBMIT;
                request.header.status = 0;
                
                request.cmd_submit_data.seqnum = static_cast<uint32_t>(time(nullptr)); // 使用时间戳作为序列号
                request.cmd_submit_data.devid = 1; // 假设设备ID为1
                request.cmd_submit_data.direction = USBIP_DIR_IN; // IN方向，从设备读取数据
                request.cmd_submit_data.ep = 0; // 端点0（控制传输端点）
                request.cmd_submit_data.transfer_flags = 0;
                request.cmd_submit_data.transfer_buffer_length = 8; // 请求8字节数据
                request.cmd_submit_data.start_frame = 0;
                request.cmd_submit_data.number_of_packets = 0;
                request.cmd_submit_data.interval = 0;
                
                // 设置控制传输的标准请求
                // Setup包: bmRequestType(1) bRequest(1) wValue(2) wIndex(2) wLength(2)
                request.cmd_submit_data.setup[0] = 0x80; // 设备到主机，标准请求，设备接收方
                request.cmd_submit_data.setup[1] = 0x06; // GET_DESCRIPTOR请求
                request.cmd_submit_data.setup[2] = 0x01; // 描述符类型：设备描述符
                request.cmd_submit_data.setup[3] = 0x00;
                request.cmd_submit_data.setup[4] = 0x00; // wIndex = 0
                request.cmd_submit_data.setup[5] = 0x00;
                request.cmd_submit_data.setup[6] = 0x08; // wLength = 8
                request.cmd_submit_data.setup[7] = 0x00;
                
                std::cout << "向服务端发送URB请求，序列号: " << request.cmd_submit_data.seqnum << std::endl;
                if (client_->sendPacket(request)) {
                    std::cout << "URB请求发送成功，等待响应..." << std::endl;
                    requestInterval = 10; // 10次循环后再次发送请求（约2秒）
                } else {
                    std::cerr << "发送URB请求失败" << std::endl;
                }
            } else {
                requestInterval--;
            }
            
            // 尝试接收服务端数据，使用超时方式
            std::cout << "等待服务端数据，超时时间 " << TIMEOUT_SECONDS << " 秒..." << std::endl;
            usbip_packet packet;
            if (client_->receivePacketWithTimeout(packet, TIMEOUT_SECONDS)) {
                noDataCount = 0; // 重置无数据计数器
                
                // 处理响应
                std::lock_guard<std::mutex> lock(virtualDeviceMutex_);
                if (virtualDevice_ && virtualDevice_->isCreated()) {
                    virtualDevice_->handleURBResponse(packet);
                }
            } else {
                // 超时但没有接收到数据
                noDataCount++;
                
                if (noDataCount >= MAX_NO_DATA) {
                    std::cout << "长时间未收到服务端数据，但连接仍然保持..." << std::endl;
                    std::cout << "您可以随时按 Ctrl+C 停止客户端" << std::endl;
                    noDataCount = 0; // 重置计数器
                }
                
                // 休眠一小段时间，避免过度消耗CPU
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        } catch (const std::exception& e) {
            std::cerr << "通信线程捕获到异常: " << e.what() << std::endl;
            // 发生异常但继续运行，除非接收到停止信号
        } catch (...) {
            std::cerr << "通信线程捕获到未知异常" << std::endl;
            // 发生异常但继续运行，除非接收到停止信号
        }
    }
    
    std::cout << "通信线程结束" << std::endl;
} 