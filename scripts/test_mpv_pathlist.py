#!/usr/bin/env python3
"""Local unit test: mpv audio-files splits on ':' unless escaped.

Matches MpvWidget::mpvEscapePathListItem / mpvPathListItemCount.
Run: python3 scripts/test_mpv_pathlist.py
Optional: same file with a real mpv CLI (if `mpv` is on PATH).
"""
from __future__ import annotations

import http.server
import shutil
import subprocess
import sys
import threading
from pathlib import Path

RAW = "http://127.0.0.1:34527/s/75efb51eb42e4e60a2b0e1f791d947af"


def escape_path_list_item(path: str) -> str:
    out = []
    for c in path:
        if c in "\\:;":
            out.append("\\")
        out.append(c)
    return "".join(out)


def path_list_item_count(value: str) -> int:
    n = 1
    escaped = False
    for c in value:
        if escaped:
            escaped = False
            continue
        if c == "\\":
            escaped = True
            continue
        if c in ":;":
            n += 1
    return n


def test_escape() -> None:
    assert path_list_item_count(RAW) == 3, path_list_item_count(RAW)
    esc = escape_path_list_item(RAW)
    assert path_list_item_count(esc) == 1, esc
    assert "http\\://" in esc and "\\:34527" in esc
    print("PASS  escape: unescaped splits to 3, escaped stays 1")


class _Handler(http.server.BaseHTTPRequestHandler):
    hits: list[str] = []

    def log_message(self, fmt: str, *args) -> None:  # noqa: A003
        return

    def do_GET(self) -> None:  # noqa: N802
        _Handler.hits.append(self.path)
        body = b"not-a-media"
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def test_mpv_cli() -> None:
    mpv = shutil.which("mpv")
    if not mpv:
        print("SKIP  mpv CLI not on PATH (C++ --queue-selftest still covers escape)")
        return
    server = http.server.HTTPServer(("127.0.0.1", 0), _Handler)
    port = server.server_address[1]
    th = threading.Thread(target=server.serve_forever, daemon=True)
    th.start()
    audio = f"http://127.0.0.1:{port}/s/deadbeef"
    video = f"http://127.0.0.1:{port}/v"
    try:
        raw = subprocess.run(
            [
                mpv,
                "--no-config",
                "--vo=null",
                "--ao=null",
                "--really-quiet",
                f"--audio-files={audio}",
                "--frames=1",
                video,
            ],
            capture_output=True,
            text=True,
            timeout=20,
        )
        blob = (raw.stderr or "") + (raw.stdout or "")
        if "Cannot open file 'http'" in blob or "Can not open external file http" in blob:
            print("PASS  mpv CLI: unescaped URL is split (reproduces the no-audio bug)")
        else:
            print("WARN  mpv CLI: expected split-on-colon in logs; got:\n", blob[-800:])

        _Handler.hits.clear()
        esc = escape_path_list_item(audio)
        esc_run = subprocess.run(
            [
                mpv,
                "--no-config",
                "--vo=null",
                "--ao=null",
                "--really-quiet",
                "--msg-level=all=warn",
                f"--audio-files={esc}",
                "--frames=1",
                video,
            ],
            capture_output=True,
            text=True,
            timeout=20,
        )
        blob2 = (esc_run.stderr or "") + (esc_run.stdout or "")
        split = "Cannot open file 'http'" in blob2 or "Can not open external file http" in blob2
        audio_hit = any(h.startswith("/s/") for h in _Handler.hits)
        if split:
            print("FAIL  escaped audio-files still split by mpv")
            print(blob2[-800:])
            raise SystemExit(1)
        if not audio_hit:
            print("FAIL  mpv never GET /s/... after escape; hits=", _Handler.hits)
            print(blob2[-800:])
            raise SystemExit(1)
        print("PASS  mpv CLI: escaped URL stays one path and GETs /s/...")
    finally:
        server.shutdown()


def main() -> int:
    test_escape()
    test_mpv_cli()
    print("OK", Path(__file__).name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
