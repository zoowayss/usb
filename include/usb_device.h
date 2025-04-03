#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <functional>
#include <libusb.h>
#include "usbip_protocol.h"

namespace libusb {

// USB设备类，封装libusb操作
class USBDevice {
public:
    USBDevice(libusb_device* device);
    ~USBDevice();
    
    // 初始化设备（打开设备）
    bool open();
    void close();
    
    // 获取设备描述符
    bool getDeviceDescriptor(usb_device_descriptor& desc);
    
    // 获取配置描述符
    bool getConfigDescriptor(uint8_t configIndex, std::vector<uint8_t>& configData);
    
    // 执行控制传输
    int controlTransfer(uint8_t requestType, uint8_t request, 
                         uint16_t value, uint16_t index,
                         uint8_t* data, uint16_t length, 
                         unsigned int timeout = 1000);
    
    // 执行批量传输
    int bulkTransfer(unsigned char endpoint, 
                     unsigned char* data, 
                     int length, 
                     int* actualLength,
                     unsigned int timeout = 1000);
    
    // 执行中断传输
    int interruptTransfer(unsigned char endpoint, 
                         unsigned char* data, 
                         int length, 
                         int* actualLength,
                         unsigned int timeout = 1000);
    
    // 获取设备信息
    std::string getBusID() const;
    uint8_t getBusNumber() const;
    uint8_t getDeviceAddress() const;
    uint16_t getVendorID() const;
    uint16_t getProductID() const;
    uint8_t getDeviceClass() const;
    
    // 检查设备是否为大容量存储设备（U盘）
    bool isMassStorage() const;
    
    // 填充USBIP设备信息结构
    bool fillDeviceInfo(usb_device_info& info);
    
private:
    libusb_device* device_;
    libusb_device_handle* handle_;
    libusb_device_descriptor deviceDesc_;
    bool isOpen_;
    
    // 检查设备接口是否为大容量存储类
    bool checkMassStorageInterface();
};

// USB设备管理器类 - 单例模式
class USBDeviceManager {
public:
    // 获取单例实例
    static USBDeviceManager& getInstance() {
        static USBDeviceManager instance;
        return instance;
    }
    
    // 禁止拷贝和赋值
    USBDeviceManager(const USBDeviceManager&) = delete;
    USBDeviceManager& operator=(const USBDeviceManager&) = delete;
    
    // 初始化libusb库
    bool init();
    void cleanup();
    
    // 扫描USB设备
    std::vector<std::shared_ptr<USBDevice>> scanDevices();
    
    // 按总线ID查找设备
    std::shared_ptr<USBDevice> findDeviceByBusID(const std::string& busID);
    
    // 按vendor/product ID查找设备
    std::shared_ptr<USBDevice> findDeviceByVendorProduct(uint16_t vendorID, uint16_t productID);
    
private:
    // 私有构造函数和析构函数
    USBDeviceManager();
    ~USBDeviceManager();
    
    libusb_context* context_;
    bool isInitialized_;
};

} // namespace libusb

#endif // USB_DEVICE_H 