#!/usr/bin/env python3
"""conref.py -- the console's model: a VT-52 on GEM's screen.

What GEMDOS's character calls draw (src/sys/con.c), for
tests/emu/m32_con.py to hold the target's screen against.  WHERE things
go is written from The Atari Compendium's table of VT-52 escapes (3.13 -
3.14) and from the choices src/sys/con.h lists where gem4xe is not the
ST: the colours are VDI indices, the cursor is a solid block shown only
while a program waits for a key, line wrap starts off, and a TAB is the
spaces GEMDOS's own output makes of it.

HOW things are drawn has to be the target's, or the pixels could not
agree, so it goes through the AES model's waist (tools/aesref.py) in the
shapes con.c uses: a cell's background a solid fill in the paper colour,
its characters a transparent text run in the ink, a scroll one
screen-to-screen blit, the cursor an XOR fill -- each write with the
pointer hidden and the clip opened to the whole screen.
"""
from aesref import Rect
from vdiref import MD_REPLACE, MD_TRANS, MD_XOR, FIS_SOLID

COLS_MAX = 80                   # a row's most: 640 / 8
ESC = 0x1B


class Console:
    def __init__(self, a):
        self.a = a
        self.cw, self.ch = a.gl_wchar, a.gl_hchar
        self.cols = min(a.gl_width // self.cw, COLS_MAX)
        self.rows = a.gl_height // self.ch
        self.reset()

    def reset(self):
        """The console a program starts with (con_reset): home, ink 1 on
        paper 0, wrap and inverse off, the cursor enabled."""
        self.col = self.row = 0
        self.ink, self.paper = 1, 0
        self.wrap = self.rev = False
        self.cursor = True
        self.esc = None             # None, "code", "row", "col", "ink", "paper"
        self.yrow = 0
        self.saved = (0, 0)
        self._drawing = False
        self._run, self._rcol = [], 0

    # -- drawing ------------------------------------------------------------

    def _begin(self):
        if self._drawing:
            return
        self._drawing = True
        a = self.a
        a.gsx_moff()
        self._clip = a.gsx_gclip()
        a.gsx_sclip(Rect(0, 0, a.gl_width, a.gl_height))

    def _end(self):
        if not self._drawing:
            return
        self._drawing = False
        self.a.gsx_sclip(self._clip)
        self.a.gsx_mon()

    def _erase(self, col, row, ncols, nrows):
        if ncols <= 0 or nrows <= 0:
            return
        self._begin()
        self.a.gsx_fcolor(self.paper)
        self.a.bb_fill(MD_REPLACE, FIS_SOLID, 0, col * self.cw, row * self.ch,
                       ncols * self.cw, nrows * self.ch)

    def _scroll_up(self, top):
        """The rows below `top` up one; the last row blank."""
        last = self.rows - 1
        if top < last:
            self._begin()
            self.a.bb_screen(0, (top + 1) * self.ch, 0, top * self.ch,
                             self.cols * self.cw, (last - top) * self.ch)
        self._erase(0, last, self.cols, 1)

    def _scroll_down(self, top):
        """The rows from `top` down one, the last lost; `top` blank."""
        last = self.rows - 1
        if top < last:
            self._begin()
            self.a.bb_screen(0, top * self.ch, 0, (top + 1) * self.ch,
                             self.cols * self.cw, (last - top) * self.ch)
        self._erase(0, top, self.cols, 1)

    def _flush(self):
        if not self._run:
            return
        ink, paper = self.ink, self.paper
        if self.rev:
            ink, paper = paper, ink
        a = self.a
        x, y, n = self._rcol * self.cw, self.row * self.ch, len(self._run)
        self._begin()
        a.gsx_fcolor(paper)
        a.bb_fill(MD_REPLACE, FIS_SOLID, 0, x, y, n * self.cw, self.ch)
        a.gsx_attr(True, MD_TRANS, ink)
        a.intin = list(self._run)
        a.gsx_tblt(0, x, y, n)
        self._run = []

    # -- the terminal --------------------------------------------------------

    def _newline(self):
        if self.row + 1 < self.rows:
            self.row += 1
        else:
            self._scroll_up(0)

    def _put(self, ch):
        """A printable character.  At the right edge the cursor waits
        past the last cell, and the next character wraps (ESC v) or
        overwrites that cell (ESC w)."""
        if self.col >= self.cols:
            self._flush()
            if self.wrap:
                self.col = 0
                self._newline()
            else:
                self.col = self.cols - 1
        if not self._run:
            self._rcol = self.col
        self._run.append(ch)
        self.col += 1

    def _escape(self, ch):
        state, self.esc = self.esc, None
        last = self.rows - 1
        if state == "row":
            self.yrow, self.esc = ch, "col"
            return
        if state == "col":                  # ESC Y: row then column, from 32
            self.row = max(0, min(self.yrow - 32, last))
            self.col = max(0, min(ch - 32, self.cols - 1))
            return
        if state == "ink":                  # ESC b: the low four bits
            self.ink = ch & 0x0F
            return
        if state == "paper":                # ESC c
            self.paper = ch & 0x0F
            return
        self.col = min(self.col, self.cols - 1)
        col, row, c = self.col, self.row, chr(ch)
        if c == "A":                        # up, stopping at the top
            self.row = max(row - 1, 0)
        elif c == "B":                      # down, stopping at the bottom
            self.row = min(row + 1, last)
        elif c == "C":                      # right, stopping at the edge
            self.col = min(col + 1, self.cols - 1)
        elif c == "D":                      # left
            self.col = max(col - 1, 0)
        elif c == "E":                      # clear the screen, home
            self._erase(0, 0, self.cols, self.rows)
            self.col = self.row = 0
        elif c == "H":                      # home
            self.col = self.row = 0
        elif c == "I":                      # up, scrolling down at the top
            if row > 0:
                self.row -= 1
            else:
                self._scroll_down(0)
        elif c == "J":                      # erase to the end of the screen
            self._erase(col, row, self.cols - col, 1)
            self._erase(0, row + 1, self.cols, last - row)
        elif c == "K":                      # erase to the end of the line
            self._erase(col, row, self.cols - col, 1)
        elif c == "L":                      # insert a line at the cursor
            self._scroll_down(row)
            self.col = 0
        elif c == "M":                      # delete the cursor's line
            self._scroll_up(row)
            self.col = 0
        elif c == "Y":
            self.esc = "row"
        elif c == "b":
            self.esc = "ink"
        elif c == "c":
            self.esc = "paper"
        elif c == "d":                      # erase from the top to the cursor
            self._erase(0, 0, self.cols, row)
            self._erase(0, row, col + 1, 1)
        elif c == "e":
            self.cursor = True
        elif c == "f":
            self.cursor = False
        elif c == "j":
            self.saved = (col, row)
        elif c == "k":
            self.col, self.row = self.saved
        elif c == "l":                      # erase the line, cursor to its start
            self._erase(0, row, self.cols, 1)
            self.col = 0
        elif c == "o":                      # erase from the line's start to the cursor
            self._erase(0, row, col + 1, 1)
        elif c == "p":
            self.rev = True
        elif c == "q":
            self.rev = False
        elif c == "v":
            self.wrap = True
        elif c == "w":
            self.wrap = False

    def write(self, data):
        """Bytes to the screen, as con_write does with them."""
        for ch in data:
            if self.esc is not None:
                self._flush()
                self._escape(ch)
                continue
            if ch >= 0x20:
                self._put(ch)
                continue
            self._flush()
            if ch == 0x08:                  # BS
                self.col = min(self.col, self.cols - 1)
                self.col = max(self.col - 1, 0)
            elif ch == 0x09:                # TAB: spaces to the next stop
                while True:
                    self._put(0x20)
                    if not (self.col & 7 and self.col < self.cols):
                        break
                self._flush()
            elif ch in (0x0A, 0x0B, 0x0C):  # LF, VT, FF
                self._newline()
            elif ch == 0x0D:
                self.col = 0
            elif ch == ESC:
                self.esc = "code"
        self._flush()
        self._end()

    # -- waiting for a key ----------------------------------------------------

    def _block(self, col, row):
        col = min(col, self.cols - 1)
        self._begin()
        self.a.gsx_fcolor(1)
        self.a.bb_fill(MD_XOR, FIS_SOLID, 0, col * self.cw, row * self.ch,
                       self.cw, self.ch)
        self._end()

    def wait(self):
        """A program starts to wait for a key: the cursor goes on where
        the next character would go, if it is enabled.  Returns what
        waited() takes back."""
        where = (self.col, self.row) if self.cursor else None
        if where:
            self._block(*where)
        return where

    def waited(self, where):
        """...and the key came: the block comes off where it went on."""
        if where:
            self._block(*where)
