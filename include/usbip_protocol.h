#ifndef USBIP_PROTOCOL_H
#define USBIP_PROTOCOL_H

#include <cstdint>
#include <string>
#include <vector>
#include <netinet/in.h>

// 协议版本和操作码
#define USBIP_VERSION 0x0111

// 命令操作码
#define USBIP_CMD_SUBMIT  0x0001
#define USBIP_CMD_UNLINK  0x0002
#define USBIP_RET_SUBMIT  0x0003
#define USBIP_RET_UNLINK  0x0004

// 设备操作
#define USBIP_OP_REQ_DEVLIST    0x8005
#define USBIP_OP_REP_DEVLIST    0x0005
#define USBIP_OP_REQ_IMPORT     0x8003
#define USBIP_OP_REP_IMPORT     0x0006

// 方向
#define USBIP_DIR_OUT 0
#define USBIP_DIR_IN  1

// USB设备类
#define USB_CLASS_MASS_STORAGE 0x08

// 传输类型
#define USBIP_XFER_CTRL     0
#define USBIP_XFER_ISO      1
#define USBIP_XFER_BULK     2
#define USBIP_XFER_INT      3

// USBIP 头部结构
struct usbip_header {
    uint32_t version;
    uint32_t command;
    uint32_t status;
};

// USB设备描述符
struct usb_device_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
};

// 设备列表请求
struct op_devlist_request {
    uint32_t version;
};

// 设备信息
struct usb_device_info {
    char path[256];
    char busid[32];
    uint32_t busnum;
    uint32_t devnum;
    uint32_t speed;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bConfigurationValue;
    uint8_t bNumConfigurations;
    uint8_t bNumInterfaces;
};

// 导入设备请求
struct op_import_request {
    uint32_t version;
    char busid[32];
};

// 导入设备响应
struct op_import_reply {
    uint32_t version;
    uint32_t status;
    usb_device_info udev;
};

// URB提交命令
struct cmd_submit {
    uint32_t seqnum;
    uint32_t devid;
    uint32_t direction;
    uint32_t ep;
    uint32_t transfer_flags;
    uint32_t transfer_buffer_length;
    uint32_t start_frame;
    uint32_t number_of_packets;
    uint32_t interval;
    uint8_t setup[8];
};

// URB提交回复
struct ret_submit {
    uint32_t seqnum;
    uint32_t devid;
    uint32_t direction;
    uint32_t ep;
    uint32_t status;
    uint32_t actual_length;
    uint32_t start_frame;
    uint32_t number_of_packets;
    uint32_t error_count;
};

// 完整的USBIP数据包
struct usbip_packet {
    usbip_header header;
    union {
        cmd_submit cmd_submit;
        ret_submit ret_submit;
        op_devlist_request devlist_req;
        op_import_request import_req;
        op_import_reply import_rep;
    };
    std::vector<uint8_t> data;
};

// 工具函数
namespace usbip_utils {
    // 主机字节序转网络字节序
    inline uint32_t htonl_wrap(uint32_t hostlong) {
        return htonl(hostlong);
    }
    
    // 网络字节序转主机字节序
    inline uint32_t ntohl_wrap(uint32_t netlong) {
        return ntohl(netlong);
    }
    
    // 主机字节序转网络字节序 (16位)
    inline uint16_t htons_wrap(uint16_t hostshort) {
        return htons(hostshort);
    }
    
    // 网络字节序转主机字节序 (16位)
    inline uint16_t ntohs_wrap(uint16_t netshort) {
        return ntohs(netshort);
    }
}

#endif // USBIP_PROTOCOL_H 