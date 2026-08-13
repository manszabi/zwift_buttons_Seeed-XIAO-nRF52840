"""GUI smoke-teszt Xvfb alatt: a valodi Tkinter feluletet epiti fel."""
import sys
from pathlib import Path
# A vizsgalt program a szulo konyvtarban van (tools/), a repo gyokere eggyel feljebb.
TOOLS = Path(__file__).resolve().parent.parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))

import sys, os, time, json, types, tempfile
import tkinter as tk
import zwift_config_gui as g
from hid_tables import (ACT_KEY, ACT_CONSUMER, ACT_NONE, ACT_VIEW_CYCLE,
                        ACT_MODE_NEXT,
                        MOD_LCTRL, MOD_LSHIFT, MOD_LALT, CONSUMER_KEYS,
                        TARGET_PC, TARGET_PHONE, TARGET_ALL, SLOT_NAMES,
                        TARGET_INHERIT, EV_CLICK, EV_DOUBLE, EV_LONG, HOLD_MS_MAX,
                        HOLD_MS_MIN, REPEAT_ENABLED, REPEAT_RELEASE,
                        REPEAT_HOLD_MOD, MOD_LALT as _MOD_LALT)

fails = []
def check(cond, msg):
    print(("  OK   " if cond else "  HIBA ") + msg)
    if not cond: fails.append(msg)

root = tk.Tk(); root.geometry("980x640+0+0")
app = g.App(root)
root.update()

print("\n[1] Fooablak felepitese")
check(len(app.cells) == 45, "45 cella jott letre")
check(app.cells[(0, 0, 0)].cget("text") == "Balra nyíl  → PC",
      "Normal/G1/rovid felirata a default_keymap.json-bol: %r" % app.cells[(0,0,0)].cget("text"))
check(app.cells[(2, 3, 2)].cget("text").startswith("Hangerő -"),
      "Media/G4/hosszu felirata: %r" % app.cells[(2,3,2)].cget("text"))
check(app.notebook.index("end") == 3, "3 uzemmod-ful")

print("\n[2] ActionDialog - ismeretlen media kod megorzese (5. talalat)")
unknown = 0x00B9   # nincs a CONSUMER_KEYS listaban
check(unknown not in dict(CONSUMER_KEYS), "a 0x00B9 tenyleg ismeretlen kod")
d = g.ActionDialog(root, "teszt", g.Action(ACT_CONSUMER, 0, unknown), event=EV_CLICK)
root.update()
d._on_ok(); root.update()
check(d.result is not None and d.result.code == unknown,
      "OK utan is 0x%04X marad (kapott: 0x%04X)" % (unknown, d.result.code if d.result else 0))

print("\n[3] ActionDialog - ismert media kod es tipusvaltas")
d = g.ActionDialog(root, "teszt", g.Action(ACT_CONSUMER, 0, 0x00E9), event=EV_LONG)
root.update(); d._on_ok(); root.update()
check(d.result.type == ACT_CONSUMER and d.result.code == 0x00E9, "Hangero+ valtozatlan")

print("\n[4] Billentyu-felvetel (keysym -> HID + modositok)")
d = g.ActionDialog(root, "teszt", g.Action(), event=EV_CLICK)
d.wait_visibility(); root.update()
d.capture.focus_force(); root.update()
d.capture.event_generate("<KeyPress-Control_L>"); root.update()
d.capture.event_generate("<KeyPress-Shift_L>"); root.update()
d.capture.event_generate("<KeyPress-r>"); root.update()
act = d._build_action()
check(act.type == ACT_KEY and act.code == 0x15, "az 'r' HID kodja 0x15 (kapott 0x%02X)" % act.code)
check(act.modifier == (MOD_LCTRL | MOD_LSHIFT),
      "Ctrl+Shift felvéve (kapott 0x%02X)" % act.modifier)
check(act.label() == "Ctrl+Shift+R", "felirat: %r" % act.label())

print("\n[5] Felvetel utan a modosito-pipak is frissulnek")
check(d.mod_vars[MOD_LCTRL].get() == 1 and d.mod_vars[MOD_LALT].get() == 0,
      "Ctrl pipa be, Alt pipa ki")
d._clear_key(); root.update()
check(d._build_action().code == 0 and d._build_action().modifier == 0, "Törlés gomb urit")
d._on_cancel()

print("\n[5b] A listak/pipak akkor is hasznalhatok, ha a cella nem billentyu")
d = g.ActionDialog(root, "teszt", g.Action(ACT_CONSUMER, 0, 0x00CD), event=EV_CLICK)
d.wait_visibility(); root.update()
check(d.key_combo.instate(["!disabled"]), "billentyu-lista hasznalhato")
check(d.consumer_combo.instate(["!disabled"]), "media-lista hasznalhato")
check(d.capture.cget("state") == "normal", "felvevo mezo aktiv")
d.key_combo.current([c for c, _ in __import__("hid_tables").KEY_CHOICES].index(0x28))
d._on_key_combo(None); root.update()
check(d._build_action().type == ACT_KEY and d._build_action().code == 0x28,
      "listabol valasztas atallitja a tipust billentyure (Enter)")
d._on_cancel()

print("\n[6] Hosszu nyomas: ismetles csak billentyu/media tipusnal")
d = g.ActionDialog(root, "teszt", g.Action(ACT_KEY, 0, 0x51, REPEAT_ENABLED, 250),
                   event=EV_LONG)
root.update()
check(d._build_action().repeat == REPEAT_ENABLED and d._build_action().repeat_ms == 250,
      "ismetles betoltve")
d.type_var.set(ACT_VIEW_CYCLE); d._refresh_state(); root.update()
check(d._build_action().repeat == 0, "nezetvaltasnal az ismetles kikapcsol")
d._on_cancel()

print("\n[7] Rovid nyomasnal kuldesi hossz van, ismetles csak hossz mellett")
d = g.ActionDialog(root, "teszt", g.Action(ACT_KEY, 0, 0x28), event=EV_CLICK)
d.wait_visibility(); root.update()
check(d.hold_spin is not None, "van kuldesi hossz mezo")
check(d._build_action().hold_ms == 0, "alapbol 0 (rovid impulzus)")
d.repeat_var.set(1); d._refresh_state(); root.update()
check(d._build_action().repeat == 0, "hossz nelkul az ismetles nem kapcsolhato be")
d.hold_ms_var.set(1000); d._refresh_state(); root.update()
check(d._build_action().hold_ms == 1000, "1000 ms-os kuldes beallitva")
d.repeat_var.set(1); d._refresh_state(); root.update()
check(d._build_action().repeat & REPEAT_ENABLED, "hossz mellett mar ismetelheto")
d._on_cancel()
# hosszu nyomasnal nincs hossz-mezo, es a hossz mindig 0
d = g.ActionDialog(root, "teszt", g.Action(ACT_KEY, 0, 0x28), event=EV_LONG)
d.wait_visibility(); root.update()
check(d.hold_spin is None, "hosszu nyomasnal nincs kuldesi hossz mezo")
check(d._build_action().hold_ms == 0, "es a mentett ertek is 0")
d._on_cancel()

print("\n[8] JSON korbeeres a fooablakon at")
app.keymap[0][0][0] = g.Action(ACT_KEY, MOD_LALT, 0x3A)      # Alt+F1
app.keymap[1][2][1] = g.Action(ACT_CONSUMER, 0, unknown)     # ismeretlen kod
path = os.path.join(tempfile.mkdtemp(), "km.json")
g.filedialog.asksaveasfilename = lambda **k: path
g.filedialog.askopenfilename = lambda **k: path
app.on_save_file()
app.keymap = g.empty_keymap()
app.on_open_file(); root.update()
check(app.keymap[0][0][0].label() == "Alt+F1", "Alt+F1 tulelte a mentest")
check(app.keymap[1][2][1].code == unknown, "ismeretlen media kod tulelte a mentest")
check(app.cells[(0, 0, 0)].cget("text") == "Alt+F1  → PC", "a cella felirata is frissult")

print("\n[9] Hibas JSON elutasitasa")
bad = os.path.join(os.path.dirname(path), "bad.json")
open(bad, "w").write('{"format":"valami-mas","modes":[]}')
g.filedialog.askopenfilename = lambda **k: bad
errors = []
g.messagebox.showerror = lambda t, m, **k: errors.append(t)
before = app.keymap[0][0][0].label()
app.on_open_file()
check(errors and app.keymap[0][0][0].label() == before,
      "idegen fajl elutasitva, a kiosztas valtozatlan")

print("\n[10] Nincs kapcsolat -> az eszkoz-muveletek nem futnak")
warns = []
g.messagebox.showwarning = lambda t, m, **k: warns.append(t)
app.on_read_device(); app.on_write_device(); app.on_save_device()
check(len(warns) == 3, "mindharom muvelet figyelmeztet (kapott: %d)" % len(warns))

print("\n[11] Uzemmodonkenti cel-eszkoz a feluleten")
check(app.targets == [TARGET_PC, TARGET_PC, TARGET_ALL],
      "gyari celpontok betoltve a default_keymap.json-bol: %s" % app.targets)
check([app.keymap[2][b][EV_LONG].target for b in range(3)] == [TARGET_PC] * 3,
      "a Media/G1-G3 hosszu nyomas gyarilag csak a PC-re megy")
check("→ PC" in app.cells[(2, 2, EV_LONG)].cget("text"),
      "a G3 cella felirata jelzi: %r" % app.cells[(2, 2, EV_LONG)].cget("text"))
check(app.target_vars[0].get() == TARGET_PC, "Normal ful radiogombja: csak PC")
check(app.target_vars[2].get() == TARGET_ALL, "Media ful radiogombja: mindketto")
app.target_vars[1].set(TARGET_PHONE)
app.on_target_changed(1) if hasattr(app, "on_target_changed") else app._on_target_changed(1)
root.update()
check(app.targets[1] == TARGET_PHONE, "radiogomb atallitasa frissiti a modellt")
check(app.dirty, "a modositas piszkosnak jeloli a konfiguraciot")

print("\n[12] A celpont is bejarja a JSON korbeerest")
app.targets = [TARGET_ALL, TARGET_PHONE, TARGET_PC]
path2 = os.path.join(os.path.dirname(path), "km2.json")
g.filedialog.asksaveasfilename = lambda **k: path2
g.filedialog.askopenfilename = lambda **k: path2
app.on_save_file()
saved = json.load(open(path2, encoding="utf-8"))
check([m["target"] for m in saved["modes"]] == [TARGET_ALL, TARGET_PHONE, TARGET_PC],
      "a fajlba a celpontok is bekerultek")
check(saved["version"] == 5, "fajlverzio 5 (kapott: %s)" % saved["version"])
app.targets = [TARGET_PC, TARGET_PC, TARGET_PC]
app.on_open_file(); root.update()
check(app.targets == [TARGET_ALL, TARGET_PHONE, TARGET_PC], "celpontok visszatoltve")
check(app.target_vars[1].get() == TARGET_PHONE, "a radiogombok is frissultek")

print("\n[13] Regi (1-es verzioju), celpont nelkuli fajl betoltese")
old = json.load(open(path2, encoding="utf-8"))
old["version"] = 1
for m in old["modes"]:
    m.pop("target")
path3 = os.path.join(os.path.dirname(path), "km_v1.json")
json.dump(old, open(path3, "w", encoding="utf-8"))
g.filedialog.askopenfilename = lambda **k: path3
app.targets = [TARGET_PHONE, TARGET_PHONE, TARGET_PHONE]
app.on_open_file(); root.update()
check(app.targets == [TARGET_PC, TARGET_PC, TARGET_ALL],
      "celpont nelkuli fajlnal a gyari beallitas lep eletbe: %s" % app.targets)

print("\n[13b] 2-es verzioju fajl (nincs muveletenkenti felulbiralas)")
v2 = json.load(open(path2, encoding="utf-8"))
v2["version"] = 2
for m in v2["modes"]:
    for b in m["buttons"]:
        for k in ("click", "double", "long"):
            b[k].pop("target", None)
path_v2 = os.path.join(os.path.dirname(path), "km_v2.json")
json.dump(v2, open(path_v2, "w", encoding="utf-8"))
g.filedialog.askopenfilename = lambda **k: path_v2
app.on_open_file(); root.update()
check(all(app.keymap[m][b][e].target == 0
          for m in range(3) for b in range(5) for e in range(3)),
      "a hianyzo muveletenkenti celpont 0 (oroklodik) lesz")
check(app.targets == [TARGET_ALL, TARGET_PHONE, TARGET_PC],
      "az uzemmod-celpontok viszont megmaradtak: %s" % app.targets)

print("\n[14] PeersDialog felepitese (hamis eszkozzel)")
class FakeLink:
    def __init__(self): self.assigned = []; self.cleared = []
    def read_peers(self):
        slots = [{"valid": True, "addr": "A5:A4:A3:A2:A1:A0"}, {"valid": False, "addr": "-"}]
        conns = [{"handle": 0, "addr": "A5:A4:A3:A2:A1:A0", "slot": 0},
                 {"handle": 1, "addr": "B5:B4:B3:B2:B1:B0", "slot": -1}]
        return slots, conns
    def assign_slot(self, s, h): self.assigned.append((s, h))
    def clear_slot(self, s): self.cleared.append(s)
fake = FakeLink()
pd = g.PeersDialog(root, fake)
pd.wait_visibility(); root.update()
check(pd.slot_labels[0].cget("text") == "A5:A4:A3:A2:A1:A0", "PC fiok cime latszik")
check("nincs hozzárendelve" in pd.slot_labels[1].cget("text"), "ures fiok jelzese")
pd._assign(1, 1); root.update()
check(fake.assigned == [(1, 1)], "hozzarendeles tovabbitva az eszkoznek: %s" % fake.assigned)
pd._clear(0); root.update()
check(fake.cleared == [0], "fiok torlese tovabbitva")
pd._close()

print("\n[15] Eszkoz nelkul a hozzarendeles nem nyithato")
warns2 = []
g.messagebox.showwarning = lambda t, m, **k: warns2.append(t)
app.on_peers()
check(len(warns2) == 1, "figyelmeztet, ha nincs kapcsolat")

print("\n[16] PeersDialog: parositatlan eszkoz nem rendelheto hozza")
class FakeLink2(FakeLink):
    def read_peers(self):
        slots = [{"valid": False, "addr": "-"}, {"valid": False, "addr": "-"}]
        conns = [{"handle": 0, "addr": "C5:C4:C3:C2:C1:C0", "slot": -1, "bonded": False}]
        return slots, conns
pd2 = g.PeersDialog(root, FakeLink2())
pd2.wait_visibility(); root.update()
check("nincs párosítva" in pd2.status.cget("text") or "párosítva" in pd2.status.cget("text"),
      "figyelmeztet a parositas hianyara")
btns = [w for r in pd2.conn_frame.winfo_children() for w in r.winfo_children()
        if w.winfo_class() == "TButton"]
check(btns and all(b.instate(["disabled"]) for b in btns),
      "a hozzarendelo gombok tiltva (%d gomb)" % len(btns))
pd2._close()

print("\n[17] PeersDialog: figyelmeztet a hozzarendeletlen eszkozre")
class FakeLink3(FakeLink):
    def read_peers(self):
        slots = [{"valid": True, "addr": "A5:A4:A3:A2:A1:A0"}, {"valid": False, "addr": "-"}]
        conns = [{"handle": 0, "addr": "A5:A4:A3:A2:A1:A0", "slot": 0, "bonded": True},
                 {"handle": 1, "addr": "B5:B4:B3:B2:B1:B0", "slot": -1, "bonded": True}]
        return slots, conns
pd3 = g.PeersDialog(root, FakeLink3())
pd3.wait_visibility(); root.update()
check("sem kapnak meg" in pd3.status.cget("text"),
      "jelzi, hogy a hozzarendeletlen eszkoz semmit nem kap")
pd3._close()

print("\n[18] Regi firmware elutasitasa (protokoll-verzio)")
import time as _t
class OldFw:
    is_open = True
    def __init__(self, banner): self.banner = banner; self.sent = []
    def reset_input_buffer(self): pass
    def flush(self): pass
    def write(self, d): self.sent.append(d)
    def readline(self): _t.sleep(0.01); return self.banner
    def close(self): self.is_open = False
for banner, proto in ((b"OK ZWIFT_BUTTONS PROTO=1 MODES=3 BUTTONS=5 EVENTS=3\r\n", 1),
                      (b"OK ZWIFT_BUTTONS MODES=3\r\n", 1)):
    fake_ser = OldFw(banner)
    g.serial = types.SimpleNamespace(Serial=lambda *a, **k: fake_ser)
    link2 = g.DeviceLink()
    try:
        link2.open("FAKE"); check(False, "regi firmware atment (%r)" % banner)
    except g.DeviceError as exc:
        check("régi firmware" in str(exc), "elutasitva: %s" % str(exc)[:52])
    check(not link2.connected, "a port lezarva maradt")
    check(not any(b"SET " in x for x in fake_ser.sent), "egyetlen SET sem ment ki")
check(g.DeviceLink._parse_proto("OK ZWIFT_BUTTONS PROTO=2 SLOTS=2") == 2, "PROTO=2 kiolvasva")

print("\n[19] Cel-felulbiralas MINDEN cellanal valaszthato, alapbol oroklodik")
missing, not_inherit = [], []
for mode in range(3):
    for btn in range(5):
        for ev in range(3):
            d = g.ActionDialog(root, "t", g.Action(ACT_KEY, 0, 0x28),
                               event=ev)
            d.wait_visibility(); root.update()
            radios = [w for w in d.target_box.winfo_children()
                      if w.winfo_class() == "TRadiobutton"]
            if len(radios) != 1 + len(g.TARGET_CHOICES) or not d.target_box.winfo_ismapped():
                missing.append((mode, btn, ev))
            if d._build_action().target != TARGET_INHERIT:
                not_inherit.append((mode, btn, ev))
            d._on_cancel()
check(not missing, "mind a 45 cellanal ott a valaszto (hianyzik: %s)" % missing)
check(not not_inherit, "mind a 45 cellanal az oroklodes az alapertelmezes")

print("\n[19b] A valaszto letiltodik, ha a muveletnek nincs celpontja")
for atype, expect in ((ACT_KEY, True), (ACT_CONSUMER, True), (ACT_VIEW_CYCLE, True),
                      (ACT_MODE_NEXT, False), (ACT_NONE, False)):
    d = g.ActionDialog(root, "t", g.Action(ACT_KEY, 0, 0x28, 0, 60, TARGET_PHONE),
                       event=EV_LONG)
    d.wait_visibility(); root.update()
    d.type_var.set(atype); d._refresh_state(); root.update()
    radio = [w for w in d.target_box.winfo_children()
             if w.winfo_class() == "TRadiobutton"][0]
    enabled = "disabled" not in radio.state()
    check(enabled == expect, "tipus %d: valaszto %s" % (atype, "aktiv" if enabled else "tiltva"))
    if not expect:
        check(d._build_action().target == TARGET_INHERIT,
              "  tipus %d: a felulbiralas visszaall oroklodesre" % atype)
    d._on_cancel()

print("\n[20] A felulbiralas beallitasa es megorzese a szerkesztoben")
d = g.ActionDialog(root, "t", g.Action(ACT_KEY, MOD_LALT, 0x2B), event=EV_LONG)
d.wait_visibility(); root.update()
check(d._build_action().target == TARGET_INHERIT, "alapertelmezes: oroklodik")
d.action_target_var.set(TARGET_PC); d._update_preview(); root.update()
a = d._build_action()
check(a.target == TARGET_PC, "csak PC-re allitva")
check(a.label().endswith("→ PC"), "a felirat mutatja: %r" % a.label())
check("→ PC" in d.preview.cget("text"), "az elonezet is mutatja")
d._on_ok()
check(d.result.target == TARGET_PC, "OK utan megmarad")

print("\n[21] A meglevo felulbiralas nem vesz el egy erintetlen szerkesztesnel")
for ev_repeat in (False, True):
    d = g.ActionDialog(root, "t", g.Action(ACT_KEY, 0, 0x28, 0, 60, TARGET_PHONE),
                       event=(EV_LONG if ev_repeat else EV_CLICK))
    d.wait_visibility(); root.update()
    check(d.action_target_var.get() == TARGET_PHONE,
          "a megnyitott ertek a valasztoban is latszik (repeat=%s)" % ev_repeat)
    d._on_ok()
    check(d.result.target == TARGET_PHONE,
          "OK utan megmarad (%s)" % d.result.target)

print("\n[22] A felulbiralas bejarja a JSON korbeerest")
app.keymap[2][0][2] = g.Action(ACT_KEY, MOD_LALT, 0x15, 0, 60, TARGET_PC)
app.keymap[2][1][2] = g.Action(ACT_KEY, MOD_LALT, 0x2B, 0, 60, TARGET_PHONE)
path4 = os.path.join(os.path.dirname(path), "km_tgt.json")
g.filedialog.asksaveasfilename = lambda **k: path4
g.filedialog.askopenfilename = lambda **k: path4
app.on_save_file()
saved4 = json.load(open(path4, encoding="utf-8"))
check(saved4["modes"][2]["buttons"][0]["long"]["target"] == TARGET_PC,
      "a fajlba bekerult a felulbiralas")
app.keymap = g.empty_keymap()
app.on_open_file(); root.update()
check(app.keymap[2][0][2].target == TARGET_PC, "G1 hosszu visszatoltve")
check(app.keymap[2][1][2].target == TARGET_PHONE, "G2 hosszu visszatoltve")
check("→ PC" in app.cells[(2, 0, EV_LONG)].cget("text"),
      "a cella felirata: %r" % app.cells[(2, 0, EV_LONG)].cget("text"))

print("\n[23] Ervenytelen konfiguraciot egyaltalan ne kuldjon ki")
sent = []
class RecordingLink(g.DeviceLink):
    def __init__(self): super().__init__(); self.ser = object()
    @property
    def connected(self): return True
    def command(self, cmd, timeout=2.0):
        sent.append(cmd); return "OK"
rl = RecordingLink()
bad = g.empty_keymap()
bad[0][0][0] = g.Action(ACT_KEY, 0, 0x150)          # billentyukod > 0xFF
bad[1][2][1] = g.Action(9, 0, 4)                     # ismeretlen tipus
bad[2][4][2] = g.Action(ACT_KEY, 0, 0x28, 0, 60, 7)  # ervenytelen cel-maszk
try:
    rl.write_config(bad, [TARGET_PC, TARGET_PC, TARGET_ALL])
    check(False, "hibas konfiguracio atment")
except g.DeviceError as exc:
    check("semmit nem küldtem el" in str(exc), "elutasitva: %s" % str(exc).split(chr(10))[0])
check(sent == [], "egyetlen parancs sem ment ki (%d)" % len(sent))
problems = g.validate_config(bad, [TARGET_PC, TARGET_PC, 9])
check(len(problems) == 4, "mind a 4 hiba felsorolva (%d)" % len(problems))
ok_km, ok_t = g.empty_keymap(), g.default_targets()
check(g.validate_config(ok_km, ok_t) == [], "hibatlan konfiguracion nincs kifogas")
rl.write_config(ok_km, ok_t)
check(len(sent) == 48, "hibatlan konfiguracio kimegy (%d parancs)" % len(sent))

print("\n[24] Felulbiralas a nezetvaltas/uzemmodvaltas cellan is latszik")
check(g.Action(ACT_VIEW_CYCLE, 0, 0, 0, 60, TARGET_PC).label() == "Nézetváltás (1-9)  → PC",
      "nezetvaltas + felulbiralas: %r" % g.Action(ACT_VIEW_CYCLE, 0, 0, 0, 60, TARGET_PC).label())
check(g.Action(g.ACT_MODE_NEXT, 0, 0, 0, 60, TARGET_PHONE).label().endswith("→ telefon"),
      "uzemmodvaltas + felulbiralas")
check(g.Action().label() == "—", "ures cella valtozatlan")

print("\n[25] Hibas alapertelmezes-fajl: reszleges betoltes helyett semmi")
import io, contextlib
bad_json = {"format": "zwift-buttons-keymap", "version": 3,
            "modes": [{"name": "a", "target": 1,
                       "buttons": [{"click": {"type": 1, "code": 80}} for _ in range(5)]}]}
tmpdir = os.path.dirname(path)
bad_path = os.path.join(tmpdir, "bad_default.json")
json.dump(bad_json, open(bad_path, "w", encoding="utf-8"))
saved_defaults = g.DEFAULTS_PATH
try:
    g.DEFAULTS_PATH = g.Path(bad_path)
    app2 = g.App(tk.Toplevel(root))
    root.update()
    check(app2.keymap[0][0][0].label() == "Balra nyíl", "az ep resz betoltodott")
    g.DEFAULTS_PATH = g.Path(os.path.join(tmpdir, "nincs_ilyen.json"))
    app3 = g.App(tk.Toplevel(root)); root.update()
    check(all(app3.keymap[m][b][e].type == 0
              for m in range(3) for b in range(5) for e in range(3)),
          "hianyzo fajlnal ures, de konzisztens kiosztas")
finally:
    g.DEFAULTS_PATH = saved_defaults

print("\n[26] Ismetles: felengedes az ismetlesek kozott (kapcsolo)")
d = g.ActionDialog(root, "t", g.Action(ACT_KEY, 0, 0x51, REPEAT_ENABLED, 2000),
                   event=EV_LONG)
d.wait_visibility(); root.update()
check(d.release_var.get() == 0, "nyomva tartva modban a pipa ki van kapcsolva")
check("nyomva tartva" in d._build_action().label(),
      "felirat: %r" % d._build_action().label())
d.release_var.set(1); d._update_preview(); root.update()
a = d._build_action()
check(a.repeat == REPEAT_ENABLED | REPEAT_RELEASE, "bekapcsolva repeat=3 (kapott %d)" % a.repeat)
check("külön leütések" in a.label(), "felirat: %r" % a.label())
check("külön leütések" in d.preview.cget("text"), "az elonezet is mutatja")
d._on_ok()
check(d.result.repeat == REPEAT_ENABLED | REPEAT_RELEASE, "OK utan megmarad")

print("\n[27] Ismetles kikapcsolva -> a leutes-mod sem szamit")
d = g.ActionDialog(root, "t", g.Action(ACT_KEY, 0, 0x51, REPEAT_ENABLED | REPEAT_RELEASE, 500),
                   event=EV_LONG)
d.wait_visibility(); root.update()
check(d.repeat_var.get() == 1 and d.release_var.get() == 1, "mindket pipa be")
d.repeat_var.set(0); d._refresh_state(); root.update()
check(d._build_action().repeat == 0, "ismetles nelkul a maszk 0 (kapott %d)" % d._build_action().repeat)
check("ismétlő" not in d._build_action().label(), "a felirat sem emliti")
d._on_cancel()

print("\n[28] Rovid nyomasnal a kuldesi hossz hatarai ervenyesulnek")
d = g.ActionDialog(root, "t", g.Action(ACT_KEY, 0, 0x28), event=EV_CLICK)
d.wait_visibility(); root.update()
d.hold_ms_var.set(10); d._refresh_state(); root.update()
check(d._build_action().hold_ms == HOLD_MS_MIN, "10 ms -> %d ms" % d._build_action().hold_ms)
d.hold_ms_var.set(99999); d._refresh_state(); root.update()
check(d._build_action().hold_ms == HOLD_MS_MAX, "99999 ms -> %d ms" % d._build_action().hold_ms)
d.hold_ms_var.set(0); d._refresh_state(); root.update()
check(d._build_action().hold_ms == 0 and d._build_action().repeat == 0,
      "0 = rovid impulzus, ismetles nelkul")
d._on_cancel()
d._on_cancel()

print("\n[29] A gyari kiosztasban az ismetlodo muveletek kulon leutesek")
rep_cells = [(m, b, e) for m in range(3) for b in range(5) for e in range(3)
             if app.keymap[m][b][e].repeat & REPEAT_ENABLED]
check(len(rep_cells) == 4, "4 ismetlodo bejegyzes (%d)" % len(rep_cells))
check(all(app.keymap[m][b][e].repeat & REPEAT_RELEASE for m, b, e in rep_cells),
      "mindegyik kulon leutesekkel megy")

print("\n[30] Ervenytelen ismetles-maszk elutasitva")
bad2 = g.empty_keymap()
bad2[0][0][EV_LONG] = g.Action(ACT_KEY, 0, 0x28, 8)   # 8 > REPEAT_MASK
problems = g.validate_config(bad2, g.default_targets())
check(len(problems) == 1 and "ismétlés" in problems[0], "hiba: %s" % (problems[0] if problems else "-"))

print("\n[31] Modosito nyomva tartasa (Alt+Tab)")
d = g.ActionDialog(root, "t", g.Action(ACT_KEY, MOD_LALT, 0x2B,
                                       REPEAT_ENABLED | REPEAT_RELEASE, 500),
                   event=EV_LONG)
d.wait_visibility(); root.update()
check(d.holdmod_var.get() == 0, "alapbol nincs bepipalva")
check(d.holdmod_check.instate(["!disabled"]), "kulon leutesek mellett hasznalhato")
d.holdmod_var.set(1); d._update_preview(); root.update()
a = d._build_action()
check(a.repeat == REPEAT_ENABLED | REPEAT_RELEASE | REPEAT_HOLD_MOD,
      "repeat=7 (kapott %d)" % a.repeat)
check("módosító nyomva" in a.label(), "felirat: %r" % a.label())
d._on_ok(); check(d.result.repeat == 7, "OK utan megmarad")

print("\n[32] Nyomva tartva modban a modosito-kapcsolo tiltva")
d = g.ActionDialog(root, "t", g.Action(ACT_KEY, MOD_LALT, 0x2B, REPEAT_ENABLED, 500),
                   event=EV_LONG)
d.wait_visibility(); root.update()
check(d.holdmod_check.instate(["disabled"]), "tiltva, mert nincs kulon leutes")
d.release_var.set(1); d._refresh_state(); root.update()
check(d.holdmod_check.instate(["!disabled"]), "kulon leutesre valtva hasznalhatova valik")
d._on_cancel()

print("\n[33] A gyari Alt+Tab lepked tovabb ket ablaknal")
# frissen a gyari fajlbol (az app kiosztasat a korabbi tesztek modositottak)
fresh_km, _ = g._parse_keymap_file(json.loads(g.DEFAULTS_PATH.read_text(encoding="utf-8")))
alt_tab = fresh_km[2][1][EV_LONG]
check(alt_tab.modifier == MOD_LALT and alt_tab.code == 0x2B, "Alt+Tab a helyen")
check(alt_tab.repeat == REPEAT_ENABLED | REPEAT_RELEASE | REPEAT_HOLD_MOD,
      "ismetlodo, kulon leutesek, modosito nyomva (repeat=%d)" % alt_tab.repeat)
check(alt_tab.repeat_ms == 500, "500 ms-onkent lep (kapott %d)" % alt_tab.repeat_ms)
check("módosító nyomva" in alt_tab.label(), "felirat: %r" % alt_tab.label())
check(alt_tab.target == TARGET_PC, "es tovabbra is csak a PC-re megy")

print("\n[34] Eddig fedetlen fooablak-utak")
# refresh_ports hamis portlistaval
class FakePort:
    def __init__(s, dev, desc, man=None): s.device, s.description, s.manufacturer = dev, desc, man
g.list_ports = types.SimpleNamespace(comports=lambda: [
    FakePort("/dev/ttyS0", "soros port"),
    FakePort("/dev/ttyACM0", "XIAO nRF52840", "Seeed")])
app.refresh_ports(); root.update()
check(len(app.port_combo["values"]) == 2, "ket port listazva")
check(app._ports[app.port_combo.current()] == "/dev/ttyACM0",
      "az eszkozre utalo portot valasztja elore")
g.list_ports = types.SimpleNamespace(comports=lambda: [])
app.refresh_ports(); root.update()
check(app.port_combo.get() == "", "ures lista eseten nincs kivalasztva semmi")

# toggle_connection: kapcsolodas + bontas hamis eszkozzel
import time as _t2
class GoodFw:
    is_open = True
    def __init__(s): s.closed = False; s.lines = []
    def reset_input_buffer(s): pass
    def flush(s): pass
    def write(s, d):
        cmd = d.decode().strip().split()[0]
        s.lines = {"PING": [b"OK ZWIFT_BUTTONS PROTO=6 MODES=3 BUTTONS=5 EVENTS=3 SLOTS=2 CONNS=2\r\n"],
                   "DBG": [b"OK DBG 0\r\n"]}.get(cmd, [b"OK\r\n"])
    def readline(s): _t2.sleep(0.005); return s.lines.pop(0) if s.lines else b""
    def close(s): s.closed = True; s.is_open = False
fake_ser2 = GoodFw()
g.serial = types.SimpleNamespace(Serial=lambda *a, **k: fake_ser2)
g.list_ports = types.SimpleNamespace(comports=lambda: [FakePort("COM3", "XIAO")])
app.refresh_ports(); root.update()
g.messagebox.askyesno = lambda *a, **k: False      # ne olvasson be
app.toggle_connection(); root.update()
check(app.link.connected, "csatlakozott")
check(app.connect_btn.cget("text") == "Bontás", "a gomb felirata valtozott")
app.toggle_connection(); root.update()
check(not app.link.connected and fake_ser2.closed, "bontas lezarta a portot")
check(app.connect_btn.cget("text") == "Csatlakozás", "a gomb felirata visszaallt")

print("\n[35] edit_cell es on_close")
class StubDialog:
    def __init__(s, master, title, action, event):
        s.result = g.Action(ACT_KEY, 0, 0x29)      # Esc
real_dialog = g.ActionDialog
g.ActionDialog = StubDialog
real_wait = app.master.wait_window
app.master.wait_window = lambda w: None
app.dirty = False
app.edit_cell(0, 0, 0); root.update()
check(app.keymap[0][0][0].code == 0x29, "edit_cell atirta a cellat")
check(app.cells[(0, 0, 0)].cget("text")
      == "Esc  → " + g.target_short(app.targets[0]), "a cella felirata frissult")
check(app.dirty, "a modositas piszkosnak jeloli")
g.ActionDialog = real_dialog
app.master.wait_window = real_wait

closed = []
app.master.destroy = lambda: closed.append(True)
g.messagebox.askokcancel = lambda *a, **k: False
app.dirty = True
app.on_close()
check(not closed, "piszkos allapotban a Megse megtartja az ablakot")
g.messagebox.askokcancel = lambda *a, **k: True
app.on_close()
check(closed, "megerositve bezar")

print("\n[36] Ismetles-kapcsolok csak ott aktivak, ahol hatnak")
# media billentyunel a modosito-kapcsolo tiltott (a firmware figyelmen kivul hagyna)
d = g.ActionDialog(root, "t", g.Action(ACT_CONSUMER, 0, 0x00E9,
                                       REPEAT_ENABLED | REPEAT_RELEASE, 70),
                   event=EV_LONG)
d.wait_visibility(); root.update()
check(d.holdmod_check.instate(["disabled"]), "media billentyunel tiltva")
d._on_cancel()
# modosito nelkuli billentyunel is tiltott
d = g.ActionDialog(root, "t", g.Action(ACT_KEY, 0, 0x51,
                                       REPEAT_ENABLED | REPEAT_RELEASE, 30),
                   event=EV_LONG)
d.wait_visibility(); root.update()
check(d.holdmod_check.instate(["disabled"]), "modosito nelkul tiltva")
d.mod_vars[MOD_LALT].set(1); d._refresh_state(); root.update()
check(d.holdmod_check.instate(["!disabled"]), "Alt bepipalasa utan hasznalhato")
d._on_cancel()
# ismetles nelkul a "kulon leutesek" is tiltott
d = g.ActionDialog(root, "t", g.Action(ACT_KEY, 0, 0x51), event=EV_LONG)
d.wait_visibility(); root.update()
check(d.release_check.instate(["disabled"]), "ismetles nelkul a leutes-mod tiltva")
d.repeat_var.set(1); d._refresh_state(); root.update()
check(d.release_check.instate(["!disabled"]), "ismetlessel hasznalhato")
d._on_cancel()

print("\n[37] Ertelmetlen ismetles-kombinaciok elutasitva")
for mask, why in ((2, "RELEASE ismetles nelkul"), (4, "csak HOLD_MOD"),
                  (5, "HOLD_MOD RELEASE nelkul"), (6, "RELEASE+HOLD ismetles nelkul")):
    bad3 = g.empty_keymap()
    bad3[0][0][EV_LONG] = g.Action(ACT_KEY, MOD_LALT, 0x2B, mask)
    probs = g.validate_config(bad3, g.default_targets())
    check(len(probs) == 1, "rep=%d elutasitva (%s)" % (mask, why))
for mask in (0, 1, 3, 7):
    ok3 = g.empty_keymap()
    ok3[0][0][EV_LONG] = g.Action(ACT_KEY, MOD_LALT, 0x2B, mask)
    check(g.validate_config(ok3, g.default_targets()) == [], "rep=%d elfogadva" % mask)

print("\n[38] Hangasszisztens-kodok a media listaban")
codes = dict(CONSUMER_KEYS)
check(codes.get(0x00CF) is not None, "0x00CF (Voice Command) benne van: %r" % codes.get(0x00CF))
check(codes.get(0x01CB) is not None, "0x01CB (Assistant) benne van: %r" % codes.get(0x01CB))
check(codes.get(0x0040) is not None, "0x0040 (Menu/Home) benne van: %r" % codes.get(0x0040))
check(len(codes) == len(CONSUMER_KEYS), "nincs duplikalt kod a listaban")
check(all(0 <= c <= g.CONSUMER_MAX_USAGE for c in codes),
      "minden listaelem a HID tartomanyon belul van")
# a szerkesztobol tenylegesen kivalaszthato es tulel egy korbeerest
d = g.ActionDialog(root, "t", g.Action(ACT_CONSUMER, 0, 0x00CF), event=EV_LONG)
d.wait_visibility(); root.update()
check(d.consumer_var.get() == 0x00CF, "a szerkeszto a 0x00CF-et valasztja ki")
d._on_ok()
check(d.result.code == 0x00CF and d.result.type == ACT_CONSUMER, "OK utan megmarad")
check("asszisztens" in d.result.label().lower(), "felirat: %r" % d.result.label())

print("\n[39] HID tartomanyon kivuli media kod elutasitva")
bad4 = g.empty_keymap()
bad4[0][0][0] = g.Action(ACT_CONSUMER, 0, 0x0400)
probs = g.validate_config(bad4, g.default_targets())
check(len(probs) == 1 and "tartomány" in probs[0], "0x0400 elutasitva: %s" % probs)
for good in (0x0000, 0x00CF, 0x01CB, g.CONSUMER_MAX_USAGE):
    ok4 = g.empty_keymap()
    ok4[0][0][0] = g.Action(ACT_CONSUMER, 0, good)
    check(g.validate_config(ok4, g.default_targets()) == [], "0x%04X elfogadva" % good)

print("\n[40] Hibas gyokeru JSON nem szall el kezeletlen kivetellel")
import json as _json
bad_path = os.path.join(os.path.dirname(path), "nem_dict.json")
open(bad_path, "w").write("[1, 2, 3]")
g.filedialog.askopenfilename = lambda **k: bad_path
errors = []
real_err = g.messagebox.showerror
g.messagebox.showerror = lambda t, m, **k: errors.append((t, m))
before40 = app.keymap[0][0][0].code
try:
    app.on_open_file(); root.update()
    check(len(errors) == 1, "hibauzenetet kaptunk: %s" % (errors[0][0] if errors else None))
    check(app.keymap[0][0][0].code == before40, "a kiosztas valtozatlan maradt")
except AttributeError as exc:
    check(False, "kezeletlen kivetel: %s" % exc)
finally:
    g.messagebox.showerror = real_err

print("\n[41] Felbeszakadt kuldes: a hibauzenet megmondja, hol tartott")
class HalfSer:
    def __init__(s): s.is_open=True; s.n=0; s.buf=b""
    def reset_input_buffer(s): pass
    def write(s, d):
        s.n += 1
        s.buf = b"" if s.n > 5 else b"OK\n"    # az 6. parancsra elnemul
    def flush(s): pass
    def readline(s):
        d, s.buf = s.buf, b""
        return d
    def close(s): s.is_open=False
link41 = g.DeviceLink()
link41.ser = HalfSer()
try:
    link41.write_config(g.empty_keymap(), g.default_targets())
    check(False, "hibat kellett volna dobnia")
except g.DeviceError as exc:
    msg = str(exc)
    check("5/48" in msg, "kiirja a mar elkuldott mennyiseget: %r" % msg.splitlines()[-1])
    check("f\u00e9lig alkalmazott" in msg, "figyelmeztet a felig alkalmazott allapotra")

print("\n[42] Ujonnan bekapcsolt ismetles alapbol KULON LEUTESEK")
d = g.ActionDialog(root, "t", g.Action(ACT_KEY, 0, 0x51), event=EV_LONG)  # repeat=0
d.wait_visibility(); root.update()
d.repeat_var.set(1); d._refresh_state(); root.update()
a42 = d._build_action()
check(a42.repeat == (REPEAT_ENABLED | REPEAT_RELEASE),
      "rep=%d (3 = kulon leutesek, 1 = a beragado mod)" % a42.repeat)
d._on_cancel()
# a mar nyomva-tartva modra allitott cella viszont maradjon az
d = g.ActionDialog(root, "t", g.Action(ACT_KEY, 0, 0x51, REPEAT_ENABLED, 60), event=EV_LONG)
d.wait_visibility(); root.update()
check(d._build_action().repeat == REPEAT_ENABLED, "a meglevo 'nyomva tartva' nem valtozik")
d._on_cancel()

print("\n[43] Regi (4 elotti) fajl betoltesekor a repeat=1 atalakul")
old_file = os.path.join(os.path.dirname(path), "v3.json")
v3 = {"format": g.FILE_FORMAT, "version": 3, "modes": [
    {"name": "m%d" % m, "target": 1, "buttons": [
        {"click": {"type": ACT_NONE}, "double": {"type": ACT_NONE},
         "long": {"type": ACT_KEY, "modifier": 0, "code": 0x51,
                  "repeat": 1, "repeat_ms": 30}}
        for _ in range(5)]} for m in range(3)]}
json.dump(v3, open(old_file, "w", encoding="utf-8"))
km43, _ = g._parse_keymap_file(json.load(open(old_file, encoding="utf-8")))
check(km43[0][0][EV_LONG].repeat == (REPEAT_ENABLED | REPEAT_RELEASE),
      "a regi repeat=1 -> %d" % km43[0][0][EV_LONG].repeat)
km43b, _ = g._parse_keymap_file(json.load(open(path2, encoding="utf-8")))
check(json.load(open(path2, encoding="utf-8"))["version"] == 5, "a mai fajl 5-os verzioju")

print("\n[44] Kulon leutesek: az ismetlesi ido nem mehet 30 ms ala")
d = g.ActionDialog(root, "t", g.Action(ACT_KEY, 0, 0x51,
                                       REPEAT_ENABLED | REPEAT_RELEASE, 60),
                   event=EV_LONG)
d.wait_visibility(); root.update()
d.repeat_ms_var.set(10); d._refresh_state(); root.update()
a44 = d._build_action()
check(a44.repeat_ms == 30, "10 ms -> %d ms (a firmware is ennyire huzna fel)" % a44.repeat_ms)
check("30 ms" in a44.label(), "a felirat is a valos erteket mutatja: %r" % a44.label())
d._on_cancel()
# nyomva tartva modban marad a 10 ms
d = g.ActionDialog(root, "t", g.Action(ACT_KEY, 0, 0x51, REPEAT_ENABLED, 10), event=EV_LONG)
d.wait_visibility(); root.update()
check(d._build_action().repeat_ms == 10, "nyomva tartva modban nincs also hatar")
d._on_cancel()

print("\n[45] Fajlba mentes / megnyitas utan nincs 'elveszo munka' figyelmeztetes")
g.filedialog.asksaveasfilename = lambda **k: path2
g.filedialog.askopenfilename = lambda **k: path2
app.dirty = True
app.on_save_file(); root.update()
check(not app.dirty, "mentes utan nincs mentetlen valtozas")
app.dirty = True
app.on_open_file(); root.update()
check(not app.dirty, "megnyitas utan sincs")
app.keymap[0][0][0] = g.Action(ACT_KEY, 0, 0x28)
app.cells[(0, 0, 0)].configure(text="x")
app.dirty = True
check(app.dirty, "szerkesztes utan viszont igen")

print("\n[46] A kuldesi hossz ellenorzese kuldes elott")
km46 = g.empty_keymap()
km46[0][0][EV_LONG] = g.Action(ACT_KEY, 0, 0x28, 0, 60, TARGET_INHERIT, 1000)
probs = g.validate_config(km46, g.default_targets())
check(len(probs) == 1 and "hosszú nyomásnál" in probs[0],
      "hosszu nyomasnal nincs kuldesi hossz: %s" % (probs[0] if probs else "-"))
km46 = g.empty_keymap()
km46[0][0][EV_CLICK] = g.Action(ACT_KEY, 0, 0x28, 0, 60, TARGET_INHERIT, 9000)
probs = g.validate_config(km46, g.default_targets())
check(len(probs) == 1 and "küldési hossz" in probs[0], "9000 ms elutasitva: %s" % probs)
km46 = g.empty_keymap()
km46[0][0][EV_DOUBLE] = g.Action(ACT_KEY, 0, 0x28, REPEAT_ENABLED, 60, TARGET_INHERIT, 0)
probs = g.validate_config(km46, g.default_targets())
check(len(probs) == 1 and "nincs küldési hossz" in probs[0],
      "ismetles hossz nelkul elutasitva: %s" % probs)
for hold in (0, HOLD_MS_MIN, 1000, HOLD_MS_MAX):
    km46 = g.empty_keymap()
    km46[0][0][EV_CLICK] = g.Action(ACT_KEY, 0, 0x28, REPEAT_ENABLED | REPEAT_RELEASE if hold else 0,
                                    100, TARGET_INHERIT, hold)
    check(g.validate_config(km46, g.default_targets()) == [], "%d ms elfogadva" % hold)

print("\n[47] A kuldesi hossz bejarja a JSON korbeerest es latszik a feliraton")
app.keymap[0][0][EV_CLICK] = g.Action(ACT_KEY, 0, 0x28, 0, 60, TARGET_INHERIT, 1500)
app.keymap[0][1][EV_DOUBLE] = g.Action(ACT_CONSUMER, 0, 0x00CF,
                                       REPEAT_ENABLED | REPEAT_RELEASE, 120,
                                       TARGET_PHONE, 2000)
path47 = os.path.join(os.path.dirname(path), "km_hold.json")
g.filedialog.asksaveasfilename = lambda **k: path47
g.filedialog.askopenfilename = lambda **k: path47
app.on_save_file()
app.keymap = g.empty_keymap()
app.on_open_file(); root.update()
check(app.keymap[0][0][EV_CLICK].hold_ms == 1500, "1500 ms tulelte a mentest")
check(app.keymap[0][1][EV_DOUBLE].hold_ms == 2000
      and app.keymap[0][1][EV_DOUBLE].repeat == 3, "a dupla kattintas beallitasai is")
lab = app.keymap[0][0][EV_CLICK].label()
check("1500 ms-ig" in lab and "végig nyomva" in lab, "felirat: %r" % lab)
lab = app.keymap[0][1][EV_DOUBLE].label()
check("2000 ms-ig" in lab and "külön leütések" in lab, "felirat: %r" % lab)
# regi (5 elotti) fajlban nincs hold_ms -> 0 lesz
old47 = json.load(open(path47, encoding="utf-8"))
for mode in old47["modes"]:
    for btn in mode["buttons"]:
        for ev in btn:
            btn[ev].pop("hold_ms", None)
old47["version"] = 4
km47, _ = g._parse_keymap_file(old47)
check(all(km47[m][b][e].hold_ms == 0 for m in range(3) for b in range(5) for e in range(3)),
      "a regi fajlbol 0 kuldesi hossz jon")

print("\n[48] A fokepernyo cella-feliratai")
app.keymap = g.empty_keymap()
app.targets = [TARGET_PC, TARGET_PC, TARGET_ALL]
app.keymap[0][0][EV_CLICK] = g.Action(ACT_KEY, MOD_LALT, 0x2B)                    # Alt+Tab
app.keymap[0][1][EV_LONG] = g.Action(ACT_KEY, MOD_LALT, 0x2B,
                                     REPEAT_ENABLED | REPEAT_RELEASE | REPEAT_HOLD_MOD, 500)
app.keymap[0][2][EV_CLICK] = g.Action(ACT_CONSUMER, 0, 0x00CF, 0, 60, TARGET_PHONE, 2000)
app.keymap[0][3][EV_CLICK] = g.Action(ACT_MODE_NEXT)
app.keymap[2][0][EV_CLICK] = g.Action(ACT_KEY, 0, 0x28)                           # oroklodo
app._refresh_all_cells(); root.update()
t = app.cells[(0, 0, EV_CLICK)].cget("text")
check(t == "Alt+Tab  → PC", "egyszeru parancs: %r" % t)
t = app.cells[(0, 1, EV_LONG)].cget("text")
check(t == "Alt+Tab  (ismétlő)  → PC", "ismetlodo, reszletek nelkul: %r" % t)
check("külön leütések" not in t and "módosító" not in t and "500" not in t,
      "a leutes-mod, a modosito es az ismetlesi ido nem latszik")
t = app.cells[(0, 2, EV_CLICK)].cget("text")
check(t == "Hangasszisztens (Siri/Google)  (2000 ms)  → telefon",
      "kuldesi hossz + felulbiralt cel: %r" % t)
t = app.cells[(0, 3, EV_CLICK)].cget("text")
check(t == "Üzemmód váltás", "uzemmodvaltasnal nincs nyil (nem megy eszkozre): %r" % t)
t = app.cells[(2, 0, EV_CLICK)].cget("text")
check(t == "Enter  → mindkettő", "oroklodo cellanal is latszik a cel: %r" % t)
# az uzemmod celjanak atallitasa frissiti az oroklodo cellakat
app.target_vars[2].set(TARGET_PHONE); app._on_target_changed(2); root.update()
t = app.cells[(2, 0, EV_CLICK)].cget("text")
check(t == "Enter  → telefon", "uzemmod-cel valtasa utan: %r" % t)
check(app.cells[(0, 2, EV_CLICK)].cget("text").endswith("→ telefon"),
      "a felulbiralt cella valtozatlan")

print("\n[49] Desktop Assistant a media listaban")
check(dict(CONSUMER_KEYS)[0x01CB] == "Desktop Assistant",
      "0x01CB neve: %r" % dict(CONSUMER_KEYS)[0x01CB])

print("\n[50] A review altal jelzett Python-hibak")
# a kuldesi hossz csak billentyu/media muveletnel ervenyes (a firmware is ezt mondja)
for atype in (ACT_MODE_NEXT, ACT_VIEW_CYCLE, ACT_NONE):
    km50 = g.empty_keymap()
    km50[0][0][EV_CLICK] = g.Action(atype, 0, 0, 0, 60, TARGET_INHERIT, 500)
    probs = g.validate_config(km50, g.default_targets())
    check(any("billentyű vagy média" in p for p in probs),
          "tipus %d + hossz elutasitva" % atype)
# az ismetles-kapcsolo tiltva, ha nincs kuldesi hossz
d = g.ActionDialog(root, "t", g.Action(ACT_KEY, 0, 0x28), event=EV_CLICK)
d.wait_visibility(); root.update()
check(d.repeat_check.instate(["disabled"]), "hossz nelkul az ismetles-kapcsolo tiltva")
d.hold_ms_var.set(1000); d._refresh_state(); root.update()
check(d.repeat_check.instate(["!disabled"]), "hossz mellett hasznalhato")
d._on_cancel()
# a regi fajl atalakitasa nem nema
notes = []
old50 = {"format": g.FILE_FORMAT, "version": 3, "modes": [
    {"target": 1, "buttons": [
        {"click": {"type": ACT_NONE}, "double": {"type": ACT_NONE},
         "long": {"type": ACT_KEY, "code": 0x51, "repeat": 1, "repeat_ms": 30}}
        for _ in range(5)]} for _ in range(3)]}
km50, _ = g._parse_keymap_file(old50, notes)
check(km50[0][0][EV_LONG].repeat == 3, "a regi repeat=1 atalakult")
check(len(notes) == 1 and "15" in notes[0], "figyelmeztetes keletkezett: %r" % (notes[0][:60] if notes else None))
check(g._parse_keymap_file(json.load(open(path2, encoding="utf-8")), []) and True, "mai fajlnal nincs uzenet")
notes2 = []
g._parse_keymap_file(json.load(open(path2, encoding="utf-8")), notes2)
check(notes2 == [], "mai (5-os) fajlnal nincs figyelmeztetes")

root.destroy()
print("\n" + ("HIBAK: %d" % len(fails) if fails else "MINDEN GUI TESZT SIKERES"))
sys.exit(1 if fails else 0)
