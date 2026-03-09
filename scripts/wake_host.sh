#!/bin/bash
# Virtual USB Wake Script
# Forces the host OS to wake up by simulating a harmless keypress (Left Shift).

if [ ! -w "/dev/hidg0" ]; then
    echo "Error: /dev/hidg0 is not writable or does not exist."
    exit 1
fi

echo "Sending wakeup keystroke (Left Shift)..."
# Press Left Shift (Modifier byte 0x02, 0 keys pressed)
printf "\x02\x00\x00\x00\x00\x00\x00\x00" > /dev/hidg0
sleep 0.1
# Release all keys
printf "\x00\x00\x00\x00\x00\x00\x00\x00" > /dev/hidg0

echo "Wakeup signal sent."