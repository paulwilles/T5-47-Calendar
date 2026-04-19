# Calendar helper service

This service is the bridge between Google Calendar, Google Tasks, and the T5 device.

## Current status

- mock mode works immediately
- google mode is now wired in
- if google auth is not ready, the service falls back to mock data safely
- the device can keep using the same read-only JSON endpoints

## Endpoints

- GET /health
- GET /api/v1/snapshot
- GET /api/v1/overview
- GET /api/v1/day/{offset}
- GET /api/v1/item/{item_id}

## Setup

1. Install dependencies
2. Copy .env.example to .env
3. For google mode, place your OAuth client file in this folder as client_secret.json
4. Run the auth bootstrap once to generate token.json
5. Start the server

## Run locally

PowerShell:

python -m pip install -r requirements.txt
python auth_bootstrap.py
python -m uvicorn server:app --reload --host 0.0.0.0 --port 8080

## Important settings

- HELPER_MODE=mock for immediate testing
- HELPER_MODE=google to pull real data
- GOOGLE_CALENDAR_IDS=primary or a comma-separated list
- GOOGLE_TASK_LIST_IDS=auto to load all task lists
- HELPER_TIMEZONE should match your home timezone

## Device note

Update the helper service URL in the firmware config to point to the computer or server running this API on your local network.
