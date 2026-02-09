#!/bin/bash
#
# WiFi Provisioning Script for ESP32-C6
# This script helps you configure WiFi credentials via BLE provisioning
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
POP="abcd1234"  # Proof of Possession - change if you modified it in code

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}ESP32-C6 WiFi Provisioning Tool${NC}"
echo -e "${BLUE}========================================${NC}"
echo

# Check if esp-prov is installed
if ! command -v esp-prov &> /dev/null; then
    echo -e "${YELLOW}esp-prov not found. Installing...${NC}"
    pip install esp-idf-provisioning
    echo -e "${GREEN}✓ esp-prov installed${NC}"
    echo
fi

# Scan for devices
echo -e "${BLUE}Scanning for ESP32-C6 devices via BLE...${NC}"
echo -e "${YELLOW}Note: Make sure your device is powered on and in provisioning mode${NC}"
echo

esp-prov scan --transport ble

echo
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}Provisioning Steps:${NC}"
echo

# Get device name
read -p "Enter device name (e.g., ShellyDevKit-ESP32C6-JSONRPC_A1B2C3): " DEVICE_NAME

if [ -z "$DEVICE_NAME" ]; then
    echo -e "${RED}Error: Device name cannot be empty${NC}"
    exit 1
fi

# Get WiFi credentials
read -p "Enter WiFi SSID: " WIFI_SSID

if [ -z "$WIFI_SSID" ]; then
    echo -e "${RED}Error: WiFi SSID cannot be empty${NC}"
    exit 1
fi

read -sp "Enter WiFi Password: " WIFI_PASSWORD
echo

if [ -z "$WIFI_PASSWORD" ]; then
    echo -e "${RED}Error: WiFi password cannot be empty${NC}"
    exit 1
fi

# Confirm before provisioning
echo
echo -e "${YELLOW}Provisioning Details:${NC}"
echo -e "  Device Name: ${GREEN}$DEVICE_NAME${NC}"
echo -e "  WiFi SSID: ${GREEN}$WIFI_SSID${NC}"
echo -e "  PoP: ${GREEN}$POP${NC}"
echo

read -p "Continue with provisioning? (y/n): " CONFIRM

if [[ ! "$CONFIRM" =~ ^[Yy]$ ]]; then
    echo -e "${RED}Provisioning cancelled${NC}"
    exit 0
fi

# Start provisioning
echo
echo -e "${BLUE}Starting WiFi provisioning...${NC}"

esp-prov \
    --transport ble \
    --service_name "$DEVICE_NAME" \
    --pop "$POP" \
    --wifi_ssid "$WIFI_SSID" \
    --wifi_password "$WIFI_PASSWORD"

if [ $? -eq 0 ]; then
    echo
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}✓ Provisioning completed successfully!${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo
    echo -e "${BLUE}Your ESP32-C6 should now be connecting to WiFi${NC}"
    echo -e "${BLUE}Check the serial monitor for the assigned IP address${NC}"
else
    echo
    echo -e "${RED}========================================${NC}"
    echo -e "${RED}✗ Provisioning failed${NC}"
    echo -e "${RED}========================================${NC}"
    echo
    echo -e "${YELLOW}Troubleshooting tips:${NC}"
    echo "  1. Ensure the device is powered on and in provisioning mode"
    echo "  2. Check that Bluetooth is enabled on your computer"
    echo "  3. Verify the device name is correct"
    echo "  4. Make sure the PoP matches the one in your code"
    exit 1
fi
