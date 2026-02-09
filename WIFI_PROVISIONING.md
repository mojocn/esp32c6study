# WiFi Provisioning 使用指南

本项目现在使用 ESP-IDF 的 WiFi Provisioning Manager API 来配置 WiFi 连接，而不是从固定文件读取配置。

## 功能特点

- ✅ 通过 BLE 动态配置 WiFi 凭证
- ✅ 凭证安全存储在 NVS (非易失性存储)
- ✅ 首次配置后自动重连
- ✅ 支持重置配置功能
- ✅ SRP6a 安全协议 (Security Version 2)

## 工作流程

### 首次启动（未配置状态）

1. 设备启动后检测到未配置 WiFi
2. 自动进入 Provisioning 模式
3. 通过 BLE 广播设备名称：`ShellyDevKit-ESP32C6-JSONRPC_XXXXXX`
4. 可以使用手机 App 连接并配置 WiFi

### 已配置状态

1. 设备从 NVS 读取已保存的 WiFi 凭证
2. 自动连接到配置的 WiFi 网络
3. 无需再次配置

## 使用方法

### 选项 1: 使用 ESP BLE Provisioning App

#### Android

1. 从 Google Play 下载 "ESP BLE Provisioning" App
2. 烧录并启动 ESP32-C6 设备
3. 打开 App，扫描附近的设备
4. 选择以 `ShellyDevKit-ESP32C6-JSONRPC_` 开头的设备
5. 输入 Proof of Possession (PoP): `abcd1234`
6. 选择要连接的 WiFi 网络并输入密码
7. 等待配置完成

#### iOS

1. 从 App Store 下载 "ESP BLE Provisioning" App
2. 按照类似 Android 的步骤操作

### 选项 2: 使用 ESP Provisioning 命令行工具

```bash
# 安装 esp-idf-provisioning-tool
pip install esp-idf-provisioning

# 扫描设备
esp-prov scan --transport ble

# 配置 WiFi
esp-prov \
  --transport ble \
  --service_name "ShellyDevKit-ESP32C6-JSONRPC_XXXXXX" \
  --pop "abcd1234" \
  --wifi_ssid "你的WiFi名称" \
  --wifi_password "你的WiFi密码"
```

### 选项 3: 使用 Python 脚本

```bash
cd $IDF_PATH/tools/esp_prov
python esp_prov.py \
  --transport ble \
  --service_name "ShellyDevKit-ESP32C6-JSONRPC_XXXXXX" \
  --pop "abcd1234" \
  --ssid "你的WiFi名称" \
  --passphrase "你的WiFi密码"
```

## 配置参数

### Proof of Possession (PoP)

当前 PoP 设置为：**`abcd1234`**

⚠️ **安全提示**：在生产环境中，应该为每个设备生成唯一的 PoP，或使用更安全的值。

可以在 [wifi_manager.c](main/wifi_manager.c#L149) 修改：

```c
const char *pop = "abcd1234";  // 修改为你的 PoP
```

### Security Version

当前使用 **Security Version 2 (SRP6a)**，提供加密的凭证传输。

可以在 [config.h](main/config.h#L17) 修改：

```c
#define PROV_SECURITY_VERSION 2  /* 0: 无安全, 1: WPA2, 2: SRP6a */
```

## API 说明

### 初始化 WiFi

```c
esp_err_t wifi_manager_init(void);
```

初始化 WiFi provisioning 或连接已配置的网络。

### 检查配置状态

```c
bool wifi_manager_is_provisioned(void);
```

返回设备是否已配置 WiFi。

### 重置配置

```c
esp_err_t wifi_manager_reset_provisioning(void);
```

清除保存的 WiFi 凭证，下次启动将重新进入 provisioning 模式。

## 调试

### 查看日志

连接串口监控器查看详细日志：

```bash
idf.py monitor
```

### 日志输出示例

**未配置状态：**

```
I (XXX) WiFi: Starting provisioning via BLE
I (XXX) WiFi: === Provisioning Info ===
I (XXX) WiFi: Device Name: ShellyDevKit-ESP32C6-JSONRPC_A1B2C3
I (XXX) WiFi: Proof of Possession (PoP): abcd1234
I (XXX) WiFi: ========================
```

**已配置状态：**

```
I (XXX) WiFi: Already provisioned, starting Wi-Fi STA
I (XXX) WiFi: Found credentials for SSID: YourWiFiName
I (XXX) WiFi: Connected to AP: YourWiFiName
I (XXX) WiFi: Got IP address: 192.168.1.100
```

## 重置 WiFi 配置

如果需要重新配置 WiFi，有两种方法：

### 方法 1: 擦除 NVS 分区

```bash
idf.py erase-flash
idf.py flash
```

### 方法 2: 调用重置函数

在代码中调用：

```c
wifi_manager_reset_provisioning();
// 然后重启设备
esp_restart();
```

## 常见问题

### Q: 设备无法进入 provisioning 模式？

**A**: 检查：

- NVS 分区是否已初始化 (在 main.c 中调用 `nvs_flash_init()`)
- BLE 是否启用
- 查看串口日志是否有错误信息

### Q: 配置后无法连接 WiFi？

**A**: 可能原因：

- WiFi 密码错误
- WiFi 信号太弱
- 路由器 MAC 过滤
- 查看日志中的详细错误信息

### Q: 如何修改 BLE 设备名称？

**A**: 修改 [config.h](main/config.h) 中的：

```c
#define BLE_DEVICE_NAME "ShellyDevKit-ESP32C6-JSONRPC"
```

### Q: 可以使用 SoftAP 而不是 BLE 吗？

**A**: 可以，修改 [wifi_manager.c](main/wifi_manager.c#L127-L129)：

```c
wifi_prov_mgr_config_t config = {
    .scheme = wifi_prov_scheme_softap,  // 改为 softap
    .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE
};
```

## 相关资源

- [ESP-IDF WiFi Provisioning 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/provisioning/wifi_provisioning.html)
- [ESP BLE Provisioning App (Android)](https://play.google.com/store/apps/details?id=com.espressif.provble)
- [ESP BLE Provisioning App (iOS)](https://apps.apple.com/app/esp-ble-provisioning/id1473590141)
- [ESP Provisioning Tools](https://github.com/espressif/esp-idf-provisioning-tools)
