# Raspberry Pi deployment

This folder contains a simple systemd-based deployment for the calendar helper service.

## Target layout

- project path: /opt/t5-47-calendar
- service name: t5-calendar-helper
- port: 8080

## Install on the Pi

Run the installer:

./deploy/raspberry-pi/install.sh

## Required private files

Copy these files from your current working helper machine into helper_service on the Pi:

- client_secret.json
- token.json
- .env

## Start and verify

sudo systemctl start t5-calendar-helper
sudo systemctl status t5-calendar-helper
curl http://localhost:8080/health

Once it is working, update the device helper URL to the Raspberry Pi IP or hostname.
