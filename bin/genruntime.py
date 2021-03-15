#!/usr/bin/env python3

# genruntime.py - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
#
# This file is part of a2fomu which is released under the two clause BSD
# licence.  See file LICENSE in the project root directory or visit the
# project at https://github.com/elecbrick/a2fomu for full license details.

import sys, getopt
from enum import Enum
from struct import pack, unpack, iter_unpack

# Combine gateware, executable, and support files into a single dfu package
# Three types of files will be joined:

class Filetype(Enum):
    bitstream = 1
    executable = 2
    data = 3

def main(argv):
    global dfu
    outfile = 'build/gateware/a2fomu.bin'
    bitstream = 'build/gateware/top.bin'
    executable = '../sw/src/.obj/runtime.bin'
    exaddress = 0x10000000
    data = []
    default_data = (('deps/AppleWin/resource/Apple2_Plus.rom', 0xC000D000),
                    ('deps/AppleWin/resource/DISK2.rom',       0xC000C600))
    try:
        opts, args = getopt.getopt(argv,"shb:l:e:d:o:",
                ["loader=","executable=","data=","sim","help","ofile="])
    except getopt.GetoptError:
        print('Usage: genruntime.py -s -h')
        sys.exit(2)
    for opt, arg in opts:
        if opt in ('-h', "--help"):
            print('Usage: genruntime.py -s -h -b<file> -e[<addr>,]<file> -d<addr>,<file> -o<file>')
            print('    -b  --bitstream=<file>           bitstream')
            print('    -e  --executable=[<addr>,]<file> executable')
            print('    -d  --data=<addr>,<file>         data file')
            print('    -o  --ofile=<file>               output file')
            print('    -s  --sim                        simulator image')
            sys.exit()
        elif opt in ("-b", "--bitstream"):
            bitstream = arg
        elif opt in ("-s", "--sim"):
            bitstream = 'build/software/bios/bios.bin'
        elif opt in ("-e", "--executable"):
            executable = arg
        elif opt in ("-o", "--ofile"):
            outfile = arg
        elif opt in ("-d", "--data"):
            if(arg == ''):
                default_data = ()
            else:
                data.push(split(',', arg, 2))
    print("  Creating {:s}".format(outfile))
    dfu = open(outfile, 'wb')
    combine(bitstream, Filetype.bitstream)
    combine(executable, Filetype.executable, exaddress)
    if(len(data)>0):
        for filename, address in data:
            combine(filename, Filetype.data, address)
    else:
        for filename, address in default_data:
            combine(filename, Filetype.data, address)
    # Pad dummy bytes to ensure simulation does not read XXXXXXXX
    dfu.write(int_to_bytes(0xffffffff))

def combine(filename, filetype, location=None):
    print("  Processing {:s}".format(filename))
    if(filetype == Filetype.bitstream):
        gateware = open(filename, 'rb')
        dfu.write(gateware.read())
        misalligned = dfu.tell()%4
        if misalligned:
            dfu.write(bytes(4-misalligned))
    else:
        magic = 0xa2f06502
        if(filetype == Filetype.executable):
            magic = 0xa2f0abe0
        software = open(filename, 'rb')
        obj = software.read()
        misalligned = len(obj)%4
        if misalligned:
            obj += bytes(4-misalligned)
        dfu.write(int_to_bytes(magic))
        dfu.write(int_to_bytes(location))
        dfu.write(int_to_bytes(len(obj)))
        dfu.write(checksum(obj))
        dfu.write(obj)

def checksum(stream):
    #n=4
    #cs= -sum([bytes_to_int([stream[i:i+n]]) for i in range(0, len(stream), n)])
    #return int_to_bytes(cs, 4)
    cs = sum(tuple(unpack("<{:n}L".format((len(stream)+3)//4), stream)))
    cs = (-cs)&0xffffffff
    return pack("<L", cs)

def bytes_to_int(bytes):
    #result = 0
    #for b in bytes:
    #    result = result * 256 + int(b)
    #return result
    return unpack("<L", bytes)

def int_to_bytes(value):
    #result = []
    #for i in range(0, 4):
    #    result.append(value >> (i * 8) & 0xff)
    #return bytes(result.reverse())
    return pack("<L", value)

if __name__ == "__main__":
   main(sys.argv[1:])
