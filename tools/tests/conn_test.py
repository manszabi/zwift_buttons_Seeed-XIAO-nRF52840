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

print("\n[E] A kapcsolat elszakad muvelet kozben")
class Breaking:
    """Elso ket olvasas rendben (PING, DBG), utana kivetel - mint egy kihuzott USB."""
    def __init__(s, mode): s.mode = mode; s.reads = 0; s.out = []; s.is_open = True
    def reset_input_buffer(s): pass
    def flush(s): pass
    def close(s): s.is_open = False
    def write(s, data):
        cmd = data.decode().strip().split()[0].upper()
        if s.mode == "write" and s.reads >= 2:
            raise OSError("device disconnected")
        if cmd == "PING":
            s.out.append("OK ZWIFT_BUTTONS PROTO=7 MODES=3 BUTTONS=5 EVENTS=3 SLOTS=2 CONNS=2")
        elif cmd == "DBG": s.out.append("OK DBG 0")
        else: s.out.append("OK")
    def readline(s):
        s.reads += 1
        if s.reads > 2: raise OSError("device disconnected")
        return (s.out.pop(0) + "\r\n").encode() if s.out else b""

for mode, muvelet in (("read", "beolvasas"), ("write", "kuldes")):
    ser = Breaking(mode)
    g.serial = types.SimpleNamespace(Serial=lambda *a, **k: ser)
    link = g.DeviceLink(); link.open("FAKE")
    check(link.connected, "%s: sikeres csatlakozas" % muvelet)
    try:
        if mode == "read": link.read_config()
        else: link.write_config(g.empty_keymap(), g.default_targets())
        check(False, "%s: kellett volna hiba" % muvelet)
    except g.DeviceError as exc:
        check("kapcsolat megszakadt" in str(exc),
              "%s: a hibauzenet megmondja, mi tortent" % muvelet)
    check(not link.connected,
          "%s: a kapcsolat le is zarult (nem hiszi magat csatlakozottnak)" % muvelet)

print("\n[F] A memoriaba mentes elveszett valasza nem allit biztosat")
class SilentSave(Breaking):
    def readline(s):
        s.reads += 1
        if s.reads > 2: return b""          # elnemul: nem kivetel, csak nincs valasz
        return (s.out.pop(0) + "\r\n").encode() if s.out else b""
ser = SilentSave("read")
g.serial = types.SimpleNamespace(Serial=lambda *a, **k: ser)
link = g.DeviceLink(); link.open("FAKE")
try:
    link.save_to_flash()
    check(False, "kellett volna hiba")
except g.DeviceError as exc:
    check("bizonytalan" in str(exc),
          "a mentes eredmenye bizonytalankent jelenik meg, nem hibakent")

print("\n" + ("HIBAK: %d" % len(fails) if fails else "MINDEN KAPCSOLAT-TESZT SIKERES"))
sys.exit(1 if fails else 0)
