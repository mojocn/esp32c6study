# ESP32-C3 JSON-RPC 2.0 Server

A dual-interface JSON-RPC 2.0 server implementation for ESP32-C3, supporting both HTTP and BLE (Bluetooth Low Energy) communication protocols.

## Features

- ✅ JSON-RPC 2.0 compliant server
- 🌐 HTTP endpoint over WiFi
- 📡 BLE (Bluetooth Low Energy) interface
- � **WiFi Provisioning via BLE** - No hardcoded credentials!
- 🔌 GPIO control (Light control on GPIO 4)
- 📊 System information retrieval
- 🧮 Built-in RPC methods (echo, add, subtract, etc.)

## Hardware Requirements

- ESP32-C3 development board
- USB cable for programming and power
- (Optional) LED connected to GPIO 4 for light control

## Software Requirements

- ESP-IDF v5.0 or later
- Python 3.8+
- CMake 3.16+

## Quick Start

### 1. Clone and Setup

```bash
git clone <repository-url>
cd esp32c3study
```

### 2. Configure WiFi via BLE Provisioning

**No manual configuration file needed!** This project uses WiFi Provisioning over BLE.

On first boot, the device will enter provisioning mode. Use one of these methods:

#### Option A: ESP BLE Provisioning App (Recommended)

1. Download "ESP BLE Provisioning" app ([Android](https://play.google.com/store/apps/details?id=com.espressif.provble) / [iOS](https://apps.apple.com/app/esp-ble-provisioning/id1473590141))
2. Scan for device: `ShellyDevKit-ESP32C3-JSONRPC_XXXXXX`
3. Enter PoP: `abcd1234`
4. Select your WiFi and enter password
5. Done! Credentials are saved automatically

#### Option B: Command Line

```bash
pip install esp-idf-provisioning
esp-prov --transport ble --service_name "ShellyDevKit-ESP32C3-JSONRPC_XXXXXX" \
  --pop "abcd1234" --wifi_ssid "YourWiFi" --wifi_password "YourPassword"
```

📖 **See [WIFI_PROVISIONING.md](WIFI_PROVISIONING.md) for detailed instructions**

#### Detailed Command Line Steps

1. **Install provisioning tool:**

   ```bash
   pip install esp-idf-provisioning
   ```

2. **Flash and start the device:**

   ```bash
   idf.py build flash monitor
   ```

3. **In device logs, find the device name:**

   ```
   I (XXX) WiFi: Device Name: ShellyDevKit-ESP32C3-JSONRPC_A1B2C3
   ```

4. **Open a new terminal and scan for devices:**

   ```bash
   esp-prov scan --transport ble
   ```

5. **Provision WiFi credentials:**

   ```bash
   esp-prov \
     --transport ble \
     --service_name "ShellyDevKit-ESP32C3-JSONRPC_A1B2C3" \
     --pop "abcd1234" \
     --wifi_ssid "YourWiFiName" \
     --wifi_password "YourWiFiPassword"
   ```

6. **Alternative: Use ESP-IDF provisioning script:**
   ```bash
   cd $IDF_PATH/tools/esp_prov
   python esp_prov.py \
     --transport ble \
     --service_name "ShellyDevKit-ESP32C3-JSONRPC_A1B2C3" \
     --pop "abcd1234" \
     --ssid "YourWiFiName" \
     --passphrase "YourWiFiPassword"
   ```

### 3. Build and Flash

```bash
idf.py build
idf.py flash monitor
```

## Usage

### HTTP Interface

Once connected to WiFi, the device will print its IP address. Send JSON-RPC requests to:

```
http://<device-ip>/rpc
```

Example using curl:

```bash
curl -X POST http://192.168.1.100/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"add","params":[5,3],"id":1}'
```

### BLE Interface

- **Device Name:** `ShellyDevKit-ESP32C3-JSONRPC`
- **Service UUID:** `5f6d4f53-5f52-5043-5f53-56435f49445f`
- **Characteristics:**
  - TX_CTL (Write): `5f6d4f53-5f52-5043-5f74-785f63746c5f`
  - DATA (Read/Write): `5f6d4f53-5f52-5043-5f64-6174615f5f5f`
  - RX_CTL (Read/Notify): `5f6d4f53-5f52-5043-5f72-785f63746c5f`

## Available RPC Methods

### `echo`

Echoes back the provided parameters.

```json
{
  "jsonrpc": "2.0",
  "method": "echo",
  "params": { "message": "Hello World" },
  "id": 1
}
```

### `add`

Adds two numbers.

```json
{
  "jsonrpc": "2.0",
  "method": "add",
  "params": [5, 3],
  "id": 1
}
```

### `subtract`

Subtracts two numbers.

```json
{
  "jsonrpc": "2.0",
  "method": "subtract",
  "params": [10, 3],
  "id": 1
}
```

### `get_system_info`

Retrieves ESP32 system information.

```json
{
  "jsonrpc": "2.0",
  "method": "get_system_info",
  "params": [],
  "id": 1
}
```

### `light`

Controls the GPIO 4 light (0 = off, 1 = on).

```json
{
  "jsonrpc": "2.0",
  "method": "light",
  "params": 1,
  "id": 1
}
```

## Project Structure

### WiFi Provisioning

WiFi credentials are configured via BLE provisioning on first boot. The credentials are:

- Securely stored in NVS (Non-Volatile Storage)
- Automatically loaded on subsequent boots
- Can be reset by calling `wifi_manager_reset_provisioning()` or erasing flash

To reconfigure WiFi:

```bash
# Method 1: Erase flash and reprogram
idf.py erase-flash
idf.py flash

# Method 2: Use the reset function in code
wifi_manager_reset_provisioning();
esp_restart();
```

### GPIO Pin

The light control is configured on GPIO 4. To change this, edit `main/config.h`:

```c
#define GPIO_LIGHT 4
```

### WiFi Settings

Configure retry attempts and provisioning security in `main/config.h`:

```c
#define WIFI_MAXIMUM_RETRY 10
#define PROV_SECURITY_VERSION 2  /* 0: No security, 1: WPA2, 2: SRP6a */
```

## Troubleshooting

### WiFi Connection Failed

- Use BLE provisioning app or command line tool to reconfigure credentials
- Check WiFi signal strength
- Ensure your router supports 2.4GHz (ESP32-C3 doesn't support 5GHz)
- Verify password is correct in provisioning step
- Check device logs with `idf.py monitor`

### Provisioning Failed

- Ensure Bluetooth is enabled on your phone/computer
- Verify the correct Proof of Possession (PoP): `abcd1234`
- Make sure device is in provisioning mode (first boot or after reset)
- Check that device name matches: `ShellyDevKit-ESP32C3-JSONRPC_XXXXXX`

### BLE Not Visible

- Ensure Bluetooth is enabled on your client device
- Check that the device is within range (typically 10m)
- Verify BLE is initialized successfully in the monitor logs

### Build Errors

- Ensure ESP-IDF is properly installed and sourced
- Check that you're using ESP-IDF v5.0 or later
- Run `idf.py fullclean` and rebuild

## License

[Add your license here]

## Contributing

[Add contribution guidelines here]

## Author

[Add author information here]
