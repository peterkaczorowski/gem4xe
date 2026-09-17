"""The compiler's dialect stays in src/portab.h.

gem4xe is built with one 65816 C compiler today, and the words that
compiler adds to C -- address-space qualifiers, calling-convention and
placement attributes, intrinsics -- are what a second compiler would
have to be taught.  src/portab.h maps each of them to one name (FAR,
TINY, SIMPLE_CALL, ...) so that teaching it is one block in one file.
That only holds while nothing else in the tree says `__far`, and this
is the check: every C source and header, every C the generators emit
and everything the application kit ships is read with its comments
removed, and any dialect word outside portab.h fails.

The assembly sources are not checked.  They are the compiler's own
assembler's, and a second toolchain gets a second set.
"""
import glob
import os
import re
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
PORTAB = os.path.join(ROOT, "src", "portab.h")
# ...and one other file is the compiler's seam by its whole purpose:
# src/app/gemstub.c answers the routines Calypsi's C library asks the
# board for, by the names that library uses.  A second toolchain does not
# want these renamed, it wants its own file -- so naming the vendor's
# header here is the point rather than a leak.
DIALECT_FILES = (PORTAB, os.path.join(ROOT, "src", "app", "gemstub.c"))

# The dialect, as words.  __attribute__ is in the list as a whole: the
# tree wraps every attribute it uses, so none should be visible.
DIALECT = re.compile(
    r"\b(?:__far24|__far|__near|__huge|__tiny|__task|__simple_call"
    r"|__attribute__|__memcpy_far|__memset_far"
    r"|__enable_interrupts|__disable_interrupts)\b"
    r"|\bcalypsi\b", re.I)

# The C the tree compiles -- what is checked in, and what the generators
# and the kit produce.
SOURCES = (glob.glob(os.path.join(ROOT, "src", "**", "*.[ch]"), recursive=True)
           + glob.glob(os.path.join(ROOT, "tests", "host", "*.c"))
           + glob.glob(os.path.join(ROOT, "tools", "sdk", "*.c")))

# The generators that write C, and the words they must write it with.
GENERATORS = [os.path.join(ROOT, "tools", g) for g in (
    "fontconv6.py", "fontconv.py", "font4x8.py", "patconv.py", "sinconv.py",
    "fselrsc.py", "langrsc.py", "gemdata.py", "mkg4a.py")]


def strip_comments(text):
    """C with its comments blanked, strings left alone."""
    out = []
    i, n = 0, len(text)
    while i < n:
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text[i] in "\"'":
            q = text[i]
            j = i + 1
            while j < n and text[j] != q:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def offenders(path, text):
    return [f"{os.path.relpath(path, ROOT)}:{k + 1}: {line.strip()}"
            for k, line in enumerate(strip_comments(text).splitlines())
            if DIALECT.search(line)]


class TestPortab(unittest.TestCase):

    def test_header_defines_the_dialect(self):
        """portab.h maps every name the tree uses, and refuses a compiler
        it does not know."""
        with open(PORTAB) as f:
            text = strip_comments(f.read())
        for name in ("FAR", "NEAR", "TINY", "SIMPLE_CALL", "TASK",
                     "SECTION(s)", "memcpy_far", "cpu_sei()", "cpu_cli()"):
            self.assertRegex(text, r"#define\s+" + re.escape(name) + r"\s",
                             f"portab.h does not define {name}")
        self.assertIn("#error", text)

    def test_no_dialect_outside_portab(self):
        """No C source or header says the compiler's words itself."""
        bad = []
        for path in sorted(SOURCES):
            if any(os.path.samefile(path, x) for x in DIALECT_FILES):
                continue
            with open(path) as f:
                bad += offenders(path, f.read())
        self.assertEqual(bad, [], "compiler dialect outside src/portab.h:\n"
                         + "\n".join(bad))

    def test_generators_emit_the_names(self):
        """The Python that writes C writes FAR, not __far -- and includes
        the header that defines it."""
        bad = []
        for path in GENERATORS:
            with open(path) as f:
                text = f.read()
            for k, line in enumerate(text.splitlines()):
                s = line.strip()
                if s.startswith("#"):
                    continue
                if DIALECT.search(line):
                    bad.append(f"{os.path.relpath(path, ROOT)}:{k + 1}: {s}")
            self.assertIn('portab.h', text,
                          f"{os.path.relpath(path, ROOT)} emits C without portab.h")
        self.assertEqual(bad, [], "compiler dialect in a generator:\n"
                         + "\n".join(bad))

    def test_uses_include_the_header(self):
        """A file that uses a portab.h name includes portab.h itself, not
        through whatever it happened to include first."""
        names = re.compile(r"\b(?:FAR|NEAR|TINY|SIMPLE_CALL|TASK|SECTION|"
                           r"memcpy_far|cpu_sei|cpu_cli)\b")
        bad = []
        for path in sorted(SOURCES):
            if os.path.samefile(path, PORTAB):
                continue
            with open(path) as f:
                text = strip_comments(f.read())
            if names.search(text) and '#include "portab.h"' not in text \
                    and '#include "gem.h"' not in text:
                bad.append(os.path.relpath(path, ROOT))
        self.assertEqual(bad, [], "uses portab.h names without including it:\n"
                         + "\n".join(bad))

    def test_kit_ships_the_header(self):
        """The application kit's gem.h includes portab.h, so the kit must
        carry it."""
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "mksdk", os.path.join(ROOT, "tools", "mksdk.py"))
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        self.assertIn(("include/portab.h", "src/portab.h"), mod.MANIFEST)


if __name__ == "__main__":
    unittest.main()
