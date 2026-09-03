#!/usr/bin/env python3
"""Launch AltirraSDL headless for the gem4xe target machine.

    from a8test.launcher import launch
    emu = launch(tag="p0")           # 800XL, VBXE FX1.26 @ $D640, Rapidus 65C816
    emu.bridge.frames(60)
    emu.stop()

Differences from the vbxetxtadv launcher this is derived from:
  * adds the Rapidus 65C816 accelerator (--adddevice rapidus)
  * memsize defaults to 1088K so PORTB banking is present for MEMAC coexistence tests
  * U1MB is NOT enabled -- AltirraSDL exposes no switch or bridge verb for it.
    See docs/phase0-u1mb.md.  Nothing in the VDI work needs it.

Never opens a window.  Never uses `pkill -f`: the emulator is stopped by pid.

The host's joysticks are kept out of the machine.  AltirraSDL opens every
joystick-class device SDL can see and routes it to port 1 through its input
maps, and a keyboard or mouse with a stray HID "joystick" interface (a
Keychron Q6 Max reports a one-axis controller whose axis sits below its own
minimum, which SDL reads as full left) then holds a PORTA direction line low
for the whole run -- found when the CX80 gate counted every pulse backwards.
The ports are driven through the bridge, so launch() enumerates the host's
joystick devices from sysfs and hands SDL their ids as a blacklist.
"""
import glob
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(HERE))

from a8test.bridge import Bridge, BridgeError, find_token  # noqa: E402

ALTIRRA = os.environ.get("ALTIRRASDL", "AltirraSDL")
XLROM = os.environ.get("ATARIXL_ROM", "/opt/altirra/roms/ATARIXL.ROM")

BASE_ARGS = ["--pal", "--hardware", "800xl", "--kernel", "xl", "--nobasic",
             "--diskemu", "fastestpossible", "--siopatch", "--nofastboot"]
VBXE_DEVICE = "vbxe,version=126,alt_page=false,shared_mem=false"
RAPIDUS_DEVICE = "rapidus"


class Emu:
    def __init__(self, proc, run_dir, bridge):
        self.proc = proc
        self.run_dir = run_dir
        self.bridge = bridge
        self.log = os.path.join(run_dir, "altirra.log")

    def log_tail(self, n=2000):
        try:
            return open(self.log, errors="ignore").read()[-n:]
        except OSError:
            return ""

    def stop(self):
        for fn in (lambda: self.bridge.cmd("QUIT"), self.bridge.close):
            try:
                fn()
            except Exception:
                pass
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=5)
        pidfile = os.path.join(self.run_dir, "pid")
        if os.path.exists(pidfile):
            os.remove(pidfile)


def host_joystick_ids():
    """VID/PID pairs of every input device on this host, in the form
    SDL_JOYSTICK_BLACKLIST_DEVICES wants ("0xVVVV/0xPPPP,...").  Every event
    device, not just the kernel's js* ones: SDL classifies evdev devices by its
    own heuristics (the Keychron interface above is a joystick to SDL and not
    to joydev), and the list only ever governs what SDL opens AS a joystick, so
    a keyboard's id on it costs nothing.  Read from sysfs, not configured:
    whatever is plugged in today is what SDL would open."""
    ids = set()
    for js in glob.glob("/sys/class/input/event*/device/id"):
        try:
            with open(os.path.join(js, "vendor")) as f:
                vid = int(f.read(), 16)
            with open(os.path.join(js, "product")) as f:
                pid = int(f.read(), 16)
        except (OSError, ValueError):
            continue
        ids.add(f"0x{vid:04x}/0x{pid:04x}")
    return ",".join(sorted(ids))


def verify_kernel(b):
    """The real XL OS must be loaded (AltirraOS differs at $E000)."""
    if not os.path.exists(XLROM):
        return None
    rom = open(XLROM, "rb").read()
    return b.memdump(0xE000, 32) == rom[0x2000:0x2020]


def launch(tag="run", extra_args=(), vbxe=True, rapidus=True, memsize="1088K",
           timeout=60, require_real_rom=True):
    run_dir = os.path.join(ROOT, "build", "emu", f"{tag}-{os.getpid()}")
    os.makedirs(run_dir, exist_ok=True)
    sock = os.path.join(run_dir, "bridge.sock")
    if os.path.exists(sock):
        os.remove(sock)
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", TMPDIR=run_dir)
    blacklist = host_joystick_ids()
    if blacklist:
        env["SDL_JOYSTICK_BLACKLIST_DEVICES"] = blacklist
    args = [ALTIRRA, f"--bridge=unix:{sock}", *BASE_ARGS, "--memsize", memsize, "--cleardevices"]
    if vbxe:
        args += ["--adddevice", VBXE_DEVICE]
    if rapidus:
        args += ["--adddevice", RAPIDUS_DEVICE]
    args += list(extra_args)

    logf = open(os.path.join(run_dir, "altirra.log"), "w")
    t0 = time.time()
    proc = subprocess.Popen(args, env=env, stdout=logf, stderr=subprocess.STDOUT, cwd=run_dir)
    with open(os.path.join(run_dir, "pid"), "w") as f:
        f.write(str(proc.pid))
    addr, tok = find_token(run_dir, timeout=timeout, newer_than=t0 - 1)
    if not addr:
        proc.kill()
        tail = open(os.path.join(run_dir, "altirra.log"), errors="ignore").read()[-1500:]
        raise BridgeError(f"no bridge token in {run_dir} after {timeout}s; log tail:\n{tail}")
    b = Bridge(addr, tok, connect_timeout=timeout)
    emu = Emu(proc, run_dir, b)
    if vbxe and not b.cmd("DEVICE_GET vbxe").get("ok"):
        b.ok("DEVICE_SET vbxe on version=126 base=d600")
    if verify_kernel(b) is False and require_real_rom:
        emu.stop()
        raise BridgeError("emulated $E000 does not match the real ATARIXL.ROM (AltirraOS in use?)")
    return emu


def cli(argv):
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--boot")
    ap.add_argument("--tag", default="run")
    ap.add_argument("--frames", type=int, default=60)
    ap.add_argument("--shot")
    ap.add_argument("--memsize", default="1088K")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--no-vbxe", action="store_true")
    ap.add_argument("--no-rapidus", action="store_true")
    ap.add_argument("verbs", nargs="*")
    a = ap.parse_args(argv)
    emu = launch(tag=a.tag, vbxe=not a.no_vbxe, rapidus=not a.no_rapidus, memsize=a.memsize)
    b = emu.bridge
    print("bridge:", b.addr, "run_dir:", emu.run_dir)
    print("devices:", b.cmd("DEVICE_LIST"))
    if a.boot:
        print("boot:", b.boot(a.boot))
    b.frames(a.frames)
    for v in a.verbs:
        print(v, "->", b.cmd(v))
    if a.shot:
        print("shot:", b.screenshot(a.shot))
    if a.keep:
        print(f"left running, pid {emu.proc.pid}; stop with: kill {emu.proc.pid}")
    else:
        emu.stop()
    return 0


if __name__ == "__main__":
    sys.exit(cli(sys.argv[1:]))
