"""A DeviceLink.open() hibakezelese: a port ne ragadjon nyitva."""
import sys
from pathlib import Path
# A vizsgalt program a szulo konyvtarban van (tools/), a repo gyokere eggyel feljebb.
TOOLS = Path(__file__).resolve().parent.parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))

import sys, types, time
tk = types.ModuleType("tkinter"); tk.TclError = Exception
for n in ("Toplevel","Frame","Label","IntVar","Tk"): setattr(tk, n, object)
sys.modules["tkinter"] = tk
for sub in ("filedialog","messagebox","ttk"):
    m = types.ModuleType("tkinter."+sub); sys.modules["tkinter."+sub] = m; setattr(tk, sub, m)
tk.ttk.Frame = object
import zwift_config_gui as g

fails = []
def check(c, m):
    print(("  OK   " if c else "  HIBA ") + m)
    if not c: fails.append(m)

def make_fake(behaviour):
    closed = {"v": False}
    class Fake:
        def __init__(self, *a, **k): pass
        @property
        def is_open(self): return not closed["v"]
        def reset_input_buffer(self): pass
        def write(self, d): pass
        def flush(self): pass
        def close(self): closed["v"] = True
        def readline(self):
            time.sleep(0.02)
            return behaviour()
    return Fake, closed

print("[A] Nema eszkoz (PING idotullepes)")
Fake, closed = make_fake(lambda: b"")
g.serial = types.SimpleNamespace(Serial=lambda *a, **k: Fake())
link = g.DeviceLink()
try:
    link.open("FAKE"); check(False, "kellett volna hiba")
except g.DeviceError as e:
    check(True, "DeviceError: %s" % str(e)[:48])
check(closed["v"], "a soros port le lett zarva")
check(not link.connected, "link.connected == False")

print("\n[B] Idegen eszkoz (mas banner)")
Fake, closed = make_fake(lambda: b"OK VALAMI MAS ESZKOZ\r\n")
g.serial = types.SimpleNamespace(Serial=lambda *a, **k: Fake())
link = g.DeviceLink()
try:
    link.open("FAKE"); check(False, "kellett volna hiba")
except g.DeviceError as e:
    check("nem Zwift Buttons" in str(e), "helyes hibauzenet")
check(closed["v"] and not link.connected, "port lezarva, nincs kapcsolat")

print("\n[C] ERR valasz a PING-re")
Fake, closed = make_fake(lambda: b"ERR UNKNOWN\r\n")
g.serial = types.SimpleNamespace(Serial=lambda *a, **k: Fake())
link = g.DeviceLink()
try:
    link.open("FAKE"); check(False, "kellett volna hiba")
except g.DeviceError:
    check(True, "DeviceError dobva")
check(closed["v"] and not link.connected, "port lezarva, nincs kapcsolat")

print("\n[D] Sikertelen kapcsolat utan a muveletek nem futnak")
try:
    link.read_config(); check(False, "kellett volna hiba")
except g.DeviceError as e:
    check("Nincs kapcsolat" in str(e), "read_config: %s" % str(e)[:40])

print("\n" + ("HIBAK: %d" % len(fails) if fails else "MINDEN KAPCSOLAT-TESZT SIKERES"))
sys.exit(1 if fails else 0)
