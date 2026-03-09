#!/bin/bash
set -x

# Fix config
sed -i 's|/dev/ttyACM0|/dev/ttyACM3?uart-baudrate=460800|g' /etc/default/otbr-agent

# Start basic services
service rsyslog start
service dbus start

# Kill any existing agents
pkill -9 otbr-agent
pkill -9 otbr-web

# Give dbus a moment
sleep 2

# Start otbr-agent manually to ensure it works
/usr/sbin/otbr-agent -I wpan0 -B eth0 --rest-listen-address 0.0.0.0 'spinel+hdlc+uart:///dev/ttyACM3?uart-baudrate=460800' &

# Wait for it to come up
sleep 5

# Start web UI
/usr/sbin/otbr-web -I wpan0 -a 0.0.0.0 -p 80 &

# Check state
ot-ctl state

# Keep alive
sleep infinity
