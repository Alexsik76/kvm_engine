#!/bin/bash
# Virtual USB Wake Script
# Forces the host OS to wake up by simulating a harmless keypress or low-level signal.

UDC_CONTROLLER=$(ls /sys/class/udc | head -n 1)

if [ -n "$UDC_CONTROLLER" ] && [ -f "/sys/class/udc/$UDC_CONTROLLER/srp" ]; then
    echo "Attempting low-level USB Session Request Protocol (SRP)..."
    echo 1 > "/sys/class/udc/$UDC_CONTROLLER/srp" 2>/dev/null || true
    sleep 0.5
fi

if [ ! -w "/dev/hidg0" ]; then
    echo "Error: /dev/hidg0 is not writable or does not exist."
    exit 1
fi

echo "Sending wakeup keystroke (Left Shift)..."
# Press Left Shift (Modifier byte 0x02, 0 keys pressed)
printf "\x02\x00\x00\x00\x00\x00\x00\x00" > /dev/hidg0 2>/dev/null || echo "Keystroke failed (endpoint sleep)"
sleep 0.1
# Release all keys
printf "\x00\x00\x00\x00\x00\x00\x00\x00" > /dev/hidg0 2>/dev/null || true

echo "Wakeup signal sent."