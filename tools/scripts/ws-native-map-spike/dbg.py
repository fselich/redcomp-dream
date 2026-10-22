#!/usr/bin/env python3
"""Minimal line-oriented client for the snesrecomp debug server."""
import json
import socket
import sys
import time


class Dbg:
    def __init__(self, port=4377, host="127.0.0.1", timeout=30.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.buf = b""

    def close(self):
        self.sock.close()

    def cmd(self, command):
        self.sock.sendall(command.encode() + b"\n")
        while b"\n" not in self.buf:
            chunk = self.sock.recv(1 << 20)
            if not chunk:
                raise EOFError("debug server closed")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        text = line.decode("utf-8", "replace")
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            return {"raw": text}

    def wait_frame(self, target, timeout=120.0):
        end = time.time() + timeout
        while time.time() < end:
            r = self.cmd("frame")
            if r.get("frame", -1) >= target:
                return r.get("frame")
            time.sleep(0.005)
        raise TimeoutError(f"frame {target} not reached")


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 4377
    command = " ".join(sys.argv[2:]) if len(sys.argv) > 2 else "frame"
    dbg = Dbg(port)
    print(json.dumps(dbg.cmd(command), indent=2))
    dbg.close()


if __name__ == "__main__":
    main()
