# USB重定向工具

这是一个基于USBIP协议的USB设备重定向工具，可以将Mac上的USB设备（目前只支持大容量存储设备/U盘）通过网络共享给Ubuntu系统使用。

## 功能特点

- 服务端（Mac）：通过libusb访问本地USB设备，并通过网络提供给客户端使用
- 客户端（Ubuntu）：连接到服务端，创建虚拟USB设备，使得Ubuntu系统可以像使用本地USB设备一样使用远程设备
- 支持大容量存储设备（如U盘）的重定向

## 系统要求

### 服务端（Mac）

- macOS操作系统
- libusb-1.0库（可通过Homebrew安装）

### 客户端（Ubuntu）

- Ubuntu Linux操作系统
- libusb-1.0库
- Linux内核支持VHCI驱动（通常需要加载vhci-hcd模块）

## 安装依赖

### 在Mac上安装libusb

```bash
brew install libusb
```

### 在Ubuntu上安装依赖

```bash
sudo apt-get update
sudo apt-get install libusb-1.0-0-dev linux-modules-extra-$(uname -r)
```

## 编译

1. 克隆仓库：

```bash
git clone https://github.com/yourusername/usb-redirect.git
cd usb-redirect
```

2. 编译代码：

```bash
make
```

编译成功后，可执行文件将被生成在`bin`目录中。

## 使用方法

### 在Mac上运行服务端

```bash
sudo ./bin/usbip -s -p 3240
```

参数说明：
- `-s`: 以服务端模式运行（Mac）
- `-p <port>`: 指定监听端口（默认为3240）

### 在Ubuntu上运行客户端

```bash
sudo ./bin/usbip -c -p 3240 <MAC的IP地址>
```

参数说明：
- `-c`: 以客户端模式运行（Ubuntu）
- `-p <port>`: 指定服务端端口（默认为3240）

注意：客户端默认连接到127.0.0.1，如需连接其他IP地址，需要修改代码中的默认值。

## 使用流程

1. 首先在Mac上插入USB设备（如U盘）
2. 在Mac上启动服务端程序
3. 在Ubuntu上启动客户端程序
4. 客户端会自动连接服务端，获取设备列表，并导入第一个可用的设备
5. 成功后，在Ubuntu系统中可以看到并使用该USB设备

## 注意事项

- 本程序需要以root用户权限运行
- 目前只支持大容量存储设备（U盘）
- Mac服务端使用libusb访问USB设备，而不是直接通过内核接口
- Ubuntu客户端使用VHCI驱动创建虚拟USB设备

## 原理简介

1. 客户端向服务端请求设备列表
2. 服务端返回可用的USB设备信息
3. 客户端选择要导入的设备并发送请求
4. 服务端接受请求并开始为该设备提供服务
5. 客户端创建虚拟USB设备，并转发所有USB请求给服务端
6. 服务端接收请求，访问物理USB设备，然后返回结果给客户端
7. 客户端将结果传递给虚拟设备，完成USB操作

## 许可证

本项目采用MIT许可证。