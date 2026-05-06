#!/usr/bin/env python3

import argparse
import base64
import hashlib
import hmac
import json
import socket
import sys
from datetime import datetime, timezone


MTSRC_OK = 0
MTSRC_BAD_REQUEST = 1
MTSRC_INVALID_TAB = 2


class TestFailure(RuntimeError):
    pass


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
            raise TestFailure(f"no response for request {payload!r}")
        try:
            return json.loads(b"".join(chunks).decode("utf-8"))
        except json.JSONDecodeError as exc:
            raise TestFailure(f"invalid JSON response for {payload!r}: {exc}") from exc


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise TestFailure(message)


def control_password_token(client: MadtClient, req_name: str, psk: str) -> str:
    random_resp = client.request({"req": "GetRandom"})
    expect(random_resp.get("retCode") == MTSRC_OK, f"GetRandom failed: {random_resp}")
    nonce_id = random_resp.get("nonceId")
    nonce = random_resp.get("nonce")
    expect(isinstance(nonce_id, str) and nonce_id, f"missing nonceId in {random_resp}")
    expect(isinstance(nonce, str) and nonce, f"missing nonce in {random_resp}")
    timestamp = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    message = "\n".join([req_name, nonce_id, nonce, timestamp]).encode("utf-8")
    auth = hmac.new(psk.encode("utf-8"), message, hashlib.sha256).hexdigest()
    token = {
        "v": 1,
        "nonceId": nonce_id,
        "timestamp": timestamp,
        "auth": auth,
    }
    return base64.b64encode(json.dumps(token, separators=(",", ":")).encode("utf-8")).decode("ascii")


class ConformanceRunner:
    def __init__(self, client: MadtClient, args: argparse.Namespace):
        self.client = client
        self.args = args
        self.failures = []
        self.original_settings = None

    def run_case(self, name: str, func) -> None:
        try:
            func()
            print(f"[PASS] {name}")
        except Exception as exc:  # noqa: BLE001
            self.failures.append((name, str(exc)))
            print(f"[FAIL] {name}: {exc}")

    def run(self) -> int:
        cases = [
            ("GetInfo", self.test_get_info),
            ("GetCharacteristics", self.test_get_characteristics),
            ("GetSettings", self.test_get_settings),
            ("SetSettings roundtrip", self.test_set_settings_roundtrip),
            ("PlaySound", self.test_play_sound),
            ("WebTab lifecycle", self.test_web_tab_lifecycle),
            ("Shortcut lifecycle", self.test_shortcut_lifecycle),
            ("GetRandom", self.test_get_random),
        ]
        if self.args.tab_map_password:
            cases.append(("GetTabMap auth", self.test_get_tab_map))
            cases.append(("GetShortcuts auth", self.test_get_shortcuts_auth))
        if self.args.control_psk or self.args.control_password:
            cases.append(("Control auth negative", self.test_control_auth_negative))
        if self.args.control_action:
            cases.append((f"Control action {self.args.control_action}", self.test_control_action))

        for name, func in cases:
            self.run_case(name, func)

        if self.failures:
            print("\nFailures:")
            for name, message in self.failures:
                print(f"- {name}: {message}")
            return 1
        return 0

    def test_get_info(self) -> None:
        response = self.client.request({"req": "GetInfo"})
        expect(response.get("retCode") == MTSRC_OK, f"unexpected response {response}")
        for key in ("version", "swvers", "hasVNC", "hasShortcut", "hasSound", "extraInfo"):
            expect(key in response, f"missing {key} in {response}")

    def test_get_characteristics(self) -> None:
        response = self.client.request({"req": "GetCharacteristics"})
        expect(response.get("retCode") == MTSRC_OK, f"unexpected response {response}")
        for key in ("winWidth", "winHeight", "iconWidth", "iconHeight", "maxTabs"):
            expect(isinstance(response.get(key), int), f"invalid {key} in {response}")

    def test_get_settings(self) -> None:
        response = self.client.request({"req": "GetSettings"})
        expect(response.get("retCode") == MTSRC_OK, f"unexpected response {response}")
        self.original_settings = response
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
            expect(key in response, f"missing {key} in {response}")

    def test_set_settings_roundtrip(self) -> None:
        if self.original_settings is None:
            self.test_get_settings()
        target_brightness = 254 if self.original_settings["brightness"] == 255 else 255
        patch = {"req": "SetSettings", "brightness": target_brightness}
        response = self.client.request(patch)
        expect(response.get("retCode") == MTSRC_OK, f"patch failed {response}")
        updated = self.client.request({"req": "GetSettings"})
        expect(updated.get("retCode") == MTSRC_OK, f"GetSettings failed {updated}")
        expect(updated.get("brightness") == target_brightness, f"brightness not updated {updated}")
        revert = self.client.request(
            {"req": "SetSettings", "brightness": self.original_settings["brightness"]}
        )
        expect(revert.get("retCode") == MTSRC_OK, f"revert failed {revert}")

    def test_play_sound(self) -> None:
        response = self.client.request(
            {"req": "PlaySound", "soundFlags": "0x00010001", "soundId": "SystemNotification"}
        )
        expect(response.get("retCode") == MTSRC_OK, f"unexpected response {response}")

    def test_web_tab_lifecycle(self) -> None:
        new_resp = self.client.request(
            {
                "req": "NewWebTab",
                "preferredPos": -1,
                "flags": 0,
                "url": "data:text/html,<html><body>MADT</body></html>",
                "iconUrl": "",
            }
        )
        expect(new_resp.get("retCode") == MTSRC_OK, f"NewWebTab failed {new_resp}")
        tab_id = new_resp.get("tabId")
        expect(isinstance(tab_id, str) and tab_id, f"missing tabId in {new_resp}")

        activate = self.client.request({"req": "ActivateTab", "tabId": tab_id})
        expect(activate.get("retCode") == MTSRC_OK, f"ActivateTab failed {activate}")
        blink = self.client.request({"req": "BlinkTab", "tabId": tab_id})
        expect(blink.get("retCode") == MTSRC_OK, f"BlinkTab failed {blink}")
        navigate = self.client.request(
            {"req": "NavigateTo", "tabId": tab_id, "url": "data:text/html,<html>NAV</html>"}
        )
        expect(navigate.get("retCode") == MTSRC_OK, f"NavigateTo failed {navigate}")
        keepalive = self.client.request({"req": "IAmAlive", "tabId": tab_id, "TTL": 10})
        expect(keepalive.get("retCode") == MTSRC_OK, f"IAmAlive failed {keepalive}")
        kill = self.client.request({"req": "KillTab", "tabId": tab_id})
        expect(kill.get("retCode") == MTSRC_OK, f"KillTab failed {kill}")

    def test_shortcut_lifecycle(self) -> None:
        new_resp = self.client.request(
            {"req": "NewShortcut", "preferredPos": -1, "flags": 0, "url": "https://example.org", "iconUrl": ""}
        )
        expect(new_resp.get("retCode") == MTSRC_OK, f"NewShortcut failed {new_resp}")
        shortcut_id = new_resp.get("shortcutId")
        expect(isinstance(shortcut_id, str) and shortcut_id, f"missing shortcutId in {new_resp}")

        listed = self.client.request(
            {"req": "GetShortcuts", "password": self.args.tab_map_password}
        ) if self.args.tab_map_password else None
        if listed is not None:
            expect(listed.get("retCode") == MTSRC_OK, f"GetShortcuts failed {listed}")
            shortcuts = listed.get("shortcuts")
            expect(isinstance(shortcuts, list), f"invalid shortcuts payload {listed}")
            expect(any(entry.get("shortcutId") == shortcut_id for entry in shortcuts), f"shortcut not listed {listed}")

        keepalive = self.client.request({"req": "IAmAlive", "shortcutId": shortcut_id, "TTL": 10})
        expect(keepalive.get("retCode") == MTSRC_OK, f"shortcut IAmAlive failed {keepalive}")
        kill = self.client.request({"req": "KillShortcut", "shortcutId": shortcut_id})
        expect(kill.get("retCode") == MTSRC_OK, f"KillShortcut failed {kill}")

    def test_get_random(self) -> None:
        response = self.client.request({"req": "GetRandom"})
        if self.args.control_psk:
            expect(response.get("retCode") == MTSRC_OK, f"GetRandom failed {response}")
            for key in ("nonceId", "nonce", "ttl", "algo"):
                expect(key in response, f"missing {key} in {response}")
        else:
            expect(response.get("retCode") in (MTSRC_OK, MTSRC_BAD_REQUEST), f"unexpected {response}")

    def test_get_tab_map(self) -> None:
        response = self.client.request({"req": "GetTabMap", "password": self.args.tab_map_password})
        expect(response.get("retCode") == MTSRC_OK, f"unexpected response {response}")
        expect(isinstance(response.get("tabMap"), list), f"invalid tabMap in {response}")

    def test_get_shortcuts_auth(self) -> None:
        response = self.client.request({"req": "GetShortcuts", "password": self.args.tab_map_password})
        expect(response.get("retCode") == MTSRC_OK, f"unexpected response {response}")
        expect(isinstance(response.get("shortcuts"), list), f"invalid shortcuts in {response}")

    def test_control_auth_negative(self) -> None:
        response = self.client.request({"req": "Stop", "password": "invalid"})
        expect(response.get("retCode") == MTSRC_BAD_REQUEST, f"unexpected response {response}")

    def test_control_action(self) -> None:
        expect(
            self.args.i_understand,
            "refusing destructive control action without --i-understand",
        )
        if self.args.control_psk:
            password = control_password_token(self.client, self.args.control_action.title(), self.args.control_psk)
        else:
            password = self.args.control_password
        expect(password, "missing control credential")
        response = self.client.request({"req": self.args.control_action.title(), "password": password})
        expect(response.get("retCode") == MTSRC_OK, f"unexpected response {response}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run MADT protocol conformance checks against a live server.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=25000)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--tab-map-password", default="")
    parser.add_argument("--control-password", default="")
    parser.add_argument("--control-psk", default="")
    parser.add_argument("--control-action", choices=("stop", "restart"))
    parser.add_argument(
        "--i-understand",
        action="store_true",
        help="required to execute destructive control actions",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    client = MadtClient(args.host, args.port, args.timeout)
    runner = ConformanceRunner(client, args)
    return runner.run()


if __name__ == "__main__":
    sys.exit(main())
