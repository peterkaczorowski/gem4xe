#!/usr/bin/env python3
"""Read a .sym file written by tools/mkxex.py --syms."""


def load(path):
    syms = {}
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) == 2:
                syms[parts[0]] = int(parts[1], 16)
    return syms
