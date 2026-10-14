#!/usr/bin/env python3
"""SacredFire online (dev only): Demonware auth reply builder and signer.

Used with the test-key mode (BB_DW_TEST_KEY + runtime key swap): builds the
reply the client's FUN_00013d30 accepts structurally (numeric auth_task/code,
tickets, identity) and signs the exact body bytes with the TEST private key
(PKCS#1 v1.5 + SHA-256, base64 in X-Signature). Shapes come from Ghidra
decompilation of the PS4 1.21 client; ticket FIELD VALUES are v1 placeholders
(magic + zeros) until their semantics are reverse engineered.

Never used against official servers/keys. Requires openssl on PATH.
"""

import base64
import json
import struct
import subprocess

TICKET_MAGIC = 0xEFBDADDE
TICKET_LEN = 128


def make_ticket_128():
    """128-byte decoded ticket v1: magic header, zero body (semantics TBD)."""
    ticket = bytearray(TICKET_LEN)
    struct.pack_into("<I", ticket, 0, TICKET_MAGIC)
    return bytes(ticket)


def build_auth_reply(auth_task=46, iv_seed=0, client_id="dev-client", account_type="dev", identity="1"):
    """Reply body: auth_task/code/iv_seed as NUMBERS (the client rejects quoted ones)."""
    reply = {
        "auth_task": int(auth_task),
        "code": 700,
        "iv_seed": int(iv_seed),
        "client_id": client_id[:64],
        "account_type": account_type[:10],
        "client_ticket": base64.b64encode(make_ticket_128()).decode(),
        "server_ticket": base64.b64encode(bytes(TICKET_LEN)).decode(),
        "identity": identity,
    }
    return json.dumps(reply, separators=(",", ":")).encode("utf-8")


def sign_body(key_path, body):
    """X-Signature value: base64 of RSA/SHA-256 signature over exact body bytes."""
    proc = subprocess.run(["openssl", "dgst", "-sha256", "-sign", key_path],
                          input=body, capture_output=True)
    if proc.returncode:
        raise RuntimeError(f"openssl sign failed: {proc.stderr.decode()}")
    return base64.b64encode(proc.stdout).decode()


def request_task_id(body):
    """auth_task from the client's request (string '46' -> 46); 46 on any error."""
    try:
        return int(json.loads(body.decode("utf-8")).get("auth_task", 46))
    except (ValueError, UnicodeDecodeError, AttributeError):
        return 46


def request_iv_seed(body):
    """iv_seed from the client's request (string digits -> int); 0 on any error."""
    try:
        return int(json.loads(body.decode("utf-8")).get("iv_seed", 0))
    except (ValueError, UnicodeDecodeError, AttributeError):
        return 0
