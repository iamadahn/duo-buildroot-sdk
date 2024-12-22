#!/bin/sh

# Set Pin-29(GP22) to GPIO
devmem 0x0502707c 32 0x111
devmem 0x03001068 32 0x3

# Set Pin-19(GP14) to GPIO
duo-pinmux -w GP14/GP14 > /dev/null

# Set Pin-0 and Pin-1 to UART1
duo-pinmux -w GP0/UART1_TX
duo-pinmux -w GP1/UART1_RX

# Insmod PWM Module
insmod /mnt/system/ko/cv180x_pwm.ko

