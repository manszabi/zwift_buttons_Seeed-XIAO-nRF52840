"""Integracios teszt: a Python DeviceLink a VALODI firmware-kodhoz beszel."""
import sys
from pathlib import Path
# A vizsgalt program a szulo konyvtarban van (tools/), a repo gyokere eggyel feljebb.
TOOLS = Path(__file__).resolve().parent.parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))

import subprocess, sys, types, os, json
tk = types.ModuleType("tkinter"); tk.TclError = Exception
for n in ("Toplevel","Frame","Label","IntVar","Tk"): setattr(tk, n, object)
sys.modules["tkinter"] = tk
for sub in ("filedialog","messagebox","ttk"):
    m = types.ModuleType("tkinter."+sub); sys.modules["tkinter."+sub] = m; setattr(tk, sub, m)
tk.ttk.Frame = object
import zwift_config_gui as g
from hid_tables import (MODE_NAMES, EVENT_NAMES,
                        ACT_KEY, ACT_CONSUMER, ACT_VIEW_CYCLE, MOD_LCTRL, MOD_LSHIFT,
                        TARGET_PC, TARGET_PHONE, TARGET_ALL, TARGET_INHERIT, SLOT_PC, SLOT_PHONE,
                        REPEAT_ENABLED, REPEAT_RELEASE, REPEAT_HOLD_MOD)

fails = []
def check(c, m):
    print(("  OK   " if c else "  HIBA ") + m)
    if not c: fails.append(m)

class ProcSerial:
    is_open = True
    def __init__(self, exe):
        self.p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    def reset_input_buffer(self): pass
    def flush(self): self.p.stdin.flush()
    def write(self, data): self.p.stdin.write(data); self.p.stdin.flush()
    def readline(self): return self.p.stdout.readline()
    def close(self): self.is_open = False; self.p.stdin.close(); self.p.wait()

link = g.DeviceLink()
link.ser = ProcSerial(str(Path(__file__).resolve().parent / "bridge"))

print("[1] Kezfogas")
info = link.command("PING")
check(info.startswith("OK ZWIFT_BUTTONS PROTO=6"), "PING: " + info)
check("SLOTS=2" in info and "CONNS=2" in info, "az eszkoz jelenti a fiokok/kapcsolatok szamat")
link.command("DBG 0")

print("\n[2] Kiosztas + celpontok beolvasasa")
km, targets = link.read_config()
check(km[0][0][0].label() == "Balra nyíl", "Normal/G1/rovid = " + km[0][0][0].label())
check(targets == [TARGET_PC, TARGET_PC, TARGET_ALL],
      "gyari celpontok: Normal=PC, Verseny=PC, Media=mindketto (%s)" % targets)

print("\n[3] Modositas kuldese es visszaolvasasa")
km[0][1][0] = g.Action(ACT_KEY, MOD_LCTRL | MOD_LSHIFT, 0x42)
km[1][4][2] = g.Action(ACT_CONSUMER, 0, 0x00E9, 1, 120)
km[2][0][1] = g.Action(ACT_VIEW_CYCLE)
targets[0] = TARGET_ALL
targets[2] = TARGET_PHONE
steps = []
link.write_config(km, targets, progress=lambda d, t: steps.append((d, t)))
check(steps[-1] == (48, 48), "45 kiosztas + 3 celpont elkuldve %s" % (steps[-1],))
back, back_t = link.read_config()
check(back[0][1][0].label() == "Ctrl+Shift+F9", "billentyu: " + back[0][1][0].label())
check(back[1][4][2].label().startswith("Hangerő +"), "media ismetlo: " + back[1][4][2].label())
check(back_t == [TARGET_ALL, TARGET_PC, TARGET_PHONE], "celpontok: %s" % back_t)

print("\n[4] Ervenytelen celpont elutasitasa")
for bad in (0, 4, 255):
    try:
        link.command("SETTARGET 0 %d" % bad); check(False, "maszk %d atment" % bad)
    except g.DeviceError:
        check(True, "maszk %d elutasitva" % bad)

print("\n[5] Eszkoz-hozzarendeles")
slots, conns = link.read_peers()
check(len(slots) == 2, "ket fiok")
check(len(conns) >= 1, "%d elo kapcsolat" % len(conns))
if conns:
    h = conns[0]["handle"]
    link.assign_slot(SLOT_PC, h)
    slots, conns = link.read_peers()
    check(slots[SLOT_PC]["valid"], "PC fiok hozzarendelve: " + slots[SLOT_PC]["addr"])
    check(conns[0]["slot"] == SLOT_PC, "a kapcsolat a PC fiokba tartozik")
    link.clear_slot(SLOT_PC)
    slots, _ = link.read_peers()
    check(not slots[SLOT_PC]["valid"], "PC fiok torolve")

print("\n[5b] Muveletenkenti cel-felulbiralas")
km2, t2 = link.read_config()
check([km2[2][b][2].target for b in range(3)] == [TARGET_PC] * 3,
      "gyarilag a Media/G1-G3 hosszu nyomas csak a PC-re megy")
check(km2[2][3][2].target == 0 and km2[2][4][2].target == 0,
      "a G4-G5 hosszu nyomas az uzemmod celpontjat orokli")
km2[2][1][2].target = TARGET_PHONE    # Media/G2/hosszu -> csak telefon
link.write_config(km2, t2)
km3, _ = link.read_config()
check(km3[2][0][2].target == TARGET_PC, "G1 hosszu: csak PC (%s)" % km3[2][0][2].label())
check(km3[2][1][2].target == TARGET_PHONE, "G2 hosszu: csak telefon (%s)" % km3[2][1][2].label())
check(km3[2][2][2].target == TARGET_PC, "G3 hosszu: csak PC (%s)" % km3[2][2][2].label())
check(km3[2][0][0].target == 0, "G1 rovid erintetlen")
check("→ PC" in km3[2][0][2].label(), "a felirat jelzi: %r" % km3[2][0][2].label())

print("\n[5c] Ismetles: kulon leutesek / nyomva tartva")
km4, t4 = link.read_config()
check(km4[2][3][2].repeat == REPEAT_ENABLED | REPEAT_RELEASE,
      "gyarilag kulon leutesek (repeat=%d)" % km4[2][3][2].repeat)
check("külön leütések" in km4[2][3][2].label(), "a felirat jelzi: %r" % km4[2][3][2].label())
km4[2][3][2].repeat = REPEAT_ENABLED          # nyomva tartva
km4[2][3][2].repeat_ms = 2000
link.write_config(km4, t4)
km5, _ = link.read_config()
check(km5[2][3][2].repeat == REPEAT_ENABLED, "nyomva tartva visszaolvasva")
check("nyomva tartva" in km5[2][3][2].label(), "a felirat jelzi: %r" % km5[2][3][2].label())
check(km4[2][1][2].repeat == REPEAT_ENABLED | REPEAT_RELEASE | REPEAT_HOLD_MOD,
      "Alt+Tab: modosito nyomva marad (repeat=%d)" % km4[2][1][2].repeat)
check("módosító nyomva" in km4[2][1][2].label(), "felirat: %r" % km4[2][1][2].label())
try:
    link.command("SET 0 3 2 1 0 81 8 100"); check(False, "rep=8 atment")
except g.DeviceError:
    check(True, "rep=8 elutasitva")

print("\n[5c] Kuldesi hossz oda-vissza a valodi firmware-rel")
km5 = link.read_config()[0]
check(all(km5[m][b][e].hold_ms == 0 for m in range(3) for b in range(5) for e in range(3)),
      "gyarilag mindenhol 0 a kuldesi hossz")
km5[0][0][0] = g.Action(ACT_KEY, 0, 0x50, REPEAT_ENABLED | REPEAT_RELEASE, 100,
                        TARGET_INHERIT, 1500)
km5[1][1][1] = g.Action(ACT_CONSUMER, 0, 0x00CF, 0, 60, TARGET_PHONE, 2000)
link.write_config(km5, targets)
back5 = link.read_config()[0]
check(back5[0][0][0].hold_ms == 1500 and back5[0][0][0].repeat == 3,
      "rovid nyomas: 1500 ms + kulon leutesek visszaolvasva (%s)" % back5[0][0][0].label())
check(back5[1][1][1].hold_ms == 2000 and back5[1][1][1].target == TARGET_PHONE,
      "dupla kattintas: 2000 ms a telefonra (%s)" % back5[1][1][1].label())
check(back5[0][0][2].hold_ms == 0, "a hosszu nyomasnal tovabbra is 0")
# a firmware visszautasitja a hosszu nyomasra adott kuldesi hosszt
try:
    link.command("SET 0 0 2 1 0 80 0 60 0 1000")
    check(False, "a firmware elfogadta a hosszu nyomasra adott hosszt")
except g.DeviceError as exc:
    check("ERR VALUE" in str(exc), "hosszu nyomasnal elutasitva: %s" % str(exc)[:60])
link.load_defaults()

print("\n[5d] A default_keymap.json EGYEZIK-e a firmware gyari kiosztasaval")
# A gyari kiosztas a firmware kodjaba van beegetve (loadDefaultKeymap), a
# konfiguralo program viszont a default_keymap.json-bol indul. A ketto
# elcsuszasa azt jelentene, hogy a program mast mutat, mint amit az eszkoz
# csinal - ezert itt tetelesen osszehasonlitjuk oket.
link.load_defaults()
fw_map, fw_targets = link.read_config()
with open(str(TOOLS / "default_keymap.json"), encoding="utf-8") as fh:
    file_map, file_targets = g._parse_keymap_file(json.load(fh))
check(fw_targets == file_targets,
      "uzemmod-celpontok: firmware=%s fajl=%s" % (fw_targets, file_targets))
elteres = []
for m in range(3):
    for b in range(5):
        for e in range(3):
            a, f = fw_map[m][b][e], file_map[m][b][e]
            if a.to_dict() != f.to_dict():
                d1, d2 = a.to_dict(), f.to_dict()
                diff = {k: (d1[k], d2[k]) for k in d1 if d1[k] != d2[k]}
                elteres.append("%s/G%d/%s: %s"
                               % (MODE_NAMES[m], b + 1, EVENT_NAMES[e], diff))
check(not elteres, "mind a 45 bejegyzes egyezik" if not elteres
      else "ELTERES (%d): %s" % (len(elteres), " | ".join(elteres[:3])))

print("\n[6] Mentes es gyari visszaallitas")
check(link.save_to_flash() == "OK SAVED", "SAVE")
check(link.load_defaults() == "OK DEFAULTS", "DEFAULTS")
after, after_t = link.read_config()
check(after[2][0][2].target == TARGET_PC, 'gyari visszaallitas: G1 hosszu ujra csak PC')
check(after[2][3][2].repeat == REPEAT_ENABLED | REPEAT_RELEASE, 'gyari visszaallitas: ujra kulon leutesek')
check(after[0][1][0].label() == "Enter", "kiosztas visszaallt: " + after[0][1][0].label())
check(after_t == [TARGET_PC, TARGET_PC, TARGET_ALL], "celpontok visszaalltak: %s" % after_t)
link.close()
print("\n" + ("HIBAK: %d" % len(fails) if fails else "INTEGRACIOS TESZT SIKERES"))
sys.exit(1 if fails else 0)
