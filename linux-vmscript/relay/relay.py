#!/usr/bin/env python3
"""AcreetBoot CI VM relay — the GHA intercept endpoint on us_ssh.

SPDX-License-Identifier: BSD-3-Clause
Copyright (c) 2026 Natalie Cole-Clift Spiva / AcreetionOS.

Listens for HMAC-signed POSTs from GitHub Actions (workflow job
"vm-boot-test" in acreetionos-gnome/.github/workflows/build-iso.yml),
authenticates, journals the call to receipts.jsonl (append-only, never
rewritten — receipts culture), and dispatches the job to the KVM node
through a reverse-SSH tunnel. Results POST back here and are journaled.

Pure stdlib: no pip deps, so it runs in a bare python:3 alpine container.
Secrets: CVM_RELAY_SECRET from environment ONLY (never on disk).
"""
from __future__ import annotations

import hmac
import hashlib
import json
import os
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SECRET = os.environ["CVM_RELAY_SECRET"].encode()          # required, no default
DISPATCH_CMD = os.environ.get(                             # optional override
    "CVM_NODE_DISPATCH",
    "ssh -o BatchMode=yes -o ConnectTimeout=15 kworker@127.0.0.1 -p 18022 "
    "/srv/cvm/bin/run-vm-gha.sh",
)
RECEIPTS = os.environ.get("CVM_RECEIPTS", "/srv/cvm/receipts.jsonl")
MAX_BODY = 1 << 20  # 1 MiB — payloads are small; we are not a file server


def journal(kind: str, payload: dict) -> None:
    """Append-only receipt journal. Never rewrites history."""
    rec = {"ts": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
           "kind": kind, **payload}
    with open(RECEIPTS, "a", encoding="utf-8") as fp:
        fp.write(json.dumps(rec, sort_keys=True) + "\n")


class Relay(BaseHTTPRequestHandler):
    server_version = "AcreetBootCVMRelay/0.1"

    def _hmac_ok(self, body: bytes) -> bool:
        sig = self.headers.get("X-AcreetBoot-Signature", "")
        want = hmac.new(SECRET, body, hashlib.sha256).hexdigest()
        return hmac.compare_digest(sig, want)

    def do_POST(self) -> None:  # noqa: N802 (http.server API)
        if self.path not in ("/gha", "/result"):
            self.send_error(404)
            return
        n = int(self.headers.get("Content-Length", "0"))
        if n <= 0 or n > MAX_BODY:
            self.send_error(413)
            return
        body = self.rfile.read(n)
        if not self._hmac_ok(body):
            journal("reject-badsig", {"path": self.path,
                                      "peer": self.client_address[0]})
            self.send_error(403, "bad signature")
            return
        try:
            payload = json.loads(body)
        except json.JSONDecodeError:
            self.send_error(400, "not json")
            return

        if self.path == "/gha":
            journal("gha-intercept", payload)
            receipt = "ok" if self._dispatch(payload) else "dispatch-failed"
            journal("dispatch-" + receipt, payload)
            self._reply(202, {"status": receipt,
                              "note": "intercepted and journaled"})
        else:  # /result
            journal("result", payload)
            self._reply(200, {"status": "received"})

    def _dispatch(self, payload: dict) -> bool:
        """Hand the job to the KVM node; receipt of the run comes back
        over /result from the node itself (async)."""
        try:
            subprocess.run(
                DISPATCH_CMD,
                input=json.dumps(payload).encode(),
                timeout=60, check=True,
            )
            return True
        except (subprocess.SubprocessError, OSError) as exc:
            journal("dispatch-error", {"error": str(exc), **payload})
            return False

    def _reply(self, code: int, obj: dict) -> None:
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt: str, *args) -> None:  # keep our logs ours
        journal("http", {"fmt": fmt % args})


if __name__ == "__main__":
    host = os.environ.get("CVM_BIND", "0.0.0.0")
    port = int(os.environ.get("CVM_PORT", "8477"))
    ThreadingHTTPServer((host, port), Relay).serve_forever()
