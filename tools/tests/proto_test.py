"""A DeviceLink protokoll-logikájának ellenőrzése hamis soros porttal,
a firmware GET/SET/END válaszait utánozva."""
import sys
from pathlib import Path
# A vizsgalt program a szulo konyvtarban van (tools/), a repo gyokere eggyel feljebb.
TOOLS = Path(__file__).resolve().parent.parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))

import sys, os, types, json

# tkinter helyettesítése, hogy a modul importálható legyen fej nélkül is
tk = types.ModuleType("tkinter"); tk.TclError = Exception
for n in ("Toplevel","Frame","Label","IntVar","Tk"): setattr(tk, n, object)
sys.modules["tkinter"] = tk
for sub in ("filedialog","messagebox","ttk"):
    m = types.ModuleType("tkinter."+sub); sys.modules["tkinter."+sub] = m; setattr(tk, sub, m)
tk.ttk.Frame = object

import zwift_config_gui as g

class FakeSerial:
    """A firmware oldali parancsértelmezés minimál mása."""
    is_open = True
    def __init__(self):
        self.out = []
        self.map = {}
        for m in range(3):
            for b in range(5):
                for e in range(3):
                    self.map[(m,b,e)] = [0,0,0,0,60,0,0]
        self.map[(0,0,0)] = [1,0,0x50,0,60,0,0]
        self.map[(2,3,2)] = [2,0,0x00EA,1,70,0,0]
    def reset_input_buffer(self): self.out = []
    def flush(self): pass
    def close(self): self.is_open = False
    def write(self, data):
        cmd = data.decode().strip()
        # a firmware debug sorai bármikor beeshetnek
        self.out.append("Button 3 click.")
        head = cmd.split()[0].upper()
        if head == "PING":
            self.out.append("OK ZWIFT_BUTTONS PROTO=6 MODES=3 BUTTONS=5 EVENTS=3 SLOTS=2 CONNS=2")
        elif head == "DBG":
            self.out.append("OK DBG 0")
        elif head == "GET":
            for (m,b,e),v in sorted(self.map.items()):
                self.out.append("MAP %d %d %d %d %d %d %d %d %d %d" % (m,b,e,v[0],v[1],v[2],v[3],v[4],v[5],v[6]))
            for m in range(3):
                self.out.append("TARGET %d %d" % (m, 1 if m < 2 else 3))
            self.out.append("END")
        elif head == "SETTARGET":
            self.out.append("OK")
        elif head == "SET":
            p = [int(x) for x in cmd.split()[1:]]
            assert len(p) in (8, 9, 10), p
            while len(p) < 10: p.append(0)  # a cel es a kuldesi hossz elhagyhato
            self.map[(p[0],p[1],p[2])] = p[3:]
            self.out.append("OK")
        elif head == "SAVE":
            self.out.append("OK SAVED")
        elif head == "DEFAULTS":
            self.out.append("OK DEFAULTS")
        else:
            self.out.append("ERR UNKNOWN")
    def readline(self):
        return (self.out.pop(0) + "\r\n").encode() if self.out else b""

link = g.DeviceLink()
link.ser = FakeSerial()
assert link.command("PING").startswith("OK ZWIFT_BUTTONS PROTO=6")

km, targets = link.read_config()
assert km[0][0][0].type == g.ACT_KEY and km[0][0][0].code == 0x50, km[0][0][0].to_dict()
assert km[2][3][2].type == g.ACT_CONSUMER and km[2][3][2].repeat == 1 and km[2][3][2].repeat_ms == 70
print("GET/parse ok  ->", km[0][0][0].label(), "|", km[2][3][2].label())

km[1][2][1] = g.Action(g.ACT_KEY, 0x0C, 0x15, 0, 60)     # Alt+Win+R
km[0][4][1] = g.Action(g.ACT_VIEW_CYCLE)
steps = []
link.write_config(km, targets, progress=lambda d,t: steps.append((d,t)))
assert steps[-1] == (48,48), steps[-1]
assert link.ser.map[(1,2,1)] == [1,12,0x15,0,60,0,0], link.ser.map[(1,2,1)]
assert link.ser.map[(0,4,1)][0] == 4
print("SET/write ok  ->", steps[-1], "| celpontok:", targets)
assert targets == [1,1,3], targets

back, _ = link.read_config()
assert back[1][2][1].label() == "Ctrl+Alt+Win+R" or back[1][2][1].label() == "Alt+Win+R", back[1][2][1].label()
assert link.save_to_flash() == "OK SAVED"
print("roundtrip ok  ->", back[1][2][1].label(), "|", back[0][4][1].label())

# hibakezelés
class ErrSerial(FakeSerial):
    def write(self, data): self.out.append("ERR RANGE")
link.ser = ErrSerial()
try:
    link.command("SET 9 0 0 0 0 0 0 0"); raise SystemExit("nem dobott hibat")
except g.DeviceError as exc:
    print("hibakezeles ok ->", exc)

# JSON alapértelmezés betöltése
with open(str(TOOLS / "default_keymap.json"), encoding="utf-8") as fh:
    data = json.load(fh)
n = 0
for m in data["modes"]:
    for b in m["buttons"]:
        for k in ("click","double","long"):
            g.Action.from_dict(b[k]); n += 1
print("default_keymap.json ok ->", n, "bejegyzes")
