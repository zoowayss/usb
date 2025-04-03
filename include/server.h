#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <map>
#include <queue>
#include "network.h"
#include "usbip_protocol.h"

// 前向声明
namespace libusb {
    class USBDevice;
}

class USBIPServer {
public:
    explicit USBIPServer(int port);
    ~USBIPServer();
    
    bool start();
    void stop();
    
private:
    // 处理客户端连接
    void handleClient(std::shared_ptr<TCPSocket> clientSocket);
    
    // 扫描USB设备
    bool scanUSBDevices();
    
    // 处理设备列表请求
    bool handleDeviceListRequest(std::shared_ptr<TCPSocket> clientSocket, const usbip_packet& packet);
    
    // 处理设备导入请求
    bool handleImportRequest(std::shared_ptr<TCPSocket> clientSocket, const usbip_packet& packet);
    
    // 处理URB请求
    bool handleURBRequest(std::shared_ptr<TCPSocket> clientSocket, const usbip_packet& packet);
    
    // 服务端变量
    int port_;
    std::unique_ptr<Server> server_;
    std::atomic<bool> running_;
    
    // USB设备列表
    std::vector<std::shared_ptr<libusb::USBDevice>> usbDevices_;
    std::map<std::string, std::shared_ptr<libusb::USBDevice>> exportedDevices_;
    std::mutex deviceMutex_;
};

#endif // SERVER_H 