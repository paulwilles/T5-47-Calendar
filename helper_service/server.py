from __future__ import annotations

import logging
import os
from datetime import date, datetime, timedelta, timezone
from pathlib import Path
from typing import Any
from zoneinfo import ZoneInfo

from fastapi import FastAPI, HTTPException

BASE_DIR = Path(__file__).resolve().parent

try:
    from dotenv import load_dotenv
    load_dotenv(BASE_DIR / ".env", override=True)
except Exception:
    pass

app = FastAPI(title="T5 Calendar Helper Service", version="0.3.0")
log = logging.getLogger("t5_helper")
logging.basicConfig(level=os.getenv("HELPER_LOG_LEVEL", "INFO").upper())

SNAPSHOT_DAYS = int(os.getenv("HELPER_DAYS", "28"))
CACHE_SECONDS = int(os.getenv("HELPER_CACHE_SECONDS", "300"))
HELPER_MODE = os.getenv("HELPER_MODE", "mock").strip().lower()
ALLOW_MOCK_FALLBACK = os.getenv("HELPER_ALLOW_MOCK_FALLBACK", "true").strip().lower() in {"1", "true", "yes", "on"}
TIMEZONE_NAME = os.getenv("HELPER_TIMEZONE", "UTC")
GOOGLE_SCOPES = [
    "https://www.googleapis.com/auth/calendar.readonly",
    "https://www.googleapis.com/auth/tasks.readonly",
]

try:
    LOCAL_TZ = ZoneInfo(TIMEZONE_NAME)
except Exception:
    LOCAL_TZ = timezone.utc

_CACHE: dict[str, Any] = {
    "expires_at": datetime.now(timezone.utc),
    "payload": None,
    "source": "cold",
    "error": None,
}


def _iso_utc(dt: datetime) -> str:
    return dt.astimezone(timezone.utc).isoformat().replace("+00:00", "Z")


def _parse_google_dt(value: str | None) -> datetime | None:
    if not value:
        return None
    if value.endswith("Z"):
        value = value[:-1] + "+00:00"
    try:
        parsed = datetime.fromisoformat(value)
    except ValueError:
        return None
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return parsed.astimezone(LOCAL_TZ)


def _sort_key(item: dict[str, Any]) -> tuple[int, str, str]:
    untimed = item.get("all_day") or item.get("start") in {"Any", "All day", ""}
    return (0 if untimed else 1, str(item.get("start", "")), str(item.get("title", "")).lower())


def build_mock_data(source: str = "mock") -> dict[str, Any]:
    today = date.today()
    days = []
    details = {}

    for offset in range(SNAPSHOT_DAYS):
        current = today + timedelta(days=offset)
        items = []

        if offset == 0:
            items = [
                {
                    "id": "evt-001",
                    "type": "event",
                    "title": "School run",
                    "start": "08:15",
                    "end": "08:45",
                    "all_day": False,
                    "completed": False,
                    "source": "Family",
                    "location": "Local school",
                    "detail": "Morning school drop-off for the children.",
                },
                {
                    "id": "evt-002",
                    "type": "event",
                    "title": "Project review",
                    "start": "10:00",
                    "end": "11:00",
                    "all_day": False,
                    "completed": False,
                    "source": "Work",
                    "location": "Home office",
                    "detail": "Weekly review call with action items and follow-ups.",
                },
                {
                    "id": "tsk-001",
                    "type": "task",
                    "title": "Order groceries",
                    "start": "Any",
                    "end": "",
                    "all_day": True,
                    "completed": False,
                    "source": "Shared Tasks",
                    "location": "",
                    "detail": "Place the weekly grocery order before 5pm.",
                },
            ]
        elif offset == 1:
            items = [
                {
                    "id": "evt-004",
                    "type": "event",
                    "title": "Dentist",
                    "start": "09:00",
                    "end": "09:30",
                    "all_day": False,
                    "completed": False,
                    "source": "Personal",
                    "location": "High Street Dental",
                    "detail": "Routine checkup appointment.",
                },
                {
                    "id": "tsk-002",
                    "type": "task",
                    "title": "Pay utility bill",
                    "start": "Any",
                    "end": "",
                    "all_day": True,
                    "completed": False,
                    "source": "Admin",
                    "location": "",
                    "detail": "Electricity bill due tomorrow.",
                },
            ]
        elif offset % 7 == 2:
            items = [
                {
                    "id": f"evt-{offset}",
                    "type": "event",
                    "title": "Club activity",
                    "start": "16:00",
                    "end": "17:00",
                    "all_day": False,
                    "completed": False,
                    "source": "Family",
                    "location": "Community hall",
                    "detail": "Standing after-school activity.",
                }
            ]

        items.sort(key=_sort_key)
        for entry in items:
            details[entry["id"]] = entry

        days.append(
            {
                "offset": offset,
                "date": current.isoformat(),
                "label": current.strftime("%a %d %b"),
                "weekday": current.strftime("%a"),
                "day": current.day,
                "item_count": len(items),
                "items": items,
            }
        )

    return {
        "generated_at": datetime.now(LOCAL_TZ).isoformat(),
        "source": source,
        "days": days,
        "details": details,
    }


def _resolve_config_path(value: str | None, default_name: str) -> Path:
    path = Path(value).expanduser() if value else BASE_DIR / default_name
    if not path.is_absolute():
        path = BASE_DIR / path
    return path


def _load_google_credentials():
    from google.auth.transport.requests import Request
    from google.oauth2.credentials import Credentials

    token_path = _resolve_config_path(os.getenv("GOOGLE_TOKEN_FILE"), "token.json")
    if not token_path.exists():
        raise RuntimeError(f"OAuth token file not found: {token_path}. Run auth_bootstrap.py first.")

    creds = Credentials.from_authorized_user_file(str(token_path), GOOGLE_SCOPES)
    if not creds.valid:
        if creds.expired and creds.refresh_token:
            creds.refresh(Request())
            token_path.write_text(creds.to_json(), encoding="utf-8")
        else:
            raise RuntimeError("OAuth token is invalid or expired. Run auth_bootstrap.py again.")
    return creds


def _discover_calendar_ids(calendar_service) -> list[str]:
    configured = os.getenv("GOOGLE_CALENDAR_IDS", "primary").strip()
    if configured.lower() != "auto":
        ids = [part.strip() for part in configured.split(",") if part.strip()]
        return ids or ["primary"]

    response = calendar_service.calendarList().list().execute()
    return [item["id"] for item in response.get("items", []) if not item.get("hidden", False)] or ["primary"]


def _discover_tasklist_ids(tasks_service) -> list[str]:
    configured = os.getenv("GOOGLE_TASK_LIST_IDS", "auto").strip()
    if configured.lower() == "none":
        return []
    if configured.lower() != "auto":
        return [part.strip() for part in configured.split(",") if part.strip()]

    response = tasks_service.tasklists().list(maxResults=50).execute()
    return [item["id"] for item in response.get("items", [])]


def _normalize_event(event: dict[str, Any], source_name: str) -> dict[str, Any]:
    start_info = event.get("start", {})
    end_info = event.get("end", {})
    all_day = "date" in start_info and not start_info.get("dateTime")

    if all_day:
        start_label = "All day"
        end_label = ""
    else:
        start_dt = _parse_google_dt(start_info.get("dateTime"))
        end_dt = _parse_google_dt(end_info.get("dateTime"))
        start_label = start_dt.strftime("%H:%M") if start_dt else ""
        end_label = end_dt.strftime("%H:%M") if end_dt else ""

    return {
        "id": f"evt-{event.get('id', 'unknown')}",
        "type": "event",
        "title": event.get("summary") or "Untitled event",
        "start": start_label,
        "end": end_label,
        "all_day": all_day,
        "completed": False,
        "source": source_name,
        "location": event.get("location", ""),
        "detail": event.get("description", "")[:500],
    }


def _normalize_task(task: dict[str, Any], source_name: str) -> dict[str, Any]:
    return {
        "id": f"tsk-{task.get('id', 'unknown')}",
        "type": "task",
        "title": task.get("title") or "Untitled task",
        "start": "Any",
        "end": "",
        "all_day": True,
        "completed": task.get("status") == "completed",
        "source": source_name,
        "location": "",
        "detail": (task.get("notes") or "")[:500],
    }


def build_google_data() -> dict[str, Any]:
    from googleapiclient.discovery import build

    creds = _load_google_credentials()
    calendar_service = build("calendar", "v3", credentials=creds, cache_discovery=False)
    tasks_service = build("tasks", "v1", credentials=creds, cache_discovery=False)

    today = datetime.now(LOCAL_TZ).date()
    calendar_ids = _discover_calendar_ids(calendar_service)
    task_list_ids = _discover_tasklist_ids(tasks_service)

    calendar_names: dict[str, str] = {}
    for calendar_id in calendar_ids:
        try:
            meta = calendar_service.calendarList().get(calendarId=calendar_id).execute()
            calendar_names[calendar_id] = meta.get("summary", calendar_id)
        except Exception:
            calendar_names[calendar_id] = calendar_id

    tasklist_names: dict[str, str] = {}
    for tasklist_id in task_list_ids:
        try:
            meta = tasks_service.tasklists().get(tasklist=tasklist_id).execute()
            tasklist_names[tasklist_id] = meta.get("title", tasklist_id)
        except Exception:
            tasklist_names[tasklist_id] = tasklist_id

    days = []
    details = {}

    for offset in range(SNAPSHOT_DAYS):
        current_date = today + timedelta(days=offset)
        day_start = datetime.combine(current_date, datetime.min.time(), LOCAL_TZ)
        day_end = day_start + timedelta(days=1)
        items: list[dict[str, Any]] = []

        for calendar_id in calendar_ids:
            events = calendar_service.events().list(
                calendarId=calendar_id,
                timeMin=_iso_utc(day_start),
                timeMax=_iso_utc(day_end),
                singleEvents=True,
                orderBy="startTime",
                maxResults=50,
            ).execute()
            for event in events.get("items", []):
                normalized = _normalize_event(event, calendar_names.get(calendar_id, calendar_id))
                items.append(normalized)
                details[normalized["id"]] = normalized

        for tasklist_id in task_list_ids:
            tasks = tasks_service.tasks().list(
                tasklist=tasklist_id,
                showCompleted=True,
                showHidden=False,
                maxResults=100,
            ).execute()
            for task in tasks.get("items", []):
                due_date = _parse_google_dt(task.get("due"))
                if due_date is None:
                    if offset != 0 or task.get("status") == "completed":
                        continue
                elif due_date.date() != current_date:
                    continue

                normalized = _normalize_task(task, tasklist_names.get(tasklist_id, tasklist_id))
                items.append(normalized)
                details[normalized["id"]] = normalized

        items.sort(key=_sort_key)
        days.append(
            {
                "offset": offset,
                "date": current_date.isoformat(),
                "label": day_start.strftime("%a %d %b"),
                "weekday": day_start.strftime("%a"),
                "day": current_date.day,
                "item_count": len(items),
                "items": items,
            }
        )

    return {
        "generated_at": datetime.now(LOCAL_TZ).isoformat(),
        "source": "google",
        "days": days,
        "details": details,
    }


def get_snapshot_data(force_refresh: bool = False) -> dict[str, Any]:
    now = datetime.now(timezone.utc)
    if not force_refresh and _CACHE["payload"] is not None and now < _CACHE["expires_at"]:
        return _CACHE["payload"]

    try:
        payload = build_google_data() if HELPER_MODE == "google" else build_mock_data("mock")
        _CACHE["payload"] = payload
        _CACHE["source"] = payload.get("source", HELPER_MODE)
        _CACHE["error"] = None
        _CACHE["expires_at"] = now + timedelta(seconds=CACHE_SECONDS)
        return payload
    except Exception as exc:
        log.exception("Helper service refresh failed")
        _CACHE["error"] = str(exc)
        if HELPER_MODE == "google" and ALLOW_MOCK_FALLBACK:
            payload = build_mock_data("mock-fallback")
            _CACHE["payload"] = payload
            _CACHE["source"] = "mock-fallback"
            _CACHE["expires_at"] = now + timedelta(seconds=60)
            return payload
        raise


@app.get("/health")
def health() -> dict[str, Any]:
    status = "ok"
    try:
        snapshot = get_snapshot_data()
    except Exception as exc:
        snapshot = None
        status = f"error: {exc}"

    return {
        "status": status,
        "mode": HELPER_MODE,
        "active_source": _CACHE.get("source"),
        "days": SNAPSHOT_DAYS,
        "cache_seconds": CACHE_SECONDS,
        "last_error": _CACHE.get("error"),
        "generated_at": snapshot.get("generated_at") if snapshot else None,
    }


@app.get("/api/v1/snapshot")
def snapshot() -> dict[str, Any]:
    return get_snapshot_data()


@app.get("/api/v1/overview")
def overview() -> dict[str, Any]:
    data = get_snapshot_data()
    return {
        "generated_at": data["generated_at"],
        "source": data.get("source", "unknown"),
        "days": [
            {
                "offset": d["offset"],
                "date": d["date"],
                "label": d["label"],
                "weekday": d["weekday"],
                "day": d["day"],
                "item_count": d["item_count"],
            }
            for d in data["days"]
        ],
    }


@app.get("/api/v1/day/{offset}")
def day(offset: int) -> dict[str, Any]:
    data = get_snapshot_data()
    for entry in data["days"]:
        if entry["offset"] == offset:
            return entry
    raise HTTPException(status_code=404, detail="Day not found")


@app.get("/api/v1/item/{item_id}")
def item(item_id: str) -> dict[str, Any]:
    data = get_snapshot_data()
    if item_id in data["details"]:
        return data["details"][item_id]
    raise HTTPException(status_code=404, detail="Item not found")
