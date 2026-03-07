#!/bin/bash
# USB HID Gadget Initialization Script (Keyboard + Mouse)
# Adheres to SRP: Responsibility is limited to USB Gadget configuration via ConfigFS.

set -e

CONFIGFS_HOME="/sys/kernel/config/usb_gadget"
GADGET_NAME="kvm_gadget"
GADGET_PATH="$CONFIGFS_HOME/$GADGET_NAME"

# Function to clean up existing gadget configuration gracefully
cleanup_gadget() {
    if [ -d "$GADGET_PATH" ]; then
        echo "Cleaning up existing gadget..."
        cd "$GADGET_PATH"
        
        # Only attempt to unbind if UDC is not empty
        if [ -n "$(cat UDC)" ]; then
            echo "" > UDC || true
        fi

        # Remove functions from configurations (unlinking)
        rm -f configs/c.1/hid.usb0 || true
        rm -f configs/c.1/hid.usb1 || true
        
        # Remove string directories in config
        [ -d "configs/c.1/strings/0x409" ] && rmdir configs/c.1/strings/0x409
        [ -d "configs/c.1" ] && rmdir configs/c.1
        
        # Remove HID functions
        [ -d "functions/hid.usb0" ] && rmdir functions/hid.usb0
        [ -d "functions/hid.usb1" ] && rmdir functions/hid.usb1
        
        # Remove top-level strings
        [ -d "strings/0x409" ] && rmdir strings/0x409
        
        cd ..
        rmdir "$GADGET_NAME"
    fi
}

# Run cleanup before initialization
cleanup_gadget

# 1. Create gadget directory
mkdir -p "$GADGET_PATH"
cd "$GADGET_PATH"

# 2. Set Device IDs
echo 0x1d6b > idVendor  # Linux Foundation
echo 0x0104 > idProduct # Multifunction Composite Gadget
echo 0x0100 > bcdDevice # v1.0.0
echo 0x0200 > bcdUSB    # USB 2.0

# 3. Set Device Strings
mkdir -p strings/0x409
echo "6b626d001" > strings/0x409/serialnumber
echo "KVM Project" > strings/0x409/manufacturer
echo "KVM HID Interface" > strings/0x409/product

# 4. Create HID Function: Keyboard
mkdir -p functions/hid.usb0
echo 1 > functions/hid.usb0/protocol
echo 1 > functions/hid.usb0/subclass
echo 8 > functions/hid.usb0/report_length
# Report Descriptor for Keyboard (Binary)
printf "\x05\x01\x09\x06\xa1\x01\x05\x07\x19\xe0\x29\xe7\x15\x00\x25\x01\x75\x01\x95\x08\x81\x02\x95\x01\x75\x08\x81\x03\x95\x05\x75\x01\x05\x08\x19\x01\x29\x05\x91\x02\x95\x01\x75\x03\x91\x03\x95\x06\x75\x08\x15\x00\x25\x65\x05\x07\x19\x00\x29\x65\x81\x00\xc0" > functions/hid.usb0/report_desc

# 5. Create HID Function: Mouse
mkdir -p functions/hid.usb1
echo 2 > functions/hid.usb1/protocol
echo 1 > functions/hid.usb1/subclass
echo 4 > functions/hid.usb1/report_length
printf "\\x05\\x01\\x09\\x02\\xa1\\x01\\x09\\x01\\xa1\\x00\\x05\\x09\\x19\\x01\\x29\\x03\\x15\\x00\\x25\\x01\\x95\\x03\\x75\\x01\\x81\\x02\\x95\\x01\\x75\\x05\\x81\\x03\\x05\\x01\\x09\\x30\\x09\\x31\\x09\\x38\\x15\\x81\\x25\\x7f\\x75\\x08\\x95\\x03\\x81\\x06\\xc0\\xc0" > functions/hid.usb1/report_desc

# 6. Create Configuration
mkdir -p configs/c.1/strings/0x409
echo "Config 1: HID Gadget" > configs/c.1/strings/0x409/configuration
echo 0xa0 > configs/c.1/bmAttributes # Bus-powered with Remote Wakeup
echo 250 > configs/c.1/MaxPower      # 500mA (Unit is 2mA)

# 7. Bind Functions to Configuration
ln -s functions/hid.usb0 configs/c.1/
ln -s functions/hid.usb1 configs/c.1/

# 8. Enable Gadget (Bind to UDC controller)
UDC_CONTROLLER=$(ls /sys/class/udc | head -n 1)
if [ -z "$UDC_CONTROLLER" ]; then
    echo "Error: No UDC controller found. Is dwc2 loaded?"
    exit 1
fi

echo "$UDC_CONTROLLER" > UDC
echo "USB HID Gadget initialized successfully on $UDC_CONTROLLER."