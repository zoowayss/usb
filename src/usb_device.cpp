#include "../include/usb_device.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace libusb {

// USBDevice 实现
USBDevice::USBDevice(libusb_device* device)
    : device_(device), handle_(nullptr), isOpen_(false) {
    // 获取设备描述符
    libusb_get_device_descriptor(device_, &deviceDesc_);
}

USBDevice::~USBDevice() {
    close();
}

bool USBDevice::open() {
    if (isOpen_) {
        return true;
    }
    
    int ret = libusb_open(device_, &handle_);
    if (ret != LIBUSB_SUCCESS) {
        std::cerr << "打开USB设备失败: " << libusb_error_name(ret) << std::endl;
        return false;
    }
    
    isOpen_ = true;
    return true;
}

void USBDevice::close() {
    if (isOpen_ && handle_) {
        libusb_close(handle_);
        handle_ = nullptr;
        isOpen_ = false;
    }
}

bool USBDevice::getDeviceDescriptor(usb_device_descriptor& desc) {
    desc.bLength = deviceDesc_.bLength;
    desc.bDescriptorType = deviceDesc_.bDescriptorType;
    desc.bcdUSB = deviceDesc_.bcdUSB;
    desc.bDeviceClass = deviceDesc_.bDeviceClass;
    desc.bDeviceSubClass = deviceDesc_.bDeviceSubClass;
    desc.bDeviceProtocol = deviceDesc_.bDeviceProtocol;
    desc.bMaxPacketSize0 = deviceDesc_.bMaxPacketSize0;
    desc.idVendor = deviceDesc_.idVendor;
    desc.idProduct = deviceDesc_.idProduct;
    desc.bcdDevice = deviceDesc_.bcdDevice;
    desc.iManufacturer = deviceDesc_.iManufacturer;
    desc.iProduct = deviceDesc_.iProduct;
    desc.iSerialNumber = deviceDesc_.iSerialNumber;
    desc.bNumConfigurations = deviceDesc_.bNumConfigurations;
    
    return true;
}

bool USBDevice::getConfigDescriptor(uint8_t configIndex, std::vector<uint8_t>& configData) {
    struct libusb_config_descriptor* config;
    int ret = libusb_get_config_descriptor(device_, configIndex, &config);
    if (ret != LIBUSB_SUCCESS) {
        std::cerr << "获取配置描述符失败: " << libusb_error_name(ret) << std::endl;
        return false;
    }
    
    // 复制配置描述符数据
    configData.resize(config->wTotalLength);
    memcpy(configData.data(), config, config->wTotalLength);
    
    libusb_free_config_descriptor(config);
    return true;
}

int USBDevice::controlTransfer(uint8_t requestType, uint8_t request, 
                             uint16_t value, uint16_t index,
                             uint8_t* data, uint16_t length, 
                             unsigned int timeout) {
    if (!isOpen_ || !handle_) {
        if (!open()) {
            return -1;
        }
    }
    
    return libusb_control_transfer(handle_, requestType, request, value, index, data, length, timeout);
}

int USBDevice::bulkTransfer(unsigned char endpoint, 
                          unsigned char* data, 
                          int length, 
                          int* actualLength,
                          unsigned int timeout) {
    if (!isOpen_ || !handle_) {
        if (!open()) {
            return -1;
        }
    }
    
    return libusb_bulk_transfer(handle_, endpoint, data, length, actualLength, timeout);
}

int USBDevice::interruptTransfer(unsigned char endpoint, 
                               unsigned char* data, 
                               int length, 
                               int* actualLength,
                               unsigned int timeout) {
    if (!isOpen_ || !handle_) {
        if (!open()) {
            return -1;
        }
    }
    
    return libusb_interrupt_transfer(handle_, endpoint, data, length, actualLength, timeout);
}

std::string USBDevice::getBusID() const {
    std::stringstream ss;
    ss << static_cast<int>(getBusNumber()) << "-" << static_cast<int>(getDeviceAddress());
    return ss.str();
}

uint8_t USBDevice::getBusNumber() const {
    return libusb_get_bus_number(device_);
}

uint8_t USBDevice::getDeviceAddress() const {
    return libusb_get_device_address(device_);
}

uint16_t USBDevice::getVendorID() const {
    return deviceDesc_.idVendor;
}

uint16_t USBDevice::getProductID() const {
    return deviceDesc_.idProduct;
}

uint8_t USBDevice::getDeviceClass() const {
    return deviceDesc_.bDeviceClass;
}

bool USBDevice::isMassStorage() const {
    // 检查设备类是否为大容量存储
    if (deviceDesc_.bDeviceClass == USB_CLASS_MASS_STORAGE) {
        std::cout << "设备类别直接标识为大容量存储设备" << std::endl;
        return true;
    }
    
    // 如果设备类是接口定义的（通常为0或0xFF），则检查接口
    if (deviceDesc_.bDeviceClass == 0 || deviceDesc_.bDeviceClass == 0xFF) {
        return const_cast<USBDevice*>(this)->checkMassStorageInterface();
    }
    
    return false;
}

bool USBDevice::checkMassStorageInterface() {
    struct libusb_config_descriptor* config;
    int ret = libusb_get_config_descriptor(device_, 0, &config);
    if (ret != LIBUSB_SUCCESS) {
        std::cerr << "获取配置描述符失败: " << libusb_error_name(ret) << std::endl;
        return false;
    }
    
    bool isMassStorage = false;
    
    // 检查所有接口
    for (int i = 0; i < config->bNumInterfaces; i++) {
        const struct libusb_interface& interface = config->interface[i];
        
        for (int j = 0; j < interface.num_altsetting; j++) {
            const struct libusb_interface_descriptor& ifaceDesc = interface.altsetting[j];
            
            if (ifaceDesc.bInterfaceClass == USB_CLASS_MASS_STORAGE) {
                std::cout << "接口 " << i << " 是大容量存储类别" << std::endl;
                std::cout << "   子类: " << static_cast<int>(ifaceDesc.bInterfaceSubClass) << std::endl;
                std::cout << "   协议: " << static_cast<int>(ifaceDesc.bInterfaceProtocol) << std::endl;
                
                // 子类常见值:
                // 0x01 = RBC (降低的块命令)
                // 0x02 = SFF-8020i/MMC-2 (ATAPI) 
                // 0x03 = QIC-157 (磁带)
                // 0x04 = UFI (软盘)
                // 0x05 = SFF-8070i (软盘)
                // 0x06 = SCSI透明命令集
                
                // 大多数U盘都是子类6 (SCSI) 或 子类1 (RBC)
                if (ifaceDesc.bInterfaceSubClass == 0x01 || 
                    ifaceDesc.bInterfaceSubClass == 0x06) {
                    isMassStorage = true;
                    std::cout << "   确认为U盘设备" << std::endl;
                } else {
                    // 其它大容量存储设备(如光驱、磁带等)也允许
                    isMassStorage = true;
                    std::cout << "   确认为其他大容量存储设备" << std::endl;
                }
                break;
            }
        }
        
        if (isMassStorage) {
            break;
        }
    }
    
    libusb_free_config_descriptor(config);
    return isMassStorage;
}

bool USBDevice::fillDeviceInfo(usb_device_info& info) {
    // 清空结构体
    memset(&info, 0, sizeof(info));
    
    // 设置总线ID和路径
    std::string busID = getBusID();
    snprintf(info.busid, sizeof(info.busid), "%s", busID.c_str());
    snprintf(info.path, sizeof(info.path), "/sys/devices/pci0000:00/0000:00:14.0/usb%d/%s", 
             getBusNumber(), busID.c_str());
    
    // 设置其他字段
    info.busnum = getBusNumber();
    info.devnum = getDeviceAddress();
    info.speed = 2; // 假设是高速设备
    
    info.idVendor = getVendorID();
    info.idProduct = getProductID();
    info.bcdDevice = deviceDesc_.bcdDevice;
    
    info.bDeviceClass = deviceDesc_.bDeviceClass;
    info.bDeviceSubClass = deviceDesc_.bDeviceSubClass;
    info.bDeviceProtocol = deviceDesc_.bDeviceProtocol;
    
    info.bConfigurationValue = 1; // 假设使用第一个配置
    info.bNumConfigurations = deviceDesc_.bNumConfigurations;
    
    // 获取接口数量
    struct libusb_config_descriptor* config;
    int ret = libusb_get_active_config_descriptor(device_, &config);
    if (ret == LIBUSB_SUCCESS) {
        info.bNumInterfaces = config->bNumInterfaces;
        libusb_free_config_descriptor(config);
    } else {
        info.bNumInterfaces = 1; // 默认至少有一个接口
    }
    
    return true;
}

// USBDeviceManager 实现
USBDeviceManager::USBDeviceManager()
    : context_(nullptr), isInitialized_(false) {
}

USBDeviceManager::~USBDeviceManager() {
    cleanup();
}

bool USBDeviceManager::init() {
    if (isInitialized_) {
        return true;
    }
    
    int ret = libusb_init(&context_);
    if (ret != LIBUSB_SUCCESS) {
        std::cerr << "初始化libusb失败: " << libusb_error_name(ret) << std::endl;
        return false;
    }
    
    isInitialized_ = true;
    return true;
}

void USBDeviceManager::cleanup() {
    if (isInitialized_ && context_) {
        libusb_exit(context_);
        context_ = nullptr;
        isInitialized_ = false;
    }
}

std::vector<std::shared_ptr<USBDevice>> USBDeviceManager::scanDevices() {
    std::vector<std::shared_ptr<USBDevice>> devices;
    
    if (!isInitialized_) {
        if (!init()) {
            return devices;
        }
    }
    
    libusb_device** devs;
    ssize_t cnt = libusb_get_device_list(context_, &devs);
    if (cnt < 0) {
        std::cerr << "获取USB设备列表失败: " << libusb_error_name(cnt) << std::endl;
        return devices;
    }
    
    std::cout << "发现 " << cnt << " 个USB设备，开始筛选U盘..." << std::endl;
    
    for (ssize_t i = 0; i < cnt; i++) {
        libusb_device* dev = devs[i];
        auto usbDev = std::make_shared<USBDevice>(dev);
        
        // 读取基本设备信息
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(dev, &desc) == 0) {
            std::cout << "设备 " << i << ": VID=" << std::hex << desc.idVendor 
                      << ", PID=" << desc.idProduct 
                      << ", 设备类=" << static_cast<int>(desc.bDeviceClass) << std::dec << std::endl;
        }
        
        // 检查是否为大容量存储设备
        if (usbDev->isMassStorage()) {
            std::cout << "找到大容量存储设备: BusID=" << usbDev->getBusID() << std::endl;
            libusb_ref_device(dev); // 增加引用计数，防止设备被释放
            devices.push_back(usbDev);
        }
    }
    
    std::cout << "筛选完成，找到 " << devices.size() << " 个大容量存储设备" << std::endl;
    
    libusb_free_device_list(devs, 1);
    return devices;
}

std::shared_ptr<USBDevice> USBDeviceManager::findDeviceByBusID(const std::string& busID) {
    if (!isInitialized_) {
        if (!init()) {
            return nullptr;
        }
    }
    
    libusb_device** devs;
    ssize_t cnt = libusb_get_device_list(context_, &devs);
    if (cnt < 0) {
        std::cerr << "获取USB设备列表失败: " << libusb_error_name(cnt) << std::endl;
        return nullptr;
    }
    
    std::shared_ptr<USBDevice> foundDevice = nullptr;
    
    for (ssize_t i = 0; i < cnt; i++) {
        libusb_device* dev = devs[i];
        auto usbDev = std::make_shared<USBDevice>(dev);
        
        if (usbDev->getBusID() == busID) {
            libusb_ref_device(dev); // 增加引用计数，防止设备被释放
            foundDevice = usbDev;
            break;
        }
    }
    
    libusb_free_device_list(devs, 1);
    return foundDevice;
}

std::shared_ptr<USBDevice> USBDeviceManager::findDeviceByVendorProduct(uint16_t vendorID, uint16_t productID) {
    if (!isInitialized_) {
        if (!init()) {
            return nullptr;
        }
    }
    
    libusb_device** devs;
    ssize_t cnt = libusb_get_device_list(context_, &devs);
    if (cnt < 0) {
        std::cerr << "获取USB设备列表失败: " << libusb_error_name(cnt) << std::endl;
        return nullptr;
    }
    
    std::shared_ptr<USBDevice> foundDevice = nullptr;
    
    for (ssize_t i = 0; i < cnt; i++) {
        libusb_device* dev = devs[i];
        auto usbDev = std::make_shared<USBDevice>(dev);
        
        if (usbDev->getVendorID() == vendorID && usbDev->getProductID() == productID) {
            libusb_ref_device(dev); // 增加引用计数，防止设备被释放
            foundDevice = usbDev;
            break;
        }
    }
    
    libusb_free_device_list(devs, 1);
    return foundDevice;
}

} // namespace libusb 