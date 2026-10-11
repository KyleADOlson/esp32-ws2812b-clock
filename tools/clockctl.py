"""Send console commands to the clock over USB serial without resetting it.

    python tools/clockctl.py COM3 status
    python tools/clockctl.py COM3 wifi "My Network" "my password"
    python tools/clockctl.py COM3 --listen 10      # just print output for 10s
"""
import argparse
import shlex
import sys
import time

import serial  # pyserial (bundled with the ESP-IDF Python env)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("command", nargs="*", help="console command and its arguments")
    ap.add_argument("--listen", type=float, default=2.0, help="seconds to read output")
    args = ap.parse_args()

    s = serial.Serial()
    s.port = args.port
    s.baudrate = 115200
    s.timeout = 0.1
    s.dtr = False  # don't toggle the chip's reset/boot lines
    s.rts = False
    s.open()

    if args.command:
        # The ESP console splits on spaces and honours "..." with \" and \\ escapes.
        line = " ".join('"' + a.replace("\\", "\\\\").replace('"', '\\"') + '"' for a in args.command)
        s.write(b"\r\n")
        time.sleep(0.2)
        s.reset_input_buffer()
        s.write(line.encode() + b"\r\n")

    end = time.time() + args.listen
    while time.time() < end:
        data = s.read(1024)
        if data:
            sys.stdout.write(data.decode(errors="replace"))
            sys.stdout.flush()
    s.close()


if __name__ == "__main__":
    main()
