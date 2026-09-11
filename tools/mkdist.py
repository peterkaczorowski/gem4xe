#!/usr/bin/env python3
"""The gem4xe distribution: what a tester is handed.

    tools/mkdist.py build/gem4xe-<stamp> [--tar out.tar.gz]

Three things, and a page that explains them:

  * the bootable disks, when this tree has the DOS fixtures to build
    them (they are not always here, and the artefact says which are
    missing rather than pretending);
  * the system's own files, loose, for putting on a disk of somebody
    else's making;
  * the application kit (tools/mksdk.py), for writing something to run
    on it.

The page is generated, and that is the point of this tool rather than a
directory of `cp` rules.  Two of its sections are read out of the
program itself -- what the desktop's File and View menus offer, and
what they offer DISABLED -- so the honest half of "what works" cannot
drift from the resource; and the disks' contents are read back out of
the images with tools/atr.py, so what the page lists is what is on
them.
"""
import argparse
import datetime
import os
import shutil
import subprocess
import sys
import tarfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import atr                                  # noqa: E402
import deskrsc                              # noqa: E402
import mksdk                                # noqa: E402

BUILD = os.path.join(ROOT, "build")
TEMPLATE = os.path.join(ROOT, "tools", "dist", "README.md")

# (file in build/, where it goes, how it is read, what it says about itself)
DISKS = [
    ("gem-sp.atr", "disks/gem-sp.atr", "sdfs",
     "SpartaDOS 3.2 on a 320 KB floppy.  It boots straight into GEM: the "
     "disk carries STARTUP.BAT and AUTOEXEC.BAT, four bytes each, because "
     "SpartaDOS 3.2 reads the first and SpartaDOS X the second.  The "
     "desk accessory is on this one: pull down Desk and there is a "
     "Clock under the About item."),
    ("gem-boot.atr", "disks/gem-boot.atr", "dos2",
     "A double-density DOS 2 floppy, 180 KB.  GEM is AUTORUN.SYS, which "
     "this DOS runs at boot.  DUP.SYS is NOT on it -- the system wanted "
     "seven sectors more than the disk had left beside the DOS's own "
     "shell -- so there is nothing to come back to when GEM quits: use "
     "one of the other two, or the card, if that matters to you.  The "
     "system and nothing else: the calculator, the clock and the desk "
     "accessory are on the other two, because 3 KB is what this disk has "
     "left once GEM's 127 KB and the desktop are on it, and its "
     "GEM4XE.CFG is the short form -- the same keys without the prose, "
     "which is on the other two and in system/.  The Desk menu here "
     "holds only the About item."),
    ("gem-cf.img", "disks/gem-cf.img", None,
     "A 16 MB CF card: an APT partition table and two SDFS partitions, "
     "with the system and the desk accessory in `\\GEM\\` and the "
     "applications in `\\APPS\\`. "
     "It carries no DOS \u2014 SpartaDOS X and the PBI BIOS that mounts "
     "the partitions both come from Ultimate 1MB flash \u2014 so this "
     "one wants a U1MB machine."),
]

# (file in build/, the name it takes on a disk)
SYSTEM = [
    ("gem.xex", "GEM.COM"),
    ("desktop.g4a", "DESKTOP.G4A"),
    ("desktop.rsc", "DESKTOP.RSC"),
    ("lang.rsc", "LANG.RSC"),
    ("816.com", "816.COM"),
    ("gem4xe.cfg", "GEM4XE.CFG"),
    ("m11_app.g4a", "M11.G4A"),
    ("calc.g4a", "CALC.G4A"),
    ("calc.rsc", "CALC.RSC"),
    ("clock.g4a", "CLOCK.G4A"),
    ("clock.rsc", "CLOCK.RSC"),
    ("clockacc.g4a", "CLOCK.ACC"),
]

WHAT_IT_IS = {
    "GEM.COM": "the system: the VDI, the AES, GEMDOS and the shell",
    "DESKTOP.G4A": "the desktop, which is an application like any other",
    "DESKTOP.RSC": "its resource -- the menu, the dialogs, the icons",
    "LANG.RSC": "what the system says, so a translation is a file",
    "816.COM": "puts a Rapidus into 65C816 mode by hand, if the loader "
               "somehow does not",
    "GEM4XE.CFG": "the screen and the mouse, in plain text -- edit it from "
                  "the DOS prompt if the display comes up wrong.  Ships "
                  "with everything commented out and documented",
    "M11.G4A": "a small program, to have something to double-click",
    "CALC.G4A": "a calculator: whole numbers, and a division that "
                "truncates rather than pretending otherwise",
    "CALC.RSC": "its panel -- every key of it, and every word",
    "CLOCK.G4A": "a clock.  With an Ultimate 1MB it shows the time; "
                 "without one it counts up from midnight, which is what "
                 "the machine knows",
    "CLOCK.RSC": "its panel, and the templates that decide how a time "
                 "and a date are written",
    "CLOCK.ACC": "THE SAME CLOCK AS A DESK ACCESSORY.  Put it beside "
                 "GEM.COM, with CLOCK.RSC, and it appears in the Desk "
                 "menu: the AES loads it once at start-up and it stays "
                 "there, ticking, through every program the desktop runs. "
                 "An accessory goes in the system's own directory, never "
                 "in \\APPS\\ -- it is not something the desktop "
                 "launches",
}


def stamp():
    """A name for this build: the date, and the commit if there is one."""
    day = datetime.date.today().isoformat()
    try:
        sha = subprocess.run(["git", "-C", ROOT, "rev-parse", "--short",
                              "HEAD"], capture_output=True, text=True,
                             check=True).stdout.strip()
        dirty = subprocess.run(["git", "-C", ROOT, "status", "--porcelain"],
                               capture_output=True, text=True,
                               check=True).stdout.strip()
        return f"{day}-{sha}" + ("-dirty" if dirty else "")
    except (subprocess.CalledProcessError, FileNotFoundError):
        return day


def menu_items():
    """The desktop's menu, item by item, as the resource has it:
    (label, enabled).  NOT_YET is what the desktop disables at start."""
    r = deskrsc.build()
    first, _ = r.trees[deskrsc.ADMENU]
    items = []
    for idx in range(deskrsc.NOBS_MENU):
        o = r.objects[first + idx]
        s = getattr(o.spec, "s", None)
        if not isinstance(s, str) or not s.startswith("  ") or set(s) == {"-", " "}:
            continue
        items.append((idx, s.strip(), idx not in deskrsc.NOT_YET))
    return items


def bullets(items, want_enabled):
    out = []
    for _idx, label, enabled in items:
        if enabled == want_enabled:
            out.append(f"- **{label}**")
    return "\n".join(out)


def human(n):
    return f"{n / 1048576:.0f} MB" if n >= 1 << 20 else f"{n // 1024} KB"


def disk_section(src, dest, kind, prose):
    """One disk, with what is really on it read back out of the image."""
    lines = [f"### `{dest}` \u2014 {human(os.path.getsize(src))}",
             "", prose, ""]
    if kind == "dos2":
        fs = atr.Dos2(atr.ATRImage.load(src))
        names = sorted(e.filename for e in fs.entries() if e.in_use)
        free = fs.free_count()
        lines.append("Files: " + ", ".join(f"`{n}`" for n in names) + ".")
        lines.append("")
        lines.append(f"{free} sectors free — about {free * 253 // 1024} KB "
                     f"for programs of your own.")
    elif kind == "sdfs":
        fs = atr.Sdfs(atr.ATRImage.load(src))
        names = sorted(e.filename + ("\\" if e.is_dir else "")
                       for e in fs.entries(""))
        free = fs.free_count()
        lines.append("Files: " + ", ".join(f"`{n}`" for n in names) + ".")
        lines.append("")
        lines.append(f"{free} sectors free — about {free * 128 // 1024} KB.")
    return "\n".join(lines) + "\n"


def build(out, tar=None):
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    made, missing = [], []

    for src, dest, kind, prose in DISKS:
        p = os.path.join(BUILD, src)
        if not os.path.isfile(p):
            missing.append((dest, src))
            continue
        d = os.path.join(out, dest)
        os.makedirs(os.path.dirname(d), exist_ok=True)
        shutil.copyfile(p, d)
        made.append(disk_section(p, dest, kind, prose))

    sysrows = ["| file | | |", "|---|---|---|"]
    for src, name in SYSTEM:
        p = os.path.join(BUILD, src)
        if not os.path.isfile(p):
            raise SystemExit(f"mkdist: build/{src} is not built")
        os.makedirs(os.path.join(out, "system"), exist_ok=True)
        shutil.copyfile(p, os.path.join(out, "system", name))
        sysrows.append(f"| `{name}` | {os.path.getsize(p):,} bytes | "
                       f"{WHAT_IT_IS[name]} |")

    mksdk.build(os.path.join(out, "sdk", "gem4xe-sdk"),
                os.path.join(out, "sdk", "gem4xe-sdk.tar.gz"))
    shutil.rmtree(os.path.join(out, "sdk", "gem4xe-sdk"))
    shutil.copyfile(os.path.join(ROOT, "COPYING"),
                    os.path.join(out, "COPYING"))

    disks = "\n".join(made)
    if missing:
        disks += ("\n" + "\n".join(
            f"*(`{d}` is not in this build: `build/{s}` was not made — it "
            f"needs a DOS image this tree did not have.)*"
            for d, s in missing) + "\n")

    items = menu_items()
    with open(TEMPLATE) as f:
        page = f.read()
    page = page.format(stamp=stamp(), disks=disks,
                       system="\n".join(sysrows),
                       works=bullets(items, True),
                       notyet=bullets(items, False))
    with open(os.path.join(out, "README.md"), "w") as f:
        f.write(page)

    if tar:
        with tarfile.open(tar, "w:gz") as t:
            t.add(out, arcname=os.path.basename(out))
    return made, missing


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("out")
    ap.add_argument("--tar")
    a = ap.parse_args(argv[1:])
    made, missing = build(a.out, a.tar)
    print(f"{a.out}: {len(made)} disk(s), {len(SYSTEM)} system files, "
          f"the kit and the page"
          + (f"; {len(missing)} disk(s) not built" if missing else "")
          + (f"; {a.tar}" if a.tar else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
