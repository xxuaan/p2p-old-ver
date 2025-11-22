# INF2004 MQTT-SN via UDP Project

## Prerequisites
Before running, ensure that the following tools and dependencies are installed.

### On Windows (Host)
| Requirement | Description |
|--------------|--------------|
| **Windows 10/11** | Running WSL 2 (Windows Subsystem for Linux v2) |
| **Visual Studio Code / Thonny / Pico SDK** | For flashing code to the Pico W |
| **PowerShell (Admin)** | For firewall control and WSL setup |
| **Git** | To clone repositories |
| **Mosquitto (optional on Windows)** | If you prefer running broker natively instead of in WSL |

### Inside WSL 2
Run these commands in your WSL terminal to prepare the environment:

```bash
sudo apt update
sudo apt install -y build-essential cmake git mosquitto libssl-dev
```

This installs:
- gcc, g++, make → compilers for building the gateway
- cmake → build configuration tool
- git → for repository management
- mosquitto → MQTT broker
- libssl-dev → optional but required if TLS support is needed

## Set Up
Below are the steps to start the application

### 1. Configure WSL Networking
Ensure your WSL 2 instance shares the same IP as Windows by enabling **mirrored networking**
Open Powershell and edit WSL config file (or create if doesn't exist):

```powershell
notepad C:\Users\<user>\.wslconfig
```

Add:
```ini
[wsl2]
networkingMode=mirrored
```

Then restart WSL:

```powershell
wsl --shutdown
```

### 2. Verify Networking
Inside WSL, check IP (WSL IP should be same as laptop IP now):
```bash
hostname -I
```

### 3. Disable Firewall (temporarily)
**<span style="color: red;">Remember to turn it back on after testing.</span>**

Before flashing Pico, turn off Windows Firewall. Open Powershell as Administrator:

```powershell
Set-NetFirewallProfile -Profile Domain,Public,Private -Enabled False
```

### 4. Start Mosquitto Broker in WSL

Start the service and view its log
```bash
sudo systemctl enable mosquitto
sudo systemctl start mosquitto
sudo tail -f /var/log/mosquitto/mosquitto.log
```

### 5. Build and Run MQTT-SN Gateway (Eclipse Paho)
Clone and build the Paho gateway in WSL:
```bash
cd ~
git clone https://github.com/eclipse-paho/paho.mqtt-sn.embedded-c.git
cd paho.mqtt-sn.embedded-c/MQTTSNGateway
```

Using your prefered editor, make the following changes to `gateway.conf`
```bash
#***************************************************************************
#
# config file of MQTT-SN Gateway
#

BrokerName=127.0.0.1

#==============================
#  SensorNetworks parameters
#==============================
#
# UDP | DTLS
#

GatewayPortNo=1884  # need to match your MQTTSN_GATEWAY_PORT in network_config_base.h

```

Build and run:
```bash
chmod +x build.sh   # Run once will do
./build.sh udp      # Build UDP transport interface
cd bin
./MQTT-SNGateway
```

The gateway will now listen for UDP MQTT-SN packets from your Pico W and bridge them to Mosquitto over TCP.

### 6. Build and Flash Pico

- Copy the `network_config_base.h` and rename to `network_config.h` 
- Enable Pico W to connect to Wi-Fi and communicate with MQTT-SN Gateway by modifying content in `network_config.h`
- Press GP22 to toggle QoS levels.

### 7. After testing, re-enable Firewall

```powershell
Set-NetFirewallProfile -Profile Domain,Public,Private -Enabled True
```

## Notes
- MQTT-SN Gateway used **[Eclipse Paho MQTT-SN Embedded C](https://github.com/eclipse-paho/paho.mqtt-sn.embedded-c)**
- Mosquitto listens on TCP port 1883