#!/bin/bash
# Virtual USB Replug Script
# Forces the host OS to wake up or reinitialize the USB port.

CONFIGFS_HOME="/sys/kernel/config/usb_gadget"
GADGET_NAME="kvm_gadget"
GADGET_PATH="$CONFIGFS_HOME/$GADGET_NAME"

if [ ! -d "$GADGET_PATH" ]; then
    echo "Error: Gadget not found."
    exit 1
fi

UDC_CONTROLLER=$(ls /sys/class/udc | head -n 1)

if [ -z "$UDC_CONTROLLER" ]; then
    echo "Error: UDC controller not found."
    exit 1
fi

echo "Simulating USB unplug..."
echo "" > "$GADGET_PATH/UDC"
sleep 1

echo "Simulating USB plug..."
echo "$UDC_CONTROLLER" > "$GADGET_PATH/UDC"
echo "Virtual replug complete."