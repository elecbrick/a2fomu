#!/usr/bin/env python3

# bin2hex.py - Part of a2fomu but taken directly from the foboot file HACKING.md
#              Foboot is released under Version 2.0 of the Apache License
#
# a2fomu is Copyright (c) 2020-2021 Doug Eaton

import sys

# Convert a binary file into mem.init hex format needed by 

def swap32(d):
    d = list(d)
    while len(d) < 4:
        d.append(0)
    t = d[0]
    d[0] = d[3]
    d[3] = t

    t = d[1]
    d[1] = d[2]
    d[2] = t
    return bytes(d)

if(len(sys.argv)<=2):
    print("Usage: bin2hex.py <infile> <outfile>", file=sys.stderr)
    exit(1)

with open(sys.argv[1], "rb") as inp:
    with open(sys.argv[2], "w", newline="\n") as outp:
        while True:
            d = inp.read(4)
            if len(d) == 0:
                break
            print(swap32(d).hex(), file=outp)

