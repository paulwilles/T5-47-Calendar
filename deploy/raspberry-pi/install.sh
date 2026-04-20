#!/usr/bin/env bash
set -euo pipefail

APP_DIR="/opt/t5-47-calendar"
SERVICE_NAME="t5-calendar-helper"
PI_USER="${SUDO_USER:-$USER}"
PI_GROUP="$(id -gn "$PI_USER")"

sudo apt-get update
sudo apt-get install -y python3 python3-venv python3-pip

if [ ! -d "$APP_DIR" ]; then
  sudo mkdir -p "$APP_DIR"
  sudo chown "$PI_USER:$PI_GROUP" "$APP_DIR"
fi

if [ ! -d "$APP_DIR/.git" ]; then
  git clone https://github.com/paulwilles/T5-47-Calendar.git "$APP_DIR"
else
  git -C "$APP_DIR" pull --ff-only
fi

python3 -m venv "$APP_DIR/.venv"
"$APP_DIR/.venv/bin/python" -m pip install --upgrade pip
"$APP_DIR/.venv/bin/python" -m pip install -r "$APP_DIR/helper_service/requirements.txt"

if [ ! -f "$APP_DIR/helper_service/.env" ]; then
  cp "$APP_DIR/helper_service/.env.example" "$APP_DIR/helper_service/.env"
fi

sudo sed \
  -e "s|User=pi|User=$PI_USER|" \
  -e "s|Group=pi|Group=$PI_GROUP|" \
  "$APP_DIR/deploy/raspberry-pi/t5-calendar-helper.service" \
  | sudo tee "/etc/systemd/system/${SERVICE_NAME}.service" >/dev/null

sudo systemctl daemon-reload
sudo systemctl enable "$SERVICE_NAME"

cat <<EOF

Install complete.

Next steps:
1. Copy client_secret.json and token.json into $APP_DIR/helper_service/
2. Review $APP_DIR/helper_service/.env
3. Start the service with:
   sudo systemctl start $SERVICE_NAME
4. Check health with:
   curl http://localhost:8080/health

EOF
