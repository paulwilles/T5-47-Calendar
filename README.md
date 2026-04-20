# T5 4.7 Family Calendar

This project turns a LilyGo T5 4.7 inch e-paper board into a wall-mounted family calendar dashboard.

## Features

- landscape dashboard layout for agenda and 4-week overview
- Wi-Fi sync from a small helper service
- Google Calendar and Google Tasks support
- button navigation on the device
- designed for the ESP32 WROVER-E based T5 board

## Project layout

- main contains the ESP-IDF firmware
- helper_service contains the Python API bridge
- vendor contains the LilyGo display driver source
- deploy contains Raspberry Pi deployment files

## Helper service hosting

The recommended production setup is to host the helper on a Raspberry Pi or another always-on machine on the home network.

After moving the helper to another machine, point the firmware at that host by updating the default helper URL in the firmware configuration.

## Raspberry Pi quick start

1. Copy this repository to the Pi.
2. Create a Python virtual environment in the project root.
3. Install the packages from helper_service/requirements.txt.
4. Copy client_secret.json, token.json, and your .env file into helper_service on the Pi.
5. Install the provided systemd unit from deploy/raspberry-pi.
6. Start and enable the service.

## Firmware note

For local Wi-Fi credentials, use the local override pattern described in main/app_config_local.example.h so real credentials stay out of git.
