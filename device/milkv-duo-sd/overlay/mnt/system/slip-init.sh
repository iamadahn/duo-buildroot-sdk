#!/bin/sh

ifconfig eth0 down

slattach -L -p slip -s 115200 /dev/ttyS1&
ifconfig sl0 192.168.240.2 pointopoint 192.168.240.1 up mtu 1500
route add default gw 192.168.240.1
echo "nameserver 9.9.9.9" > /etc/resolv.conf
