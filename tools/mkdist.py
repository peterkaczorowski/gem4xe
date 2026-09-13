#!/usr/bin/env python3
"""The gem4xe distribution: what a tester is handed.

    tools/mkdist.py build/gem4xe-<stamp> [--tar out.tar.gz] [--public]

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

--public is the release (`make release`): the same, without the two
floppies, which boot a DOS that is not gem4xe's to give away, and with
the page's few passages about them saying so instead.
"""
import argparse
import datetime
import os
import shutil
import subprocess
import sys
import tarfile
import textwrap

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import atr                                  # noqa: E402
import deskrsc                              # noqa: E402
import mkcf                                 # noqa: E402
import mksdk                                # noqa: E402

BUILD = os.path.join(ROOT, "build")
TEMPLATE = os.path.join(ROOT, "tools", "dist", "README.md")

# The two floppies boot a DOS that is not gem4xe's to give away
# (fixtures.toml.example: "a commercial or shareware Atari image you
# already have").  A build handed to a tester in the room carries them;
# the public release (--public, `make release`) does not, and its page
# says so and says how to make one.  The card image is built by
# tools/mkcf.py from this tree's own files and carries no DOS -- the
# SpartaDOS X it runs under comes from the machine's own flash -- so it
# travels in both.
THIRD_PARTY_DOS = {"gem-sp.atr", "gem-boot.atr"}

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


def card_layout():
    """What goes in which directory of the card, read from the tool that
    builds it, so the page's install-by-hand recipe is the card's own."""
    dirs = {}
    for _path, name in mkcf.SYSTEM:
        d, n = name.split(">")
        dirs.setdefault(d, []).append(n)
    return dirs


# The passages of the page that differ between the build handed to a
# tester (which has the floppies) and the public release (which has not).
# Everything else on the page is the same text.
TESTER = {
    "emu_disk": "--disk disks/gem-sp.atr",
    "emu_note": "",
    "install": (
        "**If you already have an APT drive**, do not write the card image "
        "over\nit.  `gem-sp.atr` is an install disk: it holds the same "
        "`\\GEM\\` and\n`\\APPS\\` the card does, so putting gem4xe on your "
        "own drive is a\ndirectory copy --\n\n"
        "    COPY D1:>GEM>*.* D2:>GEM>*.*\n"
        "    COPY D1:>APPS>*.* D2:>APPS>*.*\n\n"
        "-- plus an `AUTOEXEC.BAT` holding the two lines the floppy's "
        "holds,\n`CD >GEM` and `GEM`.\n\n"
        "If what you have is a **loader that reads FAT** -- a SIDE3, an "
        "AVGCART\n-- or an SDrive-MAX, a FujiNet or a real drive, then the "
        "floppies are\nwhat you want: copy `gem-sp.atr` onto the card you "
        "already have and\nload it like anything else.  `docs/media.md` in "
        "the source tree has the\nwhole matrix and the reasoning."),
    "dosnote": (
        "**The DOS on each disk image is not gem4xe's**, and is there so "
        "that the\ndisk boots.  Whoever owns it owns it; the images are for "
        "trying this\nout, not for redistribution."),
}


def public_text():
    """The same passages for the release, wrapped here because two of
    them carry lists read out of tools/mkcf.py."""
    layout = card_layout()
    gemdir = ", ".join(f"`{n}`" for n in layout["GEM"])
    appsdir = ", ".join(f"`{n}`" for n in layout["APPS"])
    boot = " and ".join(f"`{line}`" for line in mkcf.BOOT)
    fill = lambda t: textwrap.fill(t, 72)  # noqa: E731
    return {
        "emu_disk": "--disk gem.atr",
        "emu_note": fill(
            "`gem.atr` is a disk of your own making, because this download "
            "carries no DOS (*What is on the disks*, below): a bootable "
            "SpartaDOS 3.2 or DOS 2 disk with the files in `system/` on "
            "it, started at boot the way *Booting* describes or by hand "
            "from the DOS prompt.") + "\n",
        "install": fill(
            "**If you already have an APT drive**, do not write the card "
            "image over it: make `\\GEM\\` and `\\APPS\\` on it and "
            "copy the files in `system/` the way the card has them -- "
            f"{gemdir} into `\\GEM\\`; {appsdir} into `\\APPS\\` -- "
            f"plus an `AUTOEXEC.BAT` of two lines, {boot}.") + "\n\n" + fill(
            "If what you have is a **loader that reads FAT** -- a SIDE3, an "
            "AVGCART -- or an SDrive-MAX, a FujiNet or a real drive, then "
            "a floppy is what you want, and this download has none: the "
            "two that `make dist` builds boot a DOS that is not gem4xe's "
            "to give away.  Put the files in `system/` on a SpartaDOS 3.2 "
            "or DOS 2 disk of your own -- from the source tree, `make "
            "dist` does exactly that, given your DOS images in "
            "`fixtures.toml` -- and `docs/media.md` there has the whole "
            "matrix and the reasoning."),
        "dosnote": fill(
            "**No disk in this download carries a DOS.**  The card image "
            "runs under the SpartaDOS X in your Ultimate 1MB's flash, and "
            "the two floppies `make dist` also builds, `gem-sp.atr` and "
            "`gem-boot.atr`, are not here because each boots a DOS that is "
            "not gem4xe's to give away."),
    }


def build(out, tar=None, require_clean=False, public=False):
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    made, missing, left_out = [], [], []

    for src, dest, kind, prose in DISKS:
        p = os.path.join(BUILD, src)
        if public and src in THIRD_PARTY_DOS:
            left_out.append(dest)
            continue
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

    # THE SOURCE TRAVELS WITH THE BINARIES, because the GPL says it must
    # and because there is nowhere else to point: section 3 wants the
    # source alongside or a written offer, and a URL is neither.
    # `git archive` is exactly what is committed, so the tarball and the
    # stamp in this page's title describe the same tree.
    src_tar = os.path.join(out, "src", "gem4xe-src.tar.gz")
    os.makedirs(os.path.dirname(src_tar), exist_ok=True)
    # A DIRTY TREE CANNOT BE RELEASED.  `git archive` writes what is
    # COMMITTED, so on a dirty tree the tarball would not be the source
    # these binaries were built from -- which is the one thing it exists
    # to be.  `make all` and the individual disk targets are what to use
    # mid-change; this one wants a commit.
    if require_clean and stamp().endswith("-dirty"):
        raise SystemExit(
            "mkdist: the working tree has uncommitted changes, so the "
            "source tarball would not match the binaries beside it.  "
            "Commit first (docs/licence.md, 'the source travels with the "
            "binaries'), or build the disks on their own with `make all`.")
    ident = "gem4xe-" + stamp()
    r = subprocess.run(["git", "archive", "--format=tar.gz",
                        f"--prefix={ident}/", "HEAD"],
                       cwd=ROOT, capture_output=True)
    if r.returncode:
        raise SystemExit("mkdist: git archive failed: "
                         + r.stderr.decode("utf-8", "replace")[:300])
    with open(src_tar, "wb") as f:
        f.write(r.stdout)

    # ...and the version on its own, so a tester who has unpacked the
    # folder and forgotten where it came from can still quote one.  BOTH
    # numbers, because they answer different questions: the release is
    # what the About box shows and what a human says out loud, and the
    # date and commit are what identifies the build exactly.
    with open(os.path.join(out, "VERSION"), "w") as f:
        f.write(f"{deskrsc.VERSION} ({stamp()})\n")

    disks = "\n".join(made)
    if missing:
        disks += ("\n" + "\n".join(
            f"*(`{d}` is not in this build: `build/{s}` was not made — it "
            f"needs a DOS image this tree did not have.)*"
            for d, s in missing) + "\n")
    if left_out:
        disks += ("\n*(" + " and ".join(f"`{d}`" for d in left_out)
                  + ", the floppies `make dist` builds, are not in this "
                  "download: each boots a DOS that is not gem4xe's to give "
                  "away.  *On real storage*, above, says how to make one "
                  "from `system/`.)*\n")

    items = menu_items()
    with open(TEMPLATE) as f:
        page = f.read()
    page = page.format(version=deskrsc.VERSION, stamp=stamp(), disks=disks,
                       system="\n".join(sysrows),
                       works=bullets(items, True),
                       notyet=bullets(items, False),
                       **(public_text() if public else TESTER))
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
    ap.add_argument("--public", action="store_true",
                    help="the release: without the floppies that boot a "
                         "DOS which is not gem4xe's to give away")
    a = ap.parse_args(argv[1:])
    made, missing = build(a.out, a.tar, require_clean=True, public=a.public)
    print(f"{a.out}: {len(made)} disk(s), {len(SYSTEM)} system files, "
          f"the kit and the page"
          + (f"; {len(missing)} disk(s) not built" if missing else "")
          + ("; the floppies left out" if a.public else "")
          + (f"; {a.tar}" if a.tar else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
