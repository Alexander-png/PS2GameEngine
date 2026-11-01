#!/usr/bin/python3
"""Virtual DualShock-style gamepad for driving PCSX2 (or anything that reads evdev).

Use /usr/bin/python3 (needs python-evdev from pacman, not the uv python).

  vpad.py selftest            create the device, press a button, read it back, exit
  vpad.py daemon              create the device and read commands from a FIFO
  vpad.py send "<command>"    write one or more commands (';' separated) to the FIFO

Commands (also accepted line by line by the daemon):
  tap <button> [ms]           press and release (default 80 ms)
  press <button> / release <button>
  stick <left|right> <x> <y>  0..255, 128 = centre
  dpad <up|down|left|right|none>
  wait <ms>
  quit

Buttons: cross circle square triangle l1 r1 l2 r2 l3 r3 select start
"""
import os, sys, time, select
import evdev
from evdev import UInput, ecodes as e, AbsInfo

NAME = "Claude Virtual DualShock"
BUTTONS = {
    "cross": e.BTN_SOUTH, "circle": e.BTN_EAST, "square": e.BTN_NORTH, "triangle": e.BTN_WEST,
    "l1": e.BTN_TL, "r1": e.BTN_TR, "l2": e.BTN_TL2, "r2": e.BTN_TR2,
    "select": e.BTN_SELECT, "start": e.BTN_START, "l3": e.BTN_THUMBL, "r3": e.BTN_THUMBR,
}
STICKS = {"left": (e.ABS_X, e.ABS_Y), "right": (e.ABS_RX, e.ABS_RY)}
FIFO = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "vpad.fifo")


def make_device():
    stick = AbsInfo(value=128, min=0, max=255, fuzz=0, flat=0, resolution=0)
    hat = AbsInfo(value=0, min=-1, max=1, fuzz=0, flat=0, resolution=0)
    cap = {
        e.EV_KEY: list(BUTTONS.values()),
        e.EV_ABS: [(e.ABS_X, stick), (e.ABS_Y, stick), (e.ABS_RX, stick), (e.ABS_RY, stick),
                   (e.ABS_HAT0X, hat), (e.ABS_HAT0Y, hat)],
    }
    return UInput(cap, name=NAME, vendor=0x1209, product=0x7a2e, version=1)


class Pad:
    def __init__(self):
        self.ui = make_device()

    def button(self, name, down):
        self.ui.write(e.EV_KEY, BUTTONS[name], 1 if down else 0)
        self.ui.syn()

    def stick(self, which, x, y):
        ax, ay = STICKS[which]
        self.ui.write(e.EV_ABS, ax, int(x))
        self.ui.write(e.EV_ABS, ay, int(y))
        self.ui.syn()

    def dpad(self, d):
        x = {"left": -1, "right": 1}.get(d, 0)
        y = {"up": -1, "down": 1}.get(d, 0)
        self.ui.write(e.EV_ABS, e.ABS_HAT0X, x)
        self.ui.write(e.EV_ABS, e.ABS_HAT0Y, y)
        self.ui.syn()

    def run(self, line):
        p = line.split()
        if not p:
            return True
        c = p[0].lower()
        if c == "tap":
            ms = int(p[2]) if len(p) > 2 else 80
            self.button(p[1], True); time.sleep(ms / 1000); self.button(p[1], False)
        elif c == "press":
            self.button(p[1], True)
        elif c == "release":
            self.button(p[1], False)
        elif c == "stick":
            self.stick(p[1], p[2], p[3])
        elif c == "dpad":
            self.dpad(p[1])
        elif c == "wait":
            time.sleep(int(p[1]) / 1000)
        elif c == "quit":
            return False
        else:
            print("unknown command:", line, file=sys.stderr)
        return True

    def close(self):
        self.ui.close()


def selftest():
    pad = Pad()
    time.sleep(0.5)  # let udev/evdev create the node
    path = next((d.path for d in map(evdev.InputDevice, evdev.list_devices()) if d.name == NAME), None)
    if not path:
        print("FAIL: device not visible")
        return 1
    dev = evdev.InputDevice(path)
    print("device:", dev.name, path)
    for cmd in ("tap cross 50", "dpad up", "dpad none"):
        pad.run(cmd)
    time.sleep(0.2)
    got = []
    while select.select([dev.fd], [], [], 0.3)[0]:
        got += [(ev.type, ev.code, ev.value) for ev in dev.read() if ev.type in (e.EV_KEY, e.EV_ABS)]
    print("events read back (type, code, value):", got)
    pad.close()
    want = [(e.EV_KEY, e.BTN_SOUTH, 1), (e.EV_KEY, e.BTN_SOUTH, 0), (e.EV_ABS, e.ABS_HAT0Y, -1)]
    ok = all(w in got for w in want)
    print("PASS" if ok else "FAIL: expected events not received")
    return 0 if ok else 1


def daemon():
    if os.path.exists(FIFO):
        os.remove(FIFO)
    os.mkfifo(FIFO, 0o600)
    pad = Pad()
    print("ready, fifo:", FIFO, flush=True)
    alive = True
    while alive:
        with open(FIFO) as f:  # blocks until a writer connects
            for line in f:
                for cmd in line.split(";"):
                    try:
                        if not pad.run(cmd):
                            alive = False
                    except Exception as ex:  # bad command must not kill the pad
                        print("bad command %r: %r" % (cmd, ex), flush=True)
    pad.close()
    os.remove(FIFO)


def send(text):
    with open(FIFO, "w") as f:
        f.write(text + "\n")


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    if mode == "selftest":
        sys.exit(selftest())
    elif mode == "daemon":
        daemon()
    elif mode == "send" and len(sys.argv) > 2:
        send(" ".join(sys.argv[2:]))
    else:
        print(__doc__)
