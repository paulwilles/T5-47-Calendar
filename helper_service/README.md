# Calendar helper service

This service bridges Google Calendar and Google Tasks to the LilyGo T5 display over a simple local JSON API.

## Endpoints

- GET /health
- GET /api/v1/snapshot
- GET /api/v1/overview
- GET /api/v1/day/{offset}
- GET /api/v1/item/{item_id}

## Local setup

1. Create a Python virtual environment.
2. Install the requirements from this folder.
3. Copy .env.example to .env.
4. Place your Google OAuth desktop client file here as client_secret.json.
5. Run auth_bootstrap.py once to create token.json.
6. Start the service with uvicorn.

Example commands:

python -m pip install -r requirements.txt
python auth_bootstrap.py
python -m uvicorn server:app --host 0.0.0.0 --port 8080

## Important settings

- HELPER_MODE=mock for immediate testing
- HELPER_MODE=google for real Google data
- GOOGLE_CALENDAR_IDS=auto or a comma-separated list
- GOOGLE_TASK_LIST_IDS=auto or a comma-separated list
- HELPER_TIMEZONE should match the home timezone of the display

## Raspberry Pi deployment

The recommended production setup is to run this helper on a Raspberry Pi or another always-on home server.

### Recommended flow

1. Copy the project to the Pi.
2. Create a virtual environment on the Pi.
3. Install requirements.
4. Copy your existing client_secret.json and token.json from your current machine to the Pi.
5. Copy .env.example to .env and confirm the host timezone settings.
6. Install the provided systemd service so the helper starts automatically at boot.

See the deployment files under the deploy folder for a ready-to-use systemd unit and installation script.

## Device note

Update the helper service URL in the firmware config to point to the Raspberry Pi IP or hostname on your local network.
