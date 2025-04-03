#ifndef CLIENT_H
#define CLIENT_H

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <map>
#include "network.h"
#include "usbip_protocol.h"

// USB设备信息结构
struct USBDeviceInfo {
    std::string busid;
    std::string path;
    uint16_t idVendor;
    uint16_t idProduct;
    std::string manufacturer;
    std::string product;
    uint8_t bDeviceClass;
    bool isMassStorage;
};

// 虚拟USB设备接口
class VirtualUSBDevice {
public:
    virtual ~VirtualUSBDevice() = default;
    
    // 创建虚拟设备
    virtual bool create(const USBDeviceInfo& deviceInfo) = 0;
    
    // 销毁虚拟设备
    virtual void destroy() = 0;
    
    // 检查设备是否创建成功
    virtual bool isCreated() const = 0;
    
    // 处理URB返回
    virtual bool handleURBResponse(const usbip_packet& packet) = 0;
};

// VHCI设备实现
class VHCIDevice : public VirtualUSBDevice {
public:
    VHCIDevice();
    ~VHCIDevice() override;
    
    bool create(const USBDeviceInfo& deviceInfo) override;
    void destroy() override;
    bool isCreated() const override;
    bool handleURBResponse(const usbip_packet& packet) override;
    
    // 设置服务器地址
    void setServerHost(const std::string& host) { serverHost_ = host; }
    
private:
    int fd_;   // VHCI设备文件描述符
    bool isCreated_;
    USBDeviceInfo deviceInfo_;
    int port_;  // vhci端口号
    std::string serverHost_; // 服务器地址
    
    // 检查并加载vhci_hcd模块
    bool loadVHCIModule();
    
    // 查找可用的端口号
    int findAvailablePort();
};

class USBIPClient {
public:
    explicit USBIPClient(int port, const std::string& serverHost = "127.0.0.1");
    ~USBIPClient();
    
    // 启动客户端
    bool start();
    
    // 停止客户端
    void stop();
    
    // 检查客户端是否正在运行
    bool isRunning() const { return running_; }
    
private:
    // 获取服务端设备列表
    bool getDeviceList();
    
    // 导入并创建虚拟设备
    bool importDevice(const std::string& busid);
    
    // 通信线程
    void communicationThread();
    
    // 客户端变量
    std::string serverHost_;
    int port_;
    std::unique_ptr<Client> client_;
    std::atomic<bool> running_;
    std::thread commThread_;
    
    // 已发现的设备列表
    std::vector<USBDeviceInfo> deviceList_;
    std::mutex deviceListMutex_;
    
    // 已导入的设备
    std::unique_ptr<VirtualUSBDevice> virtualDevice_;
    std::mutex virtualDeviceMutex_;
};

#endif // CLIENT_H 