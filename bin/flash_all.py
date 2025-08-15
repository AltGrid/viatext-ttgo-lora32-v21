#!/usr/bin/env python3
import glob
import subprocess

ports = glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*")
print("Found ports:", ports)

for port in ports:
    print(f"Flashing {port}...")
    subprocess.run([
        "pio", "run",
        "-t", "upload",
        "--upload-port", port
    ])
