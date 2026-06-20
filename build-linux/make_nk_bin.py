#!/usr/bin/env python3
import sys
import struct
import os

def data_sum(data):
    return sum(data) & 0xFFFFFFFF

def make_nk_bin(input_file, output_file, start_addr=0x80200000):
    if not os.path.exists(input_file):
        print(f"Error: {input_file} not found")
        sys.exit(1)

    with open(input_file, "rb") as f:
        data = f.read()

    # Align data to 4 bytes
    if len(data) % 4 != 0:
        data += b'\x00' * (4 - (len(data) % 4))

    image_len = len(data)
    
    with open(output_file, "wb") as f:
        # Magic
        f.write(b"B000FF\n")
        # Header: Image Start, Image Length
        f.write(struct.pack("<II", start_addr, image_len))
        
        # Record 1: The entire payload
        # Address, Length, Checksum
        f.write(struct.pack("<III", start_addr, image_len, data_sum(data)))
        f.write(data)
        
        # Terminator: Address 0, Length = Execution Address, Checksum = 0
        f.write(struct.pack("<III", 0, start_addr, 0))

    print(f"Successfully created {output_file}")
    print(f"  Start Address: 0x{start_addr:08X}")
    print(f"  Length:        {image_len} bytes")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: make_nk_bin.py <input_exe> <output_bin> [start_addr]")
        sys.exit(1)
    
    input_exe = sys.argv[1]
    output_bin = sys.argv[2]
    start_addr = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x80200000
    
    make_nk_bin(input_exe, output_bin, start_addr)
