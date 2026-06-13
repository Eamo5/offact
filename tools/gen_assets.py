#!/usr/bin/env python3
import os
import sys

def generate(input_file, output_file, name):
    if not os.path.exists(input_file):
        print(f"error: {input_file} not found", file=sys.stderr)
        sys.exit(1)
    with open(input_file, "rb") as f:
        data = f.read()
    guard = f"ASSETS_{name.upper()}_H"
    with open(output_file, "w") as f:
        f.write(f"/* Auto-generated from {input_file}. */\n#ifndef {guard}\n#define {guard}\n\n")
        f.write(f"static const unsigned char assets_{name}[] = {{")
        for i, b in enumerate(data):
            if i % 12 == 0:
                f.write("\n    ")
            f.write(f"0x{b:02x}, ")
        f.write("\n};\n")
        f.write(f"static const unsigned int assets_{name}_len = {len(data)};\n\n#endif\n")

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("usage: gen_assets.py <input> <output> <name>", file=sys.stderr)
        sys.exit(1)
    generate(sys.argv[1], sys.argv[2], sys.argv[3])
