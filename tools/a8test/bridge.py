#!/usr/bin/env python3
"""AltirraBridge client.

`AltirraSDL --bridge[=tcp:HOST:PORT|unix:/path]` embeds a TCP/unix debug server speaking
newline-delimited commands with one-line JSON replies.  On start it writes a 0600 token file
`<TMPDIR>/altirra-bridge-<pid>.token` holding the address and a session token; the first command
must be `HELLO <token>`.

Addresses inside verbs MUST be `$`- or `0x`-prefixed — bare numbers parse as DECIMAL.

Library:  b = Bridge(addr, token); b.cmd("REGS"); b.frames(60); b.memdump(0x9C00, 1024)
CLI:      bridge.py --token-dir DIR cmd "VERB ARGS" ...
(derived from /home/jfergus/dev/a8-u4r/.claude/skills/altirra-bridge/bridge.py)
"""
import base64
import glob
import json
import os
import socket
import sys
import time


class BridgeError(RuntimeError):
    pass


def read_token_file(path):
    lines = open(path, errors="ignore").read(512).splitlines()
    if len(lines) < 2:
        raise BridgeError(f"bad token file {path}")
    return lines[0].strip(), lines[1].strip()


def find_token(token_dir, timeout=30, newer_than=0.0):
    """Newest `*bridge*` token file in token_dir that is newer than `newer_than` (epoch)."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        cands = [f for f in glob.glob(os.path.join(token_dir, "*bridge*")) if os.path.isfile(f)]
        cands.sort(key=os.path.getmtime, reverse=True)
        for f in cands:
            if os.path.getmtime(f) < newer_than:
                continue
            try:
                addr, tok = read_token_file(f)
            except (OSError, BridgeError):
                continue
            if addr.startswith(("tcp:", "unix:")):
                return addr, tok
        time.sleep(0.3)
    return None, None


class Bridge:
    def __init__(self, addr, token, connect_timeout=60):
        self.addr = addr
        last = None
        t0 = time.time()
        while time.time() - t0 < connect_timeout:
            try:
                if addr.startswith("unix:"):
                    self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    self.s.settimeout(600)
                    self.s.connect(addr[5:])
                else:
                    host, port = addr[4:].rsplit(":", 1)
                    self.s = socket.create_connection((host, int(port)), timeout=600)
                break
            except OSError as e:  # server answers once Init completes
                last = e
                time.sleep(0.5)
        else:
            raise BridgeError(f"bridge connect failed ({addr}): {last}")
        self.f = self.s.makefile("rwb")
        r = self.cmd(f"HELLO {token}")
        if not r.get("ok"):
            raise BridgeError(f"HELLO rejected: {r}")

    # -- raw protocol ---------------------------------------------------------------------------
    def cmd(self, line):
        self.f.write((line + "\n").encode())
        self.f.flush()
        resp = self.f.readline().decode().strip()
        if not resp:
            raise BridgeError(f"connection closed during: {line}")
        try:
            return json.loads(resp)
        except json.JSONDecodeError:
            return {"ok": False, "raw": resp}

    def ok(self, line):
        r = self.cmd(line)
        if not r.get("ok"):
            raise BridgeError(f"{line} -> {r}")
        return r

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass

    # -- helpers --------------------------------------------------------------------------------
    def frames(self, n):
        """Run exactly n frames (chunked so each call returns promptly)."""
        r = None
        while n > 0:
            step = min(n, 250)
            r = self.ok(f"FRAME {step}")
            n -= step
        return r

    def memdump(self, addr, length):
        out = bytearray()
        while length > 0:
            chunk = min(length, 0x10000 - (addr & 0xFFFF))
            r = self.ok(f"MEMDUMP ${addr:04X} {chunk}")
            out += base64.b64decode(r["data"])
            addr += chunk
            length -= chunk
        return bytes(out)

    def memload(self, addr, data):
        return self.ok(f"MEMLOAD ${addr:04X} {base64.b64encode(bytes(data)).decode()}")

    def peek(self, addr):
        return self.memdump(addr, 1)[0]

    def peek16(self, addr):
        d = self.memdump(addr, 2)
        return d[0] | (d[1] << 8)

    def poke(self, addr, value):
        return self.ok(f"POKE ${addr:04X} ${value & 0xFF:02X}")

    def key(self, name, shift=False, ctrl=False):
        return self.ok(f"KEY {name}" + (" shift" if shift else "") + (" ctrl" if ctrl else ""))

    def screenshot(self, path):
        path = os.path.abspath(path)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        return self.ok(f"SCREENSHOT path={path}")

    def rawscreen(self, path):
        path = os.path.abspath(path)
        return self.ok(f"RAWSCREEN path={path}")

    def state_save(self, name):
        return self.ok(f"STATE_SAVE {name}")

    def state_load(self, name):
        return self.ok(f"STATE_LOAD {name}")

    def boot(self, path):
        return self.ok(f"BOOT {os.path.abspath(path)}")

    def mount(self, drive, path):
        return self.ok(f"MOUNT {drive} {os.path.abspath(path)}")


def cli(argv):
    import argparse
    ap = argparse.ArgumentParser(description="attach to a running AltirraSDL bridge and run verbs")
    ap.add_argument("--token-dir", default=os.environ.get("A8_TOKEN_DIR", "/tmp"))
    ap.add_argument("--addr")
    ap.add_argument("--token")
    ap.add_argument("verbs", nargs="+")
    a = ap.parse_args(argv)
    addr, tok = (a.addr, a.token) if a.addr else find_token(a.token_dir, timeout=5)
    if not addr:
        print(f"no bridge token found in {a.token_dir}", file=sys.stderr)
        return 1
    b = Bridge(addr, tok)
    for v in a.verbs:
        print(v, "->", json.dumps(b.cmd(v)))
    return 0


if __name__ == "__main__":
    sys.exit(cli(sys.argv[1:]))
