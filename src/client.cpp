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
#include <filesystem>

// VHCI相关常量和路径
#define VHCI_SYSFS_PATH "/sys/devices/platform/vhci_hcd.0"
#define VHCI_ATTACH_PATH "/sys/devices/platform/vhci_hcd.0/attach"
#define VHCI_DETACH_PATH "/sys/devices/platform/vhci_hcd.0/detach"
#define VHCI_NPORTS_PATH "/sys/devices/platform/vhci_hcd.0/nports"

// USB速度常量
#define USB_SPEED_UNKNOWN      0
#define USB_SPEED_LOW          1
#define USB_SPEED_FULL         2
#define USB_SPEED_HIGH         3
#define USB_SPEED_SUPER        4

// VHCI设备实现
VHCIDevice::VHCIDevice()
    : fd_(-1), isCreated_(false), port_(-1) {
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
    
    // 检查sysfs路径是否存在
    struct stat st;
    if (stat(VHCI_SYSFS_PATH, &st) != 0) {
        std::cerr << "找不到vhci_hcd的sysfs路径: " << VHCI_SYSFS_PATH << std::endl;
        return false;
    }
    
    return true;
}

// 查找可用的端口号
int VHCIDevice::findAvailablePort() {
    // 读取可用端口数量
    std::ifstream nports_file(VHCI_NPORTS_PATH);
    if (!nports_file.is_open()) {
        std::cerr << "无法打开nports文件: " << VHCI_NPORTS_PATH << std::endl;
        return -1;
    }
    
    int total_ports = 0;
    nports_file >> total_ports;
    nports_file.close();
    
    std::cout << "vhci_hcd总共有 " << total_ports << " 个端口" << std::endl;
    
    // 检查每个端口的状态
    for (int i = 0; i < total_ports; i++) {
        std::string status_path = std::string(VHCI_SYSFS_PATH) + "/port" + std::to_string(i) + "/status";
        std::ifstream status_file(status_path);
        if (!status_file.is_open()) {
            continue;  // 如果无法打开状态文件，尝试下一个端口
        }
        
        std::string status;
        std::getline(status_file, status);
        status_file.close();
        
        if (status.find("not used") != std::string::npos) {
            std::cout << "找到可用端口: " << i << std::endl;
            return i;
        }
    }
    
    std::cerr << "没有找到可用端口" << std::endl;
    return -1;
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
    
    // 查找可用端口
    port_ = findAvailablePort();
    if (port_ < 0) {
        std::cerr << "没有可用的vhci端口" << std::endl;
        return false;
    }
    
    // 解析busid以获取总线号和设备号
    int busnum = 1;
    int devnum = 5;
    if (!deviceInfo.busid.empty()) {
        std::size_t pos = deviceInfo.busid.find('-');
        if (pos != std::string::npos) {
            try {
                busnum = std::stoi(deviceInfo.busid.substr(0, pos));
                devnum = std::stoi(deviceInfo.busid.substr(pos + 1));
                std::cout << "解析设备ID: 总线号=" << busnum << ", 设备号=" << devnum << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "解析设备ID失败: " << e.what() << std::endl;
            }
        }
    }
    
    // 构建attach命令字符串
    // 格式: "busid port"
    std::string attach_cmd = deviceInfo.busid + " " + std::to_string(port_);
    std::cout << "准备连接设备，命令: " << attach_cmd << std::endl;
    
    // 写入attach文件
    std::ofstream attach_file(VHCI_ATTACH_PATH);
    if (!attach_file.is_open()) {
        std::cerr << "无法打开attach文件: " << VHCI_ATTACH_PATH << " (确保以root权限运行)" << std::endl;
        return false;
    }
    
    attach_file << attach_cmd;
    attach_file.close();
    
    // 检查是否成功连接
    std::string port_status_path = std::string(VHCI_SYSFS_PATH) + "/port" + std::to_string(port_) + "/status";
    std::ifstream status_file(port_status_path);
    if (!status_file.is_open()) {
        std::cerr << "无法打开端口状态文件: " << port_status_path << std::endl;
        return false;
    }
    
    std::string status;
    std::getline(status_file, status);
    status_file.close();
    
    if (status.find("in use") == std::string::npos) {
        std::cerr << "设备连接失败，端口状态: " << status << std::endl;
        return false;
    }
    
    std::cout << "成功创建虚拟USB设备: " << deviceInfo.busid << " 在端口 " << port_ << std::endl;
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

void VHCIDevice::destroy() {
    if (isCreated_ && port_ >= 0) {
        // 断开设备连接
        std::ofstream detach_file(VHCI_DETACH_PATH);
        if (detach_file.is_open()) {
            detach_file << port_;
            detach_file.close();
            std::cout << "已断开端口 " << port_ << " 上的设备连接" << std::endl;
        } else {
            std::cerr << "无法打开detach文件: " << VHCI_DETACH_PATH << std::endl;
        }
    }
    
    isCreated_ = false;
    port_ = -1;
}

bool VHCIDevice::isCreated() const {
    return isCreated_;
}

bool VHCIDevice::handleURBResponse(const usbip_packet& packet) {
    if (!isCreated_ || port_ < 0) {
        std::cerr << "VHCI设备未创建或端口无效，无法处理URB响应" << std::endl;
        return false;
    }
    
    std::cout << "处理URB响应: 序列号=" << packet.ret_submit_data.seqnum
              << ", 状态=" << packet.ret_submit_data.status
              << ", 数据长度=" << packet.ret_submit_data.actual_length << std::endl;
    
    // VHCI驱动通过sysfs接口不支持直接提交URB响应
    // 在实际情况下，USBIP客户端应用程序会接收服务器的响应，然后处理这些响应
    // 我们需要模拟设备的实际存在，但这超出了当前代码的能力
    
    std::cout << "注意: 通过sysfs接口不支持直接提交URB响应。" << std::endl;
    std::cout << "实际使用中，这些响应会由内核中的vhci_hcd驱动自动处理。" << std::endl;
    
    // 我们可以检查URB响应并记录它
    if (packet.ret_submit_data.actual_length > 0) {
        std::cout << "收到数据: " << packet.data.size() << " 字节" << std::endl;
        // 在这里，我们可以根据需要处理数据
    }
    
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
    
    // 确保总线ID有效
    if (busid.empty()) {
        std::cerr << "无效的总线ID（为空）" << std::endl;
        return false;
    }
    
    // 准备请求数据包
    usbip_packet packet;
    memset(&packet, 0, sizeof(packet));  // 确保完全清零
    
    packet.header.version = USBIP_VERSION;
    packet.header.command = USBIP_OP_REQ_IMPORT;
    packet.header.status = 0;
    
    packet.import_req.version = USBIP_VERSION;
    // 确保总线ID正确复制
    strncpy(packet.import_req.busid, busid.c_str(), sizeof(packet.import_req.busid) - 1);
    packet.import_req.busid[sizeof(packet.import_req.busid) - 1] = '\0';  // 确保字符串终止
    
    std::cout << "准备导入设备请求，总线ID: [" << packet.import_req.busid << "]" << std::endl;
    
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
        std::cerr << "收到错误的响应类型: 0x" << std::hex << reply.header.command 
                  << "，期望: 0x" << USBIP_OP_REP_IMPORT << std::dec << std::endl;
        return false;
    }
    
    // 检查头部状态
    if (reply.header.status != 0) {
        std::cerr << "导入设备失败: 头部状态 " << reply.header.status << std::endl;
        return false;
    }
    
    // 检查导入响应状态（由服务端转换为网络字节序）
    int status = usbip_utils::ntohl_wrap(reply.import_rep.status);
    if (status != 0) {
        std::cerr << "导入设备失败: 响应状态 " << status << std::endl;
        return false;
    }
    
    // 将网络字节序转换为主机字节序
    reply.import_rep.udev.busnum = usbip_utils::ntohl_wrap(reply.import_rep.udev.busnum);
    reply.import_rep.udev.devnum = usbip_utils::ntohl_wrap(reply.import_rep.udev.devnum);
    reply.import_rep.udev.speed = usbip_utils::ntohl_wrap(reply.import_rep.udev.speed);
    reply.import_rep.udev.idVendor = usbip_utils::ntohs_wrap(reply.import_rep.udev.idVendor);
    reply.import_rep.udev.idProduct = usbip_utils::ntohs_wrap(reply.import_rep.udev.idProduct);
    reply.import_rep.udev.bcdDevice = usbip_utils::ntohs_wrap(reply.import_rep.udev.bcdDevice);
    
    // 提取设备信息
    USBDeviceInfo deviceInfo;
    deviceInfo.busid = reply.import_rep.udev.busid;
    deviceInfo.path = reply.import_rep.udev.path;
    deviceInfo.idVendor = reply.import_rep.udev.idVendor;
    deviceInfo.idProduct = reply.import_rep.udev.idProduct;
    deviceInfo.bDeviceClass = reply.import_rep.udev.bDeviceClass;
    deviceInfo.isMassStorage = (reply.import_rep.udev.bDeviceClass == USB_CLASS_MASS_STORAGE);
    
    // 打印设备信息，便于调试
    std::cout << "===导入的设备信息===" << std::endl;
    std::cout << "设备ID: " << deviceInfo.busid << std::endl;
    std::cout << "路径: " << deviceInfo.path << std::endl;
    std::cout << "总线号: " << reply.import_rep.udev.busnum << std::endl;
    std::cout << "设备号: " << reply.import_rep.udev.devnum << std::endl;
    std::cout << "速度: " << reply.import_rep.udev.speed << std::endl;
    std::cout << "厂商ID: 0x" << std::hex << deviceInfo.idVendor << std::endl;
    std::cout << "产品ID: 0x" << deviceInfo.idProduct << std::dec << std::endl;
    std::cout << "设备类: " << static_cast<int>(deviceInfo.bDeviceClass) << std::endl;
    std::cout << "接口数: " << static_cast<int>(reply.import_rep.udev.bNumInterfaces) << std::endl;
    std::cout << "===================" << std::endl;
    
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