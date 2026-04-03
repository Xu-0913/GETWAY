# GetWay — C++ 工业网关原型系统

一个基于 **C++17 / Linux** 构建的轻量级工业网关系统，实现设备接入、协议转换、云端通信、OTA 升级以及守护进程管理等核心能力。

> 面向：嵌入式 / IoT / 边缘计算场景  
> 特点：模块解耦 + 多协议适配 + 工程化设计 
> 环境：Raspberry Pi(ARM Ubuntu 24.04) 

---

##  核心能力

- **设备抽象层(Device)**：统一接入不同设备(串口 / 蓝牙 / LoRa)
- **协议转换**：Binary ⇄ JSON，实现设备与云端解耦
- **MQTT 通信**：基于 Paho C++ 实现可靠发布/订阅
- **多协议扩展**：支持 Bluetooth / Serial / LoRa
- **OTA 升级**：A/B 分区 + SHA256 校验 + 回滚机制
- **守护进程**：子进程监控 + 异常自动重启

---

## 系统架构

```text
daemon
  ├── GETWAY(主进程)
  │      └── Route(协议转换 / 路由)
  │              ├── MQTTClient(云端通信)
  │              └── Device(设备抽象)──Serial(串口配置)
  │                      ├── BluetoothDevice
  │                      ├── LoraDevice
  │                    
  └── OTA(升级进程)
```

---

##   数据流设计

###  设备上行(Device → Cloud)

```text
设备
  ↓
Serial / Bluetooth / LoRa
  ↓
Device
  ↓
Route
  ↓
Message::fromBinary()
  ↓
Message::toJson()
  ↓
MQTTClient::send()
  ↓
MQTT Broker / 云端
```

---

###  云端下行(Cloud → Device)

```text
MQTT Broker / 云端
  ↓
MQTTClient 回调
  ↓
Route
  ↓
Message::fromJson()
  ↓
Message::toBinary()
  ↓
Device::write()
  ↓
Serial / Bluetooth / LoRa
  ↓
设备
```

---

##  模块说明

###  App(网关主进程)

- **Buffer**：环形缓冲区，处理粘包/拆包  
- **Device**：设备抽象基类，统一设备接口  
- **BluetoothDevice / LoraDevice**：协议适配层  
- **Serial**：串口配置与数据收发  
- **Message**：Binary ⇄ JSON 编解码  
- **Route**：数据路由与协议转换核心  
- **MQTTClient**：MQTT 客户端封装  
- **ThreadPool**：线程池，处理异步任务  

---

###  OTA 模块

- manifest 拉取  
- 固件下载(libcurl)  
- SHA256 校验(OpenSSL)  
- A/B 分区升级  
- 启动后健康检查(health check)  
- commit / rollback  

---

###  Daemon(守护进程)

- 子进程拉起(GETWAY / OTA)  
- 崩溃自动重启  
- 优雅退出(signal 处理)  
- 守护化运行(daemonize)  

---

##  构建方式

```bash
# 构建网关主进程
cd App/build
cmake ..
make -j

# 构建 OTA
cd ../../Ota
make -j

# 构建守护进程
cd ../Deamon
make -j
```

---

##  运行方式

```bash
# 启动网关
./GETWAY

# 启动 OTA
./ota

# 启动守护进程(推荐)
./daemon
```

---

##  待完善
- OTA 与守护进程联动  
- 配置文件系统(去硬编码)  
- 日志系统(分级 + 持久化)  
- LoRa 深度适配  
- 压力测试与异常场景验证  

---

  
