from __future__ import annotations

import os
from pathlib import Path

SCOPES = [
    "https://www.googleapis.com/auth/calendar.readonly",
    "https://www.googleapis.com/auth/tasks.readonly",
]


def _resolve_path(value: str | None, base_dir: Path, default_name: str) -> Path:
    path = Path(value).expanduser() if value else base_dir / default_name
    if not path.is_absolute():
        path = base_dir / path
    return path


def main() -> None:
    try:
        from google_auth_oauthlib.flow import InstalledAppFlow
    except ImportError as exc:
        raise SystemExit(
            "Missing google auth packages. Install requirements.txt first."
        ) from exc

    base_dir = Path(__file__).resolve().parent
    client_secret = _resolve_path(os.getenv("GOOGLE_CLIENT_SECRET_FILE"), base_dir, "client_secret.json")
    token_file = _resolve_path(os.getenv("GOOGLE_TOKEN_FILE"), base_dir, "token.json")

    if not client_secret.exists():
        raise SystemExit(
            f"OAuth client file not found: {client_secret}\n"
            "Download the Desktop app OAuth JSON from Google Cloud and place it in helper_service."
        )

    flow = InstalledAppFlow.from_client_secrets_file(str(client_secret), SCOPES)
    creds = flow.run_local_server(host="127.0.0.1", port=0, open_browser=True)
    token_file.write_text(creds.to_json(), encoding="utf-8")
    print(f"Saved OAuth token to {token_file}")


if __name__ == "__main__":
    main()
