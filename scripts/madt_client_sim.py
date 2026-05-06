#!/usr/bin/env python3

import argparse
import base64
import hashlib
import hmac
import json
import socket
import sys
from datetime import datetime, timezone


def utc_timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


class MadtClient:
    def __init__(self, host: str, port: int, timeout: float):
        self.host = host
        self.port = port
        self.timeout = timeout

    def request(self, payload: dict) -> dict:
        encoded = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        with socket.create_connection((self.host, self.port), timeout=self.timeout) as sock:
            sock.sendall(encoded)
            sock.shutdown(socket.SHUT_WR)
            chunks = []
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                chunks.append(chunk)
        if not chunks:
            raise RuntimeError("no response from MADT server")
        return json.loads(b"".join(chunks).decode("utf-8"))


def control_password_token(client: MadtClient, req_name: str, psk: str) -> str:
    random_resp = client.request({"req": "GetRandom"})
    if random_resp.get("retCode") != 0:
        raise RuntimeError(f"GetRandom failed: {random_resp}")
    nonce_id = random_resp["nonceId"]
    nonce = random_resp["nonce"]
    timestamp = utc_timestamp()
    message = "\n".join([req_name, nonce_id, nonce, timestamp]).encode("utf-8")
    auth = hmac.new(psk.encode("utf-8"), message, hashlib.sha256).hexdigest()
    token = {
        "v": 1,
        "nonceId": nonce_id,
        "timestamp": timestamp,
        "auth": auth,
    }
    return base64.b64encode(json.dumps(token, separators=(",", ":")).encode("utf-8")).decode("ascii")


def parse_extra_json(value: str):
    if value is None:
        return None
    return json.loads(value)


def make_payload(args, client: MadtClient) -> dict:
    cmd = args.command
    if cmd == "raw":
        return json.loads(args.json)
    if cmd == "get-info":
        return {"req": "GetInfo"}
    if cmd == "get-random":
        return {"req": "GetRandom"}
    if cmd == "get-characteristics":
        return {"req": "GetCharacteristics"}
    if cmd == "get-settings":
        return {"req": "GetSettings"}
    if cmd == "set-settings":
        payload = {"req": "SetSettings"}
        for key in (
            "volume",
            "brightness",
            "contrast",
            "language",
            "defaultActiveMode",
            "activeMode",
            "defaultVisualMode",
            "visualMode",
        ):
            value = getattr(args, key)
            if value is not None:
                payload[key] = value
        if args.extra1 is not None:
            payload["extra1"] = parse_extra_json(args.extra1)
        if args.extra2 is not None:
            payload["extra2"] = parse_extra_json(args.extra2)
        return payload
    if cmd == "play-sound":
        return {"req": "PlaySound", "soundFlags": args.sound_flags, "soundId": args.sound_id}
    if cmd == "new-web-tab":
        return {
            "req": "NewWebTab",
            "preferredPos": args.preferred_pos,
            "flags": args.flags,
            "iconUrl": args.icon_url,
            "url": args.url,
        }
    if cmd == "activate-tab":
        return {"req": "ActivateTab", "tabId": args.tab_id}
    if cmd == "blink-tab":
        return {"req": "BlinkTab", "tabId": args.tab_id}
    if cmd == "navigate-to":
        return {"req": "NavigateTo", "tabId": args.tab_id, "url": args.url}
    if cmd == "kill-tab":
        return {"req": "KillTab", "tabId": args.tab_id}
    if cmd == "new-shortcut":
        return {
            "req": "NewShortcut",
            "preferredPos": args.preferred_pos,
            "flags": args.flags,
            "iconUrl": args.icon_url,
            "url": args.url,
        }
    if cmd == "kill-shortcut":
        return {"req": "KillShortcut", "shortcutId": args.shortcut_id}
    if cmd == "get-tab-map":
        return {"req": "GetTabMap", "password": args.password}
    if cmd == "get-shortcuts":
        return {"req": "GetShortcuts", "password": args.password}
    if cmd == "iam-alive-tab":
        return {"req": "IAmAlive", "tabId": args.tab_id, "TTL": args.ttl}
    if cmd == "iam-alive-shortcut":
        return {"req": "IAmAlive", "shortcutId": args.shortcut_id, "TTL": args.ttl}
    if cmd in ("stop", "restart"):
        req_name = cmd.title()
        password = args.password
        if args.control_psk:
            password = control_password_token(client, req_name, args.control_psk)
        if not password:
            raise RuntimeError(f"{req_name} requires --password or --control-psk")
        return {"req": req_name, "password": password}
    raise RuntimeError(f"unsupported command {cmd}")


def add_common(parser):
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=25000)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--pretty", action="store_true")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="MADT client simulator")
    add_common(parser)
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("get-info")
    sub.add_parser("get-random")
    sub.add_parser("get-characteristics")
    sub.add_parser("get-settings")

    p = sub.add_parser("set-settings")
    p.add_argument("--volume")
    p.add_argument("--brightness", type=int)
    p.add_argument("--contrast", type=int)
    p.add_argument("--language")
    p.add_argument("--defaultActiveMode")
    p.add_argument("--activeMode")
    p.add_argument("--defaultVisualMode")
    p.add_argument("--visualMode")
    p.add_argument("--extra1")
    p.add_argument("--extra2")

    p = sub.add_parser("play-sound")
    p.add_argument("--sound-id", default="SystemNotification")
    p.add_argument("--sound-flags", default="0x00010001")

    p = sub.add_parser("new-web-tab")
    p.add_argument("--url", required=True)
    p.add_argument("--icon-url", default="")
    p.add_argument("--preferred-pos", type=int, default=-1)
    p.add_argument("--flags", type=int, default=0)

    for name in ("activate-tab", "blink-tab", "kill-tab"):
        p = sub.add_parser(name)
        p.add_argument("--tab-id", required=True)

    p = sub.add_parser("navigate-to")
    p.add_argument("--tab-id", required=True)
    p.add_argument("--url", required=True)

    p = sub.add_parser("new-shortcut")
    p.add_argument("--url", required=True)
    p.add_argument("--icon-url", default="")
    p.add_argument("--preferred-pos", type=int, default=-1)
    p.add_argument("--flags", type=int, default=0)

    p = sub.add_parser("kill-shortcut")
    p.add_argument("--shortcut-id", required=True)

    for name in ("get-tab-map", "get-shortcuts"):
        p = sub.add_parser(name)
        p.add_argument("--password", required=True)

    p = sub.add_parser("iam-alive-tab")
    p.add_argument("--tab-id", required=True)
    p.add_argument("--ttl", type=int, required=True)

    p = sub.add_parser("iam-alive-shortcut")
    p.add_argument("--shortcut-id", required=True)
    p.add_argument("--ttl", type=int, required=True)

    for name in ("stop", "restart"):
        p = sub.add_parser(name)
        p.add_argument("--password", default="")
        p.add_argument("--control-psk", default="")

    p = sub.add_parser("raw")
    p.add_argument("--json", required=True)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    client = MadtClient(args.host, args.port, args.timeout)
    try:
        payload = make_payload(args, client)
        response = client.request(payload)
    except Exception as exc:  # noqa: BLE001
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if args.pretty:
        print(json.dumps(response, indent=2, sort_keys=True))
    else:
        print(json.dumps(response, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
