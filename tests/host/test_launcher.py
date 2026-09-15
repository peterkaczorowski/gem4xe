#!/usr/bin/env python3
"""Host tests for the emulator launcher's refusals (tools/a8test/launcher.py).

Two ways the rig used to fail slowly and misleadingly instead of at once:

* **An emulator without tools/altirra/'s patches.**  Upstream PR #88 brought
  KEYRAW in the same change as two 65C816 native-mode CPU fixes, so a build
  that does not answer KEYRAW has the IRQ storm that walks the stack through
  bank $00.  It is intermittent and load-dependent, so a gate can pass on
  it.  bridge.py could already tell the builds apart -- and used that only
  to cross two keyboard bits back.
* **A socket path the kernel cannot bind.**  sun_path holds 108 bytes with
  its NUL.  Past that AltirraSDL comes up and never listens, and the
  launcher waited out its timeout and blamed a missing token.

Neither refusal needs an emulator to test: the path is checked before one is
started, and the patch check runs against a stand-in bridge.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
from a8test import launcher                     # noqa: E402
from a8test.bridge import BridgeError           # noqa: E402


class StubBridge:
    def __init__(self, keyraw):
        self.keyraw = keyraw

    def has_keyraw(self):
        return self.keyraw


class TestSocketPath(unittest.TestCase):
    def test_the_longest_bindable_path_is_accepted(self):
        launcher.check_socket_path("/" + "a" * (launcher.SUN_PATH_MAX - 1))

    def test_one_byte_more_is_refused_and_says_how_long(self):
        sock = "/" + "a" * launcher.SUN_PATH_MAX
        with self.assertRaises(BridgeError) as e:
            launcher.check_socket_path(sock)
        self.assertIn(f"{len(sock)} bytes", str(e.exception))

    def test_launch_refuses_before_making_or_starting_anything(self):
        deep = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, deep)
        root = os.path.join(deep, "x" * 120)
        with mock.patch.object(launcher, "ROOT", root), \
             mock.patch.object(subprocess, "Popen",
                               side_effect=AssertionError("an emulator was started")):
            with self.assertRaises(BridgeError):
                launcher.launch(tag="toolong")
        self.assertFalse(os.path.exists(os.path.join(root, "build")),
                         "a run directory was made for a path that cannot work")


class TestPatchedEmulator(unittest.TestCase):
    def test_a_build_without_keyraw_is_refused_for_its_cpu(self):
        with self.assertRaises(BridgeError) as e:
            launcher.check_patched(StubBridge(False), exe="/usr/bin/AltirraSDL")
        msg = str(e.exception)
        self.assertIn("/usr/bin/AltirraSDL", msg)       # which build
        self.assertIn("65C816", msg)                    # the real reason
        self.assertIn("ALTIRRASDL=", msg)               # what to do

    def test_a_patched_build_passes(self):
        launcher.check_patched(StubBridge(True))

    def test_launch_stops_an_unpatched_emulator_rather_than_leaving_it(self):
        stopped = []

        class FakeProc:
            pid = 4242

        class FakeEmu:
            def __init__(self, proc, run_dir, bridge):
                self.bridge = bridge

            def stop(self):
                stopped.append(True)

        with tempfile.TemporaryDirectory() as root, \
             mock.patch.object(launcher, "ROOT", root), \
             mock.patch.object(subprocess, "Popen", return_value=FakeProc()), \
             mock.patch.object(launcher, "find_token", return_value=("addr", "tok")), \
             mock.patch.object(launcher, "Bridge", return_value=StubBridge(False)), \
             mock.patch.object(launcher, "Emu", FakeEmu):
            with self.assertRaises(BridgeError):
                launcher.launch(tag="unpatched")
        self.assertEqual(stopped, [True], "the refused emulator was left running")


class TestWhichEmulator(unittest.TestCase):
    """ALTIRRASDL is read at launch.  A script that imported the launcher
    first and set the variable after used to get the build on PATH."""

    def test_the_variable_set_after_import_is_the_one_launched(self):
        seen = []

        class Stop(Exception):
            pass

        def popen(args, **kw):
            seen.append(args[0])
            raise Stop

        with tempfile.TemporaryDirectory() as root, \
             mock.patch.dict(os.environ, {"ALTIRRASDL": "/set/after/import/AltirraSDL"}), \
             mock.patch.object(launcher, "ROOT", root), \
             mock.patch.object(subprocess, "Popen", side_effect=popen):
            with self.assertRaises(Stop):
                launcher.launch(tag="which")
        self.assertEqual(seen, ["/set/after/import/AltirraSDL"])

    def test_the_refusal_names_the_build_actually_asked_for(self):
        with mock.patch.dict(os.environ, {"ALTIRRASDL": "/set/after/import/AltirraSDL"}):
            with self.assertRaises(BridgeError) as e:
                launcher.check_patched(StubBridge(False))
        self.assertIn("/set/after/import/AltirraSDL", str(e.exception))


if __name__ == "__main__":
    unittest.main()
