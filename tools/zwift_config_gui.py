#!/usr/bin/env python3
"""Zwift Buttons – gomb-kiosztás konfiguráló (Seeed XIAO nRF52840).

Ablakos program, amivel a 3 üzemmód × 5 gomb × 3 esemény (rövid / dupla /
hosszú nyomás) billentyű-kombinációi állíthatók be, és USB soros porton
elküldhetők az eszköznek, illetve elmenthetők annak flash memóriájába.

Futtatás:
    pip install pyserial
    python zwift_config_gui.py
"""

import json
import sys
import time
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

# A modul mellől importálunk (hid_tables.py), akkor is, ha máshonnan indítják.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from hid_tables import (  # noqa: E402
    ACT_CONSUMER, ACT_KEY, ACT_MODE_NEXT, ACT_NONE, ACT_TYPE_COUNT, ACT_VIEW_CYCLE,
    CONSUMER_KEYS, CONSUMER_MAX_USAGE, DEFAULT_TARGETS, EVENT_KEYS, EVENT_NAMES, EV_LONG,
    KEY_CHOICES, KEY_NAMES, MODE_NAMES, MODIFIERS, MODIFIER_KEYSYMS,
    HOLD_MS_MAX, HOLD_MS_MIN, REPEAT_ENABLED, REPEAT_HOLD_MOD, REPEAT_MASK,
    REPEAT_MIN_MS_RELEASE, REPEAT_RELEASE,
    SLOT_NAMES, TARGET_ALL, TARGET_CHOICES, TARGET_INHERIT, consumer_label,
    key_label, keysym_to_hid, target_label, target_short,
)

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover
    serial = None
    list_ports = None

DEFAULTS_PATH = Path(__file__).resolve().parent / "default_keymap.json"

BAUDRATE = 115200
NUM_MODES = 3
NUM_BUTTONS = 5
NUM_EVENTS = 3
FILE_FORMAT = "zwift-buttons-keymap"
FILE_VERSION = 5
NUM_SLOTS = 2
# Ez a program legalább ilyen protokoll-verziójú firmware-t igényel.
MIN_PROTO = 6


# ---------------------------------------------------------------------------
# Adatmodell
# ---------------------------------------------------------------------------

class Action:
    """Egy (üzemmód, gomb, esemény) hármashoz tartozó művelet."""

    __slots__ = ("type", "modifier", "code", "repeat", "repeat_ms", "target",
                 "hold_ms")

    def __init__(self, type=ACT_NONE, modifier=0, code=0, repeat=0, repeat_ms=60,
                 target=TARGET_INHERIT, hold_ms=0):
        self.type = type
        self.modifier = modifier
        self.code = code
        self.repeat = repeat
        self.repeat_ms = repeat_ms
        # 0 = az üzemmód célpontja érvényes; egyébként saját cél-maszk.
        self.target = target
        # Csak rövid / dupla nyomásnál: meddig menjen ki a parancs (ms).
        # 0 = a korábbi viselkedés, egyetlen rövid impulzus.
        self.hold_ms = hold_ms

    def label(self) -> str:
        if self.type == ACT_KEY:
            text = key_label(self.modifier, self.code)
        elif self.type == ACT_CONSUMER:
            text = consumer_label(self.code)
        elif self.type == ACT_MODE_NEXT:
            text = "Üzemmód váltás"
        elif self.type == ACT_VIEW_CYCLE:
            text = "Nézetváltás (1-9)"
        else:
            text = "—"
        # Az ismétlés csak billentyű/média műveletnél értelmes, a cél-felül-
        # bírálás viszont bármelyiknél – ezért az utóbbi nem térhet ki korán.
        if self.type in (ACT_KEY, ACT_CONSUMER):
            parts = []
            if self.hold_ms:
                parts.append(f"{self.hold_ms} ms-ig")
            if self.repeat & REPEAT_ENABLED:
                if self.repeat & REPEAT_RELEASE:
                    mode = ("külön leütések, módosító nyomva"
                            if self.repeat & REPEAT_HOLD_MOD else "külön leütések")
                else:
                    mode = "nyomva tartva"
                parts.append(f"ismétlő {self.repeat_ms} ms")
                parts.append(mode)
            elif self.hold_ms:
                parts.append("végig nyomva")
            if parts:
                text += "  (" + ", ".join(parts) + ")"
        if self.target:
            text += f"  → {target_short(self.target)}"
        return text

    def cell_label(self, mode_target) -> str:
        """A főképernyő celláinak rövid felirata.

        Csak a lényeg: a kiküldött parancs, zárójelben hogy ismétlődik-e (és ha
        van beállított küldési hossz, az is), végül nyíl után a parancs
        célpontja. A célpont akkor is látszik, ha az üzemmódtól öröklődik –
        ezért kell hozzá az üzemmód célja.
        """
        if self.type == ACT_KEY:
            text = key_label(self.modifier, self.code)
        elif self.type == ACT_CONSUMER:
            text = consumer_label(self.code)
        elif self.type == ACT_MODE_NEXT:
            return "Üzemmód váltás"
        elif self.type == ACT_VIEW_CYCLE:
            text = "Nézetváltás (1-9)"
        else:
            return "—"

        marks = []
        if self.hold_ms:
            marks.append(f"{self.hold_ms} ms")
        if self.repeat & REPEAT_ENABLED:
            marks.append("ismétlő")
        if marks:
            text += "  (" + ", ".join(marks) + ")"
        return f"{text}  → {target_short(self.target or mode_target)}"

    def to_dict(self):
        return {
            "type": self.type,
            "modifier": self.modifier,
            "code": self.code,
            "repeat": self.repeat,
            "repeat_ms": self.repeat_ms,
            "target": self.target,
            "hold_ms": self.hold_ms,
        }

    @staticmethod
    def from_dict(d):
        return Action(
            int(d.get("type", ACT_NONE)),
            int(d.get("modifier", 0)),
            int(d.get("code", 0)),
            int(d.get("repeat", 0)),
            int(d.get("repeat_ms", 60)),
            int(d.get("target", TARGET_INHERIT)),
            int(d.get("hold_ms", 0)),
        )


def _parse_keymap_file(data, notes=None):
    """JSON tartalom -> (keymap, célpontok). Hiba esetén kivételt dob.

    A régi (1-es, 2-es verziójú) fájlokból hiányzó mezők a gyári értéket
    kapják, így azok is betölthetők maradnak.

    A 4-es fájlverzió előtt a `repeat` mező csak 0/1 lehetett, és az 1 a
    billentyűt végig lenyomva tartotta – ez a hostnál beragadt billentyűnek
    látszik. A firmware a saját mentését betöltéskor átalakítja
    (migrateKeymapV2toV3); ugyanezt kell tennünk a fájlokkal is, különben egy
    régi mentés visszatöltése csendben visszahozná a hibás működést.

    A 3-as fájlverzió sajnos kétértelmű: a „külön leütések" mód még ezzel a
    verziószámmal jelent meg. Ezért az átalakítás nem lehet néma – ha történt,
    a `notes` listába kerül egy üzenet, amit a hívó kiír a felhasználónak.
    """
    keymap = empty_keymap()
    targets = default_targets()
    migrated = 0
    try:
        file_version = int(data.get("version", 1))
    except (TypeError, ValueError):
        file_version = 1
    for m, mode in enumerate(data["modes"][:NUM_MODES]):
        mask = int(mode.get("target", targets[m]))
        if 1 <= mask <= TARGET_ALL:
            targets[m] = mask
        for b, button in enumerate(mode["buttons"][:NUM_BUTTONS]):
            for e in range(NUM_EVENTS):
                entry = button.get(EVENT_KEYS[e])
                if entry:
                    action = Action.from_dict(entry)
                    if file_version < 4 and action.repeat == REPEAT_ENABLED:
                        action.repeat |= REPEAT_RELEASE
                        migrated += 1
                    keymap[m][b][e] = action
    if migrated and notes is not None:
        notes.append(
            f"A fájl régi formátumú ({file_version}); {migrated} ismétlődő "
            "bejegyzés „külön leütésekre” alakult. Ha valamelyiknél szándékosan "
            "„nyomva tartva” volt, állítsd vissza.")
    return keymap, targets


def validate_config(keymap, targets):
    """A konfiguráció ellenőrzése küldés előtt.

    Az eszköz bejegyzésenként utasítja vissza a hibás értéket, és a küldés a
    hiba helyén állna meg – félig alkalmazott kiosztást hagyva a készüléken.
    Ezért mindent előre ellenőrzünk, és csak hibátlan konfigurációt küldünk ki.
    """
    problems = []
    for m in range(NUM_MODES):
        if not 1 <= targets[m] <= TARGET_ALL:
            problems.append(f"{MODE_NAMES[m]}: érvénytelen cél ({targets[m]})")
        for b in range(NUM_BUTTONS):
            for e in range(NUM_EVENTS):
                a = keymap[m][b][e]
                where = f"{MODE_NAMES[m]} / Gomb {b + 1} / {EVENT_NAMES[e]}"
                if not 0 <= a.type < ACT_TYPE_COUNT:
                    problems.append(f"{where}: ismeretlen művelet-típus ({a.type})")
                if not 0 <= a.modifier <= 0xFF:
                    problems.append(f"{where}: érvénytelen módosító ({a.modifier})")
                if a.type == ACT_KEY and not 0 <= a.code <= 0xFF:
                    problems.append(f"{where}: a billentyűkód nem fér el egy bájton ({a.code})")
                # A HID leíró a 0x0000..0x03FF tartományt hirdeti meg; az e
                # fölötti kódot a fogadó eszköz eldobná, ezért nem küldjük ki.
                if a.type == ACT_CONSUMER and not 0 <= a.code <= CONSUMER_MAX_USAGE:
                    problems.append(
                        f"{where}: a média kód a HID tartományon kívül esik "
                        f"(0x{a.code:04X} > 0x{CONSUMER_MAX_USAGE:04X})")
                if not 0 <= a.repeat <= REPEAT_MASK:
                    problems.append(f"{where}: érvénytelen ismétlés ({a.repeat})")
                elif a.repeat and not a.repeat & REPEAT_ENABLED:
                    problems.append(f"{where}: ismétlés-beállítás ismétlés nélkül ({a.repeat})")
                elif a.repeat & REPEAT_HOLD_MOD and not a.repeat & REPEAT_RELEASE:
                    problems.append(
                        f"{where}: a módosító nyomva tartása csak külön leütésekkel működik")
                if not 0 <= a.repeat_ms <= 0xFFFF:
                    problems.append(f"{where}: érvénytelen ismétlési idő ({a.repeat_ms})")
                if a.target and not 1 <= a.target <= TARGET_ALL:
                    problems.append(f"{where}: érvénytelen cél-felülbírálás ({a.target})")
                # A küldési hossz csak a rövid és a dupla nyomásnál értelmes:
                # hosszú nyomásnál a gomb elengedése zárja le a küldést.
                if e == EV_LONG:
                    if a.hold_ms:
                        problems.append(
                            f"{where}: hosszú nyomásnál nincs küldési hossz ({a.hold_ms})")
                else:
                    if a.hold_ms and a.type not in (ACT_KEY, ACT_CONSUMER):
                        problems.append(
                            f"{where}: küldési hossz csak billentyű vagy média "
                            f"műveletnél adható meg")
                    if a.hold_ms and not HOLD_MS_MIN <= a.hold_ms <= HOLD_MS_MAX:
                        problems.append(
                            f"{where}: a küldési hossz {HOLD_MS_MIN}–{HOLD_MS_MAX} ms "
                            f"között lehet ({a.hold_ms})")
                    if a.repeat and not a.hold_ms:
                        problems.append(
                            f"{where}: ismétlés beállítva, de nincs küldési hossz")
    return problems


def default_targets():
    return list(DEFAULT_TARGETS)


def empty_keymap():
    return [[[Action() for _ in range(NUM_EVENTS)]
             for _ in range(NUM_BUTTONS)]
            for _ in range(NUM_MODES)]


# ---------------------------------------------------------------------------
# Soros kapcsolat
# ---------------------------------------------------------------------------

class DeviceError(Exception):
    pass


class DeviceLink:
    """A firmware sor alapú konfigurációs protokollja fölötti burkoló."""

    def __init__(self):
        self.ser = None
        self.info = ""
        self.proto = 0

    @property
    def connected(self):
        return self.ser is not None and self.ser.is_open

    def open(self, port):
        if serial is None:
            raise DeviceError("Hiányzik a pyserial csomag. Telepítsd: pip install pyserial")
        self.close()
        try:
            self.ser = serial.Serial(port, BAUDRATE, timeout=0.4, write_timeout=2.0)
        except Exception as exc:
            self.ser = None
            raise DeviceError(f"A port nem nyitható meg ({port}): {exc}")
        # A TinyUSB CDC-nek kell egy pillanat, mire kész a kapcsolat.
        time.sleep(0.4)
        self.ser.reset_input_buffer()
        # Bármi hiba esetén a portot le kell zárni, különben nyitva ragad, és a
        # program azt hinné, hogy van érvényes kapcsolat.
        try:
            self.info = self.command("PING", timeout=3.0)
            if not self.info.startswith("OK ZWIFT_BUTTONS"):
                raise DeviceError(
                    "Az eszköz nem Zwift Buttons konfigurációs firmware-t futtat.")
            self.proto = self._parse_proto(self.info)
            # Régebbi firmware nem ismeri a SETTARGET parancsot: a küldés a 45
            # billentyű után szakadna meg, félig alkalmazott kiosztást hagyva.
            if self.proto < MIN_PROTO:
                raise DeviceError(
                    f"Az eszközön régi firmware fut (protokoll {self.proto}, "
                    f"ehhez a programhoz {MIN_PROTO} kell). Töltsd fel az "
                    "eszközre a repóban lévő firmware aktuális változatát.")
        except DeviceError:
            self.close()
            raise
        # A gombok debug üzenetei ne zavarják a válaszokat.
        try:
            self.command("DBG 0", timeout=1.5)
        except DeviceError:
            pass

    def close(self):
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None
        self.info = ""
        self.proto = 0

    @staticmethod
    def _parse_proto(banner):
        for token in banner.split():
            if token.startswith("PROTO="):
                try:
                    return int(token[6:])
                except ValueError:
                    return 0
        return 1  # a legelső változat még nem írta ki a verziót

    # -- alacsony szint --

    def _write(self, cmd):
        if not self.connected:
            raise DeviceError("Nincs kapcsolat az eszközzel.")
        try:
            self.ser.reset_input_buffer()
            self.ser.write((cmd + "\n").encode("ascii"))
            self.ser.flush()
        except Exception as exc:
            raise DeviceError(f"Írási hiba: {exc}")

    def _readline(self):
        try:
            raw = self.ser.readline()
        except Exception as exc:
            raise DeviceError(f"Olvasási hiba: {exc}")
        return raw.decode("ascii", errors="replace").strip()

    def command(self, cmd, timeout=2.0):
        """Parancs küldése, amire egy OK/ERR sor a válasz.

        A firmware egyéb kiírásait (debug sorok) átugorja.
        """
        self._write(cmd)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self._readline()
            if not line:
                continue
            if line.startswith("OK"):
                return line
            if line.startswith("ERR"):
                raise DeviceError(f"Az eszköz hibát jelzett: {line} (parancs: {cmd})")
        raise DeviceError(f"Időtúllépés, nincs válasz erre: {cmd}")

    # -- magas szint --

    def read_config(self, timeout=6.0):
        """A teljes konfiguráció beolvasása: (keymap, célpontok)."""
        self._write("GET")
        keymap = empty_keymap()
        targets = default_targets()
        seen = 0
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self._readline()
            if not line:
                continue
            if line == "END":
                if seen != NUM_MODES * NUM_BUTTONS * NUM_EVENTS:
                    raise DeviceError(f"Hiányos kiosztás érkezett ({seen} sor).")
                return keymap, targets
            if line.startswith("ERR"):
                raise DeviceError(f"Az eszköz hibát jelzett: {line}")
            if line.startswith("TARGET "):
                parts = line.split()
                if len(parts) == 3:
                    try:
                        m, mask = int(parts[1]), int(parts[2])
                    except ValueError:
                        continue
                    # Érvénytelen maszkot nem viszünk tovább: a mentéskor
                    # amúgy is elakadna, itt viszont még javítható.
                    if 0 <= m < NUM_MODES and 1 <= mask <= TARGET_ALL:
                        targets[m] = mask
                continue
            if not line.startswith("MAP "):
                continue
            parts = line.split()
            if len(parts) not in (9, 10, 11):
                continue
            try:
                values = [int(x) for x in parts[1:]]
            except ValueError:
                continue
            m, b, e, t, mod, code, rep, ms = values[:8]
            tgt = values[8] if len(values) > 8 else TARGET_INHERIT
            hold = values[9] if len(values) > 9 else 0
            if m >= NUM_MODES or b >= NUM_BUTTONS or e >= NUM_EVENTS:
                continue
            keymap[m][b][e] = Action(t, mod, code, rep, ms, tgt, hold)
            seen += 1
        raise DeviceError("Időtúllépés a kiosztás beolvasása közben.")

    def write_config(self, keymap, targets, progress=None):
        # Előbb ellenőrzünk, csak utána küldünk: egy félúton elakadt küldés
        # félig alkalmazott kiosztást hagyna az eszközön.
        problems = validate_config(keymap, targets)
        if problems:
            extra = f"\n… és további {len(problems) - 8} hiba" if len(problems) > 8 else ""
            raise DeviceError("A konfiguráció hibás, ezért semmit nem küldtem el:\n- "
                              + "\n- ".join(problems[:8]) + extra)
        total = NUM_MODES * NUM_BUTTONS * NUM_EVENTS + NUM_MODES
        done = 0

        def send_step(cmd):
            # Egy félúton megszakadt küldés félig alkalmazott kiosztást hagy az
            # eszközön. A hibaüzenetben megmondjuk, hol tartottunk, hogy a
            # felhasználó tudja: az eszköz állapota vegyes, újraküldés kell.
            nonlocal done
            try:
                self.command(cmd)
            except DeviceError as exc:
                raise DeviceError(
                    f"{exc}\n\nAz eszközre {done}/{total} beállítás ment ki, "
                    "tehát a kiosztás félig alkalmazott. Szüntesd meg a hiba "
                    "okát, és küldd el újra.")
            done += 1
            if progress is not None:
                progress(done, total)

        for m in range(NUM_MODES):
            for b in range(NUM_BUTTONS):
                for e in range(NUM_EVENTS):
                    a = keymap[m][b][e]
                    send_step(f"SET {m} {b} {e} {a.type} {a.modifier} {a.code} "
                              f"{a.repeat} {a.repeat_ms} {a.target} {a.hold_ms}")
        for m in range(NUM_MODES):
            send_step(f"SETTARGET {m} {targets[m]}")

    def read_peers(self, timeout=4.0):
        """(fiókok, élő kapcsolatok) beolvasása.

        fiókok: [{'valid': bool, 'addr': str}, ...]
        kapcsolatok: [{'handle': int, 'addr': str, 'slot': int}, ...]
        """
        self._write("PEERS")
        slots = [{"valid": False, "addr": "-"} for _ in range(NUM_SLOTS)]
        conns = []
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self._readline()
            if not line:
                continue
            if line == "END":
                return slots, conns
            if line.startswith("ERR"):
                raise DeviceError(f"Az eszköz hibát jelzett: {line}")
            parts = line.split()
            if line.startswith("SLOT ") and len(parts) == 4:
                try:
                    idx, valid = int(parts[1]), int(parts[2])
                except ValueError:
                    continue
                if 0 <= idx < NUM_SLOTS:
                    slots[idx] = {"valid": bool(valid), "addr": parts[3]}
            elif line.startswith("CONN ") and len(parts) >= 4:
                try:
                    conns.append({"handle": int(parts[1]), "addr": parts[2],
                                  "slot": int(parts[3]),
                                  "bonded": bool(int(parts[4])) if len(parts) > 4 else True})
                except ValueError:
                    continue
        raise DeviceError("Időtúllépés az eszközlista beolvasása közben.")

    def assign_slot(self, slot, conn_handle):
        return self.command(f"ASSIGN {slot} {conn_handle}", timeout=3.0)

    def clear_slot(self, slot):
        return self.command(f"CLEARSLOT {slot}", timeout=3.0)

    def save_to_flash(self):
        return self.command("SAVE", timeout=5.0)

    def load_defaults(self):
        return self.command("DEFAULTS", timeout=3.0)


# ---------------------------------------------------------------------------
# Művelet szerkesztő ablak
# ---------------------------------------------------------------------------

class ActionDialog(tk.Toplevel):
    """Modális ablak egy művelet beállításához, billentyű-felvétellel."""

    def __init__(self, master, title, action, event):
        tk.Toplevel.__init__(self, master)
        self.title(title)
        self.resizable(False, False)
        self.transient(master)
        self.result = None
        # Hosszú nyomásnál a gomb elengedése zárja le a küldést, rövid és dupla
        # nyomásnál viszont beállítható, hogy meddig menjen ki a parancs.
        self.is_long = (event == EV_LONG)

        self._pressed_mods = set()

        self.type_var = tk.IntVar(value=action.type)
        self.key_var = tk.IntVar(value=action.code if action.type == ACT_KEY else 0)
        # Az eszközön olyan média-kód is lehet, ami nincs a listánkban (pl. soros
        # terminálból állították be). Ilyenkor a listát egészítjük ki, hogy a
        # szerkesztés ne írja felül csendben az eredeti értéket.
        self.consumer_choices = list(CONSUMER_KEYS)
        if action.type == ACT_CONSUMER and action.code not in dict(CONSUMER_KEYS):
            self.consumer_choices.append((action.code, consumer_label(action.code)))
        self.consumer_var = tk.IntVar(
            value=action.code if action.type == ACT_CONSUMER else CONSUMER_KEYS[0][0])
        self.mod_vars = {}
        for bit, name in MODIFIERS:
            self.mod_vars[bit] = tk.IntVar(
                value=1 if (action.type == ACT_KEY and action.modifier & bit) else 0)
        self.repeat_var = tk.IntVar(value=1 if action.repeat & REPEAT_ENABLED else 0)
        # Ha a cellán még nincs ismétlés, a bekapcsoláskor a KÜLÖN LEÜTÉSEK módot
        # kínáljuk fel: enélkül az új ismétlés a nyomva tartott (beragadtnak
        # látszó) módban indulna, amit a gyári kiosztás sem használ.
        self.release_var = tk.IntVar(
            value=1 if (action.repeat & REPEAT_RELEASE)
            or not (action.repeat & REPEAT_ENABLED) else 0)
        self.holdmod_var = tk.IntVar(value=1 if action.repeat & REPEAT_HOLD_MOD else 0)
        self.repeat_ms_var = tk.IntVar(value=action.repeat_ms or 60)
        self.hold_ms_var = tk.IntVar(value=action.hold_ms)
        self.action_target_var = tk.IntVar(value=action.target)

        self._build()
        self._refresh_state()

        self.protocol("WM_DELETE_WINDOW", self._on_cancel)
        self.bind("<Escape>", lambda e: self._on_cancel())
        self.update_idletasks()
        self._center_on(master)
        self.grab_set()
        self.capture.focus_set()

    # -- felépítés --

    def _build(self):
        pad = {"padx": 8, "pady": 4}
        outer = ttk.Frame(self, padding=10)
        outer.pack(fill="both", expand=True)

        # Művelet típusa
        type_box = ttk.LabelFrame(outer, text="Művelet típusa", padding=8)
        type_box.pack(fill="x")
        for value, text in (
            (ACT_NONE, "Nincs művelet"),
            (ACT_KEY, "Billentyű / billentyű-kombináció"),
            (ACT_CONSUMER, "Média billentyű"),
            (ACT_MODE_NEXT, "Üzemmód váltás (következő)"),
            (ACT_VIEW_CYCLE, "Zwift nézetváltás (1-9 körbe)"),
        ):
            ttk.Radiobutton(type_box, text=text, value=value, variable=self.type_var,
                            command=self._refresh_state).pack(anchor="w")

        # Billentyű
        self.key_box = ttk.LabelFrame(outer, text="Billentyű", padding=8)
        self.key_box.pack(fill="x", pady=(8, 0))

        ttk.Label(self.key_box,
                  text="Kattints az alábbi mezőbe, és nyomd le a kívánt kombinációt:"
                  ).grid(row=0, column=0, columnspan=3, sticky="w", pady=(0, 4))

        self.capture = tk.Label(self.key_box, text="", relief="sunken", bd=2,
                                width=34, height=2, bg="#ffffff",
                                highlightthickness=2, highlightbackground="#cccccc")
        self.capture.grid(row=1, column=0, columnspan=2, sticky="we")
        self.capture.bind("<Button-1>", lambda e: self.capture.focus_set())
        self.capture.bind("<KeyPress>", self._on_key_press)
        self.capture.bind("<KeyRelease>", self._on_key_release)
        self.capture.bind("<FocusIn>", self._on_focus_in)
        self.capture.bind("<FocusOut>", self._on_focus_out)
        self.capture.configure(takefocus=True)

        ttk.Button(self.key_box, text="Törlés", width=10,
                   command=self._clear_key).grid(row=1, column=2, padx=(8, 0))

        ttk.Label(self.key_box, text="vagy válaszd ki a listából:").grid(
            row=2, column=0, sticky="w", pady=(8, 2))
        self.key_combo = ttk.Combobox(self.key_box, state="readonly", width=24,
                                      values=[KEY_NAMES[c] for c, _ in KEY_CHOICES])
        self.key_combo.grid(row=3, column=0, columnspan=2, sticky="we")
        self.key_combo.bind("<<ComboboxSelected>>", self._on_key_combo)

        mod_frame = ttk.Frame(self.key_box)
        mod_frame.grid(row=4, column=0, columnspan=3, sticky="w", pady=(8, 0))
        ttk.Label(mod_frame, text="Módosítók:").grid(row=0, column=0, sticky="w")
        for i, (bit, name) in enumerate(MODIFIERS):
            ttk.Checkbutton(mod_frame, text=name, variable=self.mod_vars[bit],
                            command=self._refresh_state).grid(
                row=1 + i // 4, column=i % 4, sticky="w", padx=(0, 10))

        # Média billentyű
        self.consumer_box = ttk.LabelFrame(outer, text="Média billentyű", padding=8)
        self.consumer_box.pack(fill="x", pady=(8, 0))
        self.consumer_combo = ttk.Combobox(
            self.consumer_box, state="readonly", width=32,
            values=[name for _, name in self.consumer_choices])
        self.consumer_combo.pack(fill="x")
        self.consumer_combo.bind("<<ComboboxSelected>>", self._on_consumer_combo)

        # Küldés hossza és ismétlése
        self.repeat_box = ttk.LabelFrame(
            outer,
            text="Nyomva tartás" if self.is_long else "A parancs küldésének hossza",
            padding=8)
        self.repeat_box.pack(fill="x", pady=(8, 0))

        row = 0
        if not self.is_long:
            ttk.Label(self.repeat_box, text="Küldés hossza (ms):").grid(
                row=row, column=0, sticky="w")
            self.hold_spin = ttk.Spinbox(self.repeat_box, from_=0, to=HOLD_MS_MAX,
                                         increment=50, width=8,
                                         textvariable=self.hold_ms_var,
                                         command=self._refresh_state)
            self.hold_spin.grid(row=row, column=1, sticky="w", padx=(8, 0))
            ttk.Label(self.repeat_box, foreground="#555555", wraplength=430,
                      text=("0 = a szokásos rövid impulzus. Ennél hosszabb küldés "
                            f"({HOLD_MS_MIN}–{HOLD_MS_MAX} ms) addig tartja a "
                            "parancsot – például a telefon asszisztensének "
                            "indításához. Amíg tart, más gomb parancsa nem megy "
                            "ki.")).grid(row=row + 1, column=0, columnspan=2,
                                         sticky="w", pady=(2, 6))
            row += 2
        else:
            self.hold_spin = None

        self.repeat_check = ttk.Checkbutton(
            self.repeat_box,
            text=("Ismétlés, amíg nyomva tartod" if self.is_long
                  else "Ismétlés a küldés alatt"),
            variable=self.repeat_var, command=self._refresh_state)
        self.repeat_check.grid(row=row, column=0, sticky="w")
        ttk.Label(self.repeat_box, text="Ismétlési idő (ms):").grid(
            row=row + 1, column=0, sticky="w", pady=(4, 0))
        self.repeat_spin = ttk.Spinbox(self.repeat_box, from_=10, to=2000,
                                       increment=10, width=8,
                                       textvariable=self.repeat_ms_var,
                                       command=self._update_preview)
        self.repeat_spin.grid(row=row + 1, column=1, sticky="w", padx=(8, 0), pady=(4, 0))
        self.release_check = ttk.Checkbutton(
            self.repeat_box,
            text="Külön leütésekként (felengedés minden ismétlés után)",
            variable=self.release_var, command=self._refresh_state)
        self.release_check.grid(row=row + 2, column=0, columnspan=2,
                                sticky="w", pady=(6, 0))
        self.holdmod_check = ttk.Checkbutton(
            self.repeat_box,
            text="A módosító (Alt, Ctrl, …) maradjon nyomva – Alt+Tab-hoz kell",
            variable=self.holdmod_var, command=self._update_preview)
        self.holdmod_check.grid(row=row + 3, column=0, columnspan=2,
                                sticky="w", padx=(18, 0))
        ttk.Label(self.repeat_box, wraplength=430, foreground="#555555", text=(
            "Külön leütések nélkül a billentyű végig lenyomva marad, és a "
            "számítógép a saját ismétlési sebességével pörgeti – ilyenkor a "
            "fenti idő nem érvényesül. A módosító nyomva tartása az Alt+Tab "
            "ablakváltáshoz kell: a Windows csak addig lépked tovább, amíg "
            "az Alt nyomva van, különben csak két ablak között vált.")).grid(
            row=row + 4, column=0, columnspan=2, sticky="w", pady=(4, 0))

        # Cél-eszköz felülbírálás. Minden cellánál felkínáljuk; alapértelmezés az
        # üzemmódnál beállított célpont öröklése, így a beállítás csak akkor tér
        # el, ha a felhasználó szándékosan átállítja.
        self.target_box = ttk.LabelFrame(outer, text="Cél eszköz", padding=8)
        self.target_box.pack(fill="x", pady=(8, 0))
        choices = [(TARGET_INHERIT, "Az üzemmódnál beállított célpont")]
        choices += list(TARGET_CHOICES)
        for value, text in choices:
            ttk.Radiobutton(self.target_box, text=text, value=value,
                            variable=self.action_target_var,
                            command=self._update_preview).pack(anchor="w")
        # Előnézet + gombok
        self.preview = ttk.Label(outer, text="", font=("TkDefaultFont", 10, "bold"))
        self.preview.pack(anchor="w", pady=(10, 0))

        buttons = ttk.Frame(outer)
        buttons.pack(fill="x", pady=(10, 0))
        ttk.Button(buttons, text="Mégse", command=self._on_cancel).pack(side="right")
        ttk.Button(buttons, text="OK", command=self._on_ok).pack(side="right", padx=(0, 6))
        del pad

    def _center_on(self, master):
        try:
            x = master.winfo_rootx() + (master.winfo_width() - self.winfo_width()) // 2
            y = master.winfo_rooty() + (master.winfo_height() - self.winfo_height()) // 3
            self.geometry(f"+{max(x, 0)}+{max(y, 0)}")
        except tk.TclError:
            pass

    # -- billentyű felvétel --

    def _on_focus_in(self, _event):
        self._pressed_mods.clear()
        self.capture.configure(highlightbackground="#2a7ade", bg="#eef6ff")
        self._update_preview()

    def _on_focus_out(self, _event):
        self._pressed_mods.clear()
        self.capture.configure(highlightbackground="#cccccc", bg="#ffffff")
        self._update_preview()

    def _on_key_press(self, event):
        keysym = event.keysym
        if keysym in MODIFIER_KEYSYMS:
            self._pressed_mods.add(MODIFIER_KEYSYMS[keysym])
            self._update_preview()
            return "break"

        code = keysym_to_hid(keysym)
        if code is None:
            return "break"

        modifier = 0
        for bit in self._pressed_mods:
            modifier |= bit
        # Kiegészítés az eseményből (néhány rendszeren megbízhatóbb)
        state = getattr(event, "state", 0)
        if isinstance(state, int):
            if state & 0x0001:
                modifier |= 0x02  # Shift
            if state & 0x0004:
                modifier |= 0x01  # Ctrl
            if state & 0x20000:
                modifier |= 0x04  # Alt (Windows)

        self.type_var.set(ACT_KEY)
        self.key_var.set(code)
        for bit, _name in MODIFIERS:
            self.mod_vars[bit].set(1 if modifier & bit else 0)
        self._refresh_state()
        return "break"

    def _on_key_release(self, event):
        bit = MODIFIER_KEYSYMS.get(event.keysym)
        if bit is not None:
            self._pressed_mods.discard(bit)
            self._update_preview()
        return "break"

    def _clear_key(self):
        self.key_var.set(0)
        for bit, _name in MODIFIERS:
            self.mod_vars[bit].set(0)
        self._refresh_state()
        self.capture.focus_set()

    def _on_key_combo(self, _event):
        idx = self.key_combo.current()
        if idx >= 0:
            self.type_var.set(ACT_KEY)
            self.key_var.set(KEY_CHOICES[idx][0])
            self._refresh_state()

    def _on_consumer_combo(self, _event):
        idx = self.consumer_combo.current()
        if idx >= 0:
            self.type_var.set(ACT_CONSUMER)
            self.consumer_var.set(self.consumer_choices[idx][0])
            self._refresh_state()

    # -- állapot --

    def _hold_ms(self):
        """A beállított küldési hossz, a firmware határaira igazítva."""
        if self.is_long:
            return 0
        try:
            value = int(self.hold_ms_var.get())
        except (tk.TclError, ValueError):
            return 0
        if value <= 0:
            return 0
        return min(max(value, HOLD_MS_MIN), HOLD_MS_MAX)

    def _current_modifier(self):
        modifier = 0
        for bit, _name in MODIFIERS:
            if self.mod_vars[bit].get():
                modifier |= bit
        return modifier

    def _set_widget_state(self, container, state):
        for child in container.winfo_children():
            try:
                child.configure(state=state)
            except tk.TclError:
                pass
            self._set_widget_state(child, state)

    def _refresh_state(self):
        atype = self.type_var.get()

        # A billentyű-felvevő és a listák szándékosan mindig aktívak: ha a
        # felhasználó lenyom egy billentyűt vagy választ a listából, az magától
        # átállítja a művelet típusát. (Letiltva a felvevő mező meg sem kapná a
        # billentyű-eseményeket, ami épp a fő használati módot törné el.)
        code = self.key_var.get()
        index = next((i for i, (c, _) in enumerate(KEY_CHOICES) if c == code), 0)
        self.key_combo.current(index)

        code = self.consumer_var.get()
        index = next((i for i, (c, _) in enumerate(self.consumer_choices)
                      if c == code), 0)
        self.consumer_combo.current(index)

        # A küldési hossz és az ismétlés csak billentyű/média műveletnél él.
        sends_something = atype in (ACT_KEY, ACT_CONSUMER)
        if not sends_something:
            self.repeat_var.set(0)
            if not self.is_long:
                self.hold_ms_var.set(0)
        self._set_widget_state(self.repeat_box,
                               "normal" if sends_something else "disabled")

        # Rövid/dupla nyomásnál az ismétlésnek csak akkor van értelme, ha a
        # küldés tart is valameddig: egy pillanatnyi impulzus alatt nincs mit
        # ismételni. A firmware ugyanezt a beállítást vissza is utasítja.
        timed = self.is_long or self._hold_ms() > 0
        can_repeat = sends_something and timed
        if not can_repeat:
            self.repeat_var.set(0)
        # Enélkül a kapcsoló bekattintható maradna, majd magától visszaugrana.
        self.repeat_check.state(["!disabled"] if can_repeat else ["disabled"])
        repeating = can_repeat and bool(self.repeat_var.get())
        if not repeating:
            # Ismétlés nélkül sem az idő, sem a leütés-mód nem értelmes.
            self.repeat_spin.configure(state="disabled")

        # A módosító nyomva tartását a firmware csak billentyű-műveletnél és
        # csak külön leütések mellett veszi figyelembe; máshol félrevezető
        # lenne felkínálni, mert a felirat olyat ígérne, ami nem történik meg.
        hold_ok = (repeating and bool(self.release_var.get())
                   and atype == ACT_KEY and self._current_modifier() != 0)
        self.release_check.state(["!disabled"] if repeating else ["disabled"])
        self.holdmod_check.state(["!disabled"] if hold_ok else ["disabled"])
        if not hold_ok:
            self.holdmod_var.set(0)

        # Célpontja csak annak a műveletnek van, ami küld is valamit: a „Nincs
        # művelet" és az üzemmódváltás nem megy ki egyik eszközre sem.
        can_target = atype in (ACT_KEY, ACT_CONSUMER, ACT_VIEW_CYCLE)
        if not can_target:
            self.action_target_var.set(TARGET_INHERIT)
        self._set_widget_state(self.target_box, "normal" if can_target else "disabled")

        self._update_preview()

    def _update_preview(self):
        action = self._build_action()
        hint = ""
        if self.type_var.get() == ACT_KEY and self.focus_get() is self.capture:
            live = "+".join(name for bit, name in MODIFIERS if bit in self._pressed_mods)
            if live:
                hint = f"   [lenyomva: {live}+…]"
        self.preview.configure(text="Beállítás: " + action.label() + hint)

    def _build_action(self):
        atype = self.type_var.get()
        hold_ms = self._hold_ms() if atype in (ACT_KEY, ACT_CONSUMER) else 0
        # Rövid/dupla nyomásnál ismétlés csak küldési hossz mellett létezik.
        repeat = self.repeat_var.get() if (self.is_long or hold_ms) else 0
        try:
            repeat_ms = int(self.repeat_ms_var.get())
        except (tk.TclError, ValueError):
            repeat_ms = 60
        repeat_ms = min(max(repeat_ms, 10), 2000)
        if repeat:
            repeat = REPEAT_ENABLED
            if self.release_var.get():
                repeat |= REPEAT_RELEASE
                # A firmware ennél rövidebb időt úgysem tud tartani (a leütés
                # impulzusának is be kell férnie), és csendben felhúzná — akkor
                # viszont a felirat mást ígérne, mint ami történik.
                repeat_ms = max(repeat_ms, REPEAT_MIN_MS_RELEASE)
                if self.holdmod_var.get():
                    repeat |= REPEAT_HOLD_MOD
        target = self.action_target_var.get()
        if atype == ACT_KEY:
            return Action(ACT_KEY, self._current_modifier(), self.key_var.get(),
                          repeat, repeat_ms, target, hold_ms)
        if atype == ACT_CONSUMER:
            return Action(ACT_CONSUMER, 0, self.consumer_var.get(), repeat,
                          repeat_ms, target, hold_ms)
        return Action(atype, 0, 0, 0, repeat_ms, target, 0)

    def _on_ok(self):
        action = self._build_action()
        if action.type == ACT_KEY and action.code == 0:
            messagebox.showwarning("Hiányzó billentyű",
                                   "Válassz billentyűt, vagy állítsd a típust "
                                   "„Nincs művelet”-re.", parent=self)
            return
        self.result = action
        self.grab_release()
        self.destroy()

    def _on_cancel(self):
        self.result = None
        self.grab_release()
        self.destroy()


# ---------------------------------------------------------------------------
# Eszköz-hozzárendelés (melyik BLE eszköz a PC és melyik a telefon)
# ---------------------------------------------------------------------------

class PeersDialog(tk.Toplevel):
    """A csatlakozott BLE eszközök hozzárendelése a PC / telefon fiókokhoz.

    A kapcsolat-azonosítók csatlakozási sorrendben keletkeznek, ezért az eszköz
    a BLE cím alapján jegyzi meg, melyik a PC és melyik a telefon.
    """

    def __init__(self, master, link):
        tk.Toplevel.__init__(self, master)
        self.title("Eszközök hozzárendelése")
        self.resizable(False, False)
        self.transient(master)
        self.link = link
        self.slot_labels = []
        self.conn_frame = None

        outer = ttk.Frame(self, padding=10)
        outer.pack(fill="both", expand=True)

        ttk.Label(outer, wraplength=520, text=(
            "Párosítsd az eszközt mindkét géppel, majd itt add meg, melyik "
            "melyik. A hozzárendelés a BLE cím alapján történik, így "
            "újracsatlakozás után is megmarad.")).pack(anchor="w", pady=(0, 10))

        slot_box = ttk.LabelFrame(outer, text="Jelenlegi hozzárendelés", padding=8)
        slot_box.pack(fill="x")
        for slot in range(NUM_SLOTS):
            row = ttk.Frame(slot_box)
            row.pack(fill="x", pady=2)
            ttk.Label(row, text=SLOT_NAMES[slot] + ":", width=14).pack(side="left")
            lbl = ttk.Label(row, text="—", font=("TkFixedFont", 9))
            lbl.pack(side="left")
            ttk.Button(row, text="Törlés", width=8,
                       command=lambda s=slot: self._clear(s)).pack(side="right")
            self.slot_labels.append(lbl)

        self.conn_box = ttk.LabelFrame(outer, text="Most csatlakozott eszközök", padding=8)
        self.conn_box.pack(fill="x", pady=(10, 0))

        buttons = ttk.Frame(outer)
        buttons.pack(fill="x", pady=(10, 0))
        ttk.Button(buttons, text="Bezárás", command=self._close).pack(side="right")
        ttk.Button(buttons, text="Frissítés", command=self.refresh).pack(side="right", padx=(0, 6))

        self.status = ttk.Label(outer, text="", wraplength=520, foreground="#555555")
        self.status.pack(anchor="w", pady=(8, 0))

        self.protocol("WM_DELETE_WINDOW", self._close)
        self.bind("<Escape>", lambda e: self._close())
        self.refresh()
        self.update_idletasks()
        self.grab_set()

    def refresh(self):
        try:
            slots, conns = self.link.read_peers()
        except DeviceError as exc:
            self.status.configure(text=str(exc), foreground="#a03030")
            return

        for slot in range(NUM_SLOTS):
            info = slots[slot]
            self.slot_labels[slot].configure(
                text=info["addr"] if info["valid"] else "— (nincs hozzárendelve)")

        if self.conn_frame is not None:
            self.conn_frame.destroy()
        self.conn_frame = ttk.Frame(self.conn_box)
        self.conn_frame.pack(fill="x")

        if not conns:
            ttk.Label(self.conn_frame,
                      text="Jelenleg egy eszköz sincs csatlakozva BLE-n.").pack(anchor="w")
        unassigned = 0
        unbonded = 0
        for conn in conns:
            row = ttk.Frame(self.conn_frame)
            row.pack(fill="x", pady=2)
            assigned = 0 <= conn["slot"] < NUM_SLOTS
            if not assigned:
                unassigned += 1
            if not conn.get("bonded", True):
                unbonded += 1
                current = "nincs párosítva"
            else:
                current = SLOT_NAMES[conn["slot"]] if assigned else "nincs hozzárendelve"
            ttk.Label(row, text=conn["addr"], font=("TkFixedFont", 9), width=20).pack(side="left")
            ttk.Label(row, text=f"({current})", width=18).pack(side="left")
            for slot in range(NUM_SLOTS):
                btn = ttk.Button(row, text="Ez a " + SLOT_NAMES[slot], width=16,
                                 command=lambda s=slot, h=conn["handle"]: self._assign(s, h))
                btn.pack(side="left", padx=(4, 0))
                # Párosítás előtt a BLE cím még változó, tehát nem menthető el.
                if not conn.get("bonded", True):
                    btn.state(["disabled"])

        any_assigned = any(sl["valid"] for sl in slots)
        if unbonded:
            self.status.configure(
                text="Van olyan csatlakozott eszköz, amellyel az eszköz még nincs "
                     "párosítva. A BLE cím párosítás előtt változó, ezért nem "
                     "menthető el – fejezd be a párosítást, majd Frissítés.",
                foreground="#a06000")
        elif any_assigned and unassigned:
            self.status.configure(
                text="Figyelem: van hozzá nem rendelt csatlakozott eszköz. Amint "
                     "legalább egy hozzárendelés létezik, a hozzá nem rendelt "
                     "eszközök EGYETLEN parancsot sem kapnak meg.",
                foreground="#a06000")
        else:
            self.status.configure(
                text="A hozzárendelés azonnal érvényes; a megőrzéshez a főablakban "
                     "mentsd az eszköz memóriájába.", foreground="#555555")

    def _assign(self, slot, handle):
        try:
            self.link.assign_slot(slot, handle)
        except DeviceError as exc:
            self.status.configure(text=str(exc), foreground="#a03030")
            return
        self.refresh()

    def _clear(self, slot):
        try:
            self.link.clear_slot(slot)
        except DeviceError as exc:
            self.status.configure(text=str(exc), foreground="#a03030")
            return
        self.refresh()

    def _close(self):
        self.grab_release()
        self.destroy()


# ---------------------------------------------------------------------------
# Főablak
# ---------------------------------------------------------------------------

class App(ttk.Frame):

    def __init__(self, master):
        ttk.Frame.__init__(self, master, padding=10)
        self.pack(fill="both", expand=True)
        self.master.title("Zwift Buttons – gomb-kiosztás konfiguráló")

        self.link = DeviceLink()
        self.keymap = empty_keymap()
        self.targets = default_targets()
        self.cells = {}
        self.target_vars = {}
        self.dirty = False

        self._build()
        self.refresh_ports()
        self._load_local_defaults()
        self._refresh_all_cells()
        self.master.protocol("WM_DELETE_WINDOW", self.on_close)

    # -- felépítés --

    def _build(self):
        top = ttk.Frame(self)
        top.pack(fill="x")

        ttk.Label(top, text="Soros port:").pack(side="left")
        self.port_combo = ttk.Combobox(top, state="readonly", width=38)
        self.port_combo.pack(side="left", padx=(6, 6))
        ttk.Button(top, text="Frissítés", command=self.refresh_ports).pack(side="left")
        self.connect_btn = ttk.Button(top, text="Csatlakozás", command=self.toggle_connection)
        self.connect_btn.pack(side="left", padx=(6, 0))

        self.status = ttk.Label(self, text="Nincs kapcsolat.", foreground="#a03030")
        self.status.pack(fill="x", pady=(6, 8))

        self.notebook = ttk.Notebook(self)
        self.notebook.pack(fill="both", expand=True)
        for mode in range(NUM_MODES):
            self.notebook.add(self._build_mode_tab(mode), text=MODE_NAMES[mode])

        bottom = ttk.Frame(self)
        bottom.pack(fill="x", pady=(10, 0))

        ttk.Button(bottom, text="Beolvasás az eszközről",
                   command=self.on_read_device).pack(side="left")
        ttk.Button(bottom, text="Küldés az eszközre",
                   command=self.on_write_device).pack(side="left", padx=(6, 0))
        ttk.Button(bottom, text="Mentés az eszköz memóriájába",
                   command=self.on_save_device).pack(side="left", padx=(6, 0))
        ttk.Button(bottom, text="Gyári alapértelmezés",
                   command=self.on_device_defaults).pack(side="left", padx=(6, 0))
        ttk.Button(bottom, text="Eszközök hozzárendelése…",
                   command=self.on_peers).pack(side="left", padx=(16, 0))

        ttk.Button(bottom, text="Megnyitás fájlból…",
                   command=self.on_open_file).pack(side="right")
        ttk.Button(bottom, text="Mentés fájlba…",
                   command=self.on_save_file).pack(side="right", padx=(0, 6))

        hint = ("Kattints egy cellára a billentyű beállításához. "
                "A „Küldés az eszközre” csak az eszköz memóriájába tölti fel a "
                "kiosztást; hogy újraindítás után is megmaradjon, nyomd meg a "
                "„Mentés az eszköz memóriájába” gombot is.")
        ttk.Label(self, text=hint, wraplength=880, foreground="#555555").pack(
            fill="x", pady=(8, 0))

    def _build_mode_tab(self, mode):
        frame = ttk.Frame(self.notebook, padding=10)
        for col in range(1, NUM_EVENTS + 1):
            frame.columnconfigure(col, weight=1, uniform="ev")

        # Üzemmódonkénti cél-eszköz
        target_box = ttk.LabelFrame(frame, text="Ebben az üzemmódban a parancsok célja",
                                    padding=8)
        target_box.grid(row=0, column=0, columnspan=NUM_EVENTS + 1,
                        sticky="we", pady=(0, 10))
        var = tk.IntVar(value=self.targets[mode])
        self.target_vars[mode] = var
        for i, (value, text) in enumerate(TARGET_CHOICES):
            ttk.Radiobutton(target_box, text=text, value=value, variable=var,
                            command=lambda m=mode: self._on_target_changed(m)
                            ).grid(row=0, column=i, sticky="w", padx=(0, 18))

        ttk.Label(frame, text="Gomb", font=("TkDefaultFont", 9, "bold")).grid(
            row=1, column=0, padx=4, pady=(0, 6), sticky="w")
        for e in range(NUM_EVENTS):
            ttk.Label(frame, text=EVENT_NAMES[e], font=("TkDefaultFont", 9, "bold")).grid(
                row=1, column=e + 1, padx=4, pady=(0, 6))

        for b in range(NUM_BUTTONS):
            ttk.Label(frame, text=f"Gomb {b + 1}").grid(
                row=b + 2, column=0, padx=4, pady=3, sticky="w")
            for e in range(NUM_EVENTS):
                btn = ttk.Button(frame, text="—", width=26,
                                 command=lambda m=mode, bb=b, ee=e: self.edit_cell(m, bb, ee))
                btn.grid(row=b + 2, column=e + 1, padx=4, pady=3, sticky="we")
                self.cells[(mode, b, e)] = btn
        return frame

    def _on_target_changed(self, mode):
        self.targets[mode] = self.target_vars[mode].get()
        self.dirty = True
        # Az öröklődő cellák felirata az üzemmód célpontját mutatja, ezért újra
        # kell írni őket.
        self._refresh_all_cells()
        self._set_status(f"{MODE_NAMES[mode]}: {target_label(self.targets[mode])} – "
                         "a „Küldés az eszközre” gombbal lép érvénybe.")

    def _refresh_targets(self):
        for mode in range(NUM_MODES):
            if mode in self.target_vars:
                self.target_vars[mode].set(self.targets[mode])

    # -- cellák --

    def _refresh_all_cells(self):
        for (m, b, e), btn in self.cells.items():
            btn.configure(text=self.keymap[m][b][e].cell_label(self.targets[m]))
        self._refresh_targets()

    def edit_cell(self, mode, button, event):
        title = f"{MODE_NAMES[mode]} – Gomb {button + 1} – {EVENT_NAMES[event]}"
        dialog = ActionDialog(self.master, title, self.keymap[mode][button][event],
                              event=event)
        self.master.wait_window(dialog)
        if dialog.result is not None:
            self.keymap[mode][button][event] = dialog.result
            self.cells[(mode, button, event)].configure(
                text=dialog.result.cell_label(self.targets[mode]))
            self.dirty = True
            self._set_status("Módosítva – a „Küldés az eszközre” gombbal lép érvénybe.")

    # -- kapcsolat --

    def refresh_ports(self):
        if list_ports is None:
            self.port_combo["values"] = []
            self._set_status("Hiányzik a pyserial csomag – telepítsd: pip install pyserial",
                             error=True)
            return
        ports = list(list_ports.comports())
        labels = [f"{p.device} – {p.description}" for p in ports]
        self._ports = [p.device for p in ports]
        self.port_combo["values"] = labels
        if labels:
            # Ha van XIAO/nRF52/Adafruit jellegű port, azt válasszuk elsőként.
            preferred = 0
            for i, p in enumerate(ports):
                text = f"{p.description} {p.manufacturer or ''}".lower()
                if any(k in text for k in ("xiao", "nrf52", "seeed", "adafruit", "cdc")):
                    preferred = i
                    break
            self.port_combo.current(preferred)
        else:
            self.port_combo.set("")

    def toggle_connection(self):
        if self.link.connected:
            self.link.close()
            self.connect_btn.configure(text="Csatlakozás")
            self._set_status("Nincs kapcsolat.", error=True)
            return

        idx = self.port_combo.current()
        if idx < 0 or not getattr(self, "_ports", None):
            messagebox.showwarning("Nincs port", "Válassz soros portot.")
            return
        port = self._ports[idx]
        try:
            self._busy(True)
            self.link.open(port)
        except DeviceError as exc:
            self._set_status(str(exc), error=True)
            messagebox.showerror("Csatlakozási hiba", str(exc))
            return
        finally:
            self._busy(False)

        self.connect_btn.configure(text="Bontás")
        self._set_status(f"Csatlakozva: {port}  ({self.link.info})")
        if messagebox.askyesno("Beolvasás",
                               "Beolvassam az eszközön lévő jelenlegi kiosztást?"):
            self.on_read_device()

    def _require_link(self):
        if not self.link.connected:
            messagebox.showwarning("Nincs kapcsolat",
                                   "Előbb csatlakozz az eszközhöz.")
            return False
        return True

    # -- eszköz műveletek --

    def on_read_device(self):
        if not self._require_link():
            return
        try:
            self._busy(True)
            self.keymap, self.targets = self.link.read_config()
        except DeviceError as exc:
            messagebox.showerror("Olvasási hiba", str(exc))
            return
        finally:
            self._busy(False)
        self._refresh_all_cells()
        self.dirty = False
        self._set_status("A kiosztás beolvasva az eszközről.")

    def on_write_device(self):
        if not self._require_link():
            return
        try:
            self._busy(True)
            self.link.write_config(
                self.keymap, self.targets,
                progress=lambda d, t: self._set_status(
                    f"Küldés… {d}/{t}", update=True))
        except DeviceError as exc:
            messagebox.showerror("Küldési hiba", str(exc))
            return
        finally:
            self._busy(False)
        self.dirty = False
        self._set_status("A kiosztás elküldve. Azonnal érvényes; a végleges "
                         "megőrzéshez mentsd az eszköz memóriájába.")

    def on_save_device(self):
        if not self._require_link():
            return
        try:
            self._busy(True)
            self.link.save_to_flash()
        except DeviceError as exc:
            messagebox.showerror("Mentési hiba", str(exc))
            return
        finally:
            self._busy(False)
        self._set_status("A kiosztás elmentve az eszköz memóriájába.")

    def on_device_defaults(self):
        if not self._require_link():
            return
        if not messagebox.askyesno(
                "Gyári alapértelmezés",
                "Visszaállítsam az eszközön a gyári kiosztást?\n"
                "A jelenlegi (el nem mentett) beállítások elvesznek."):
            return
        try:
            self._busy(True)
            self.link.load_defaults()
            self.keymap, self.targets = self.link.read_config()
        except DeviceError as exc:
            messagebox.showerror("Hiba", str(exc))
            return
        finally:
            self._busy(False)
        self._refresh_all_cells()
        self.dirty = False
        self._set_status("Gyári kiosztás betöltve az eszközre (még nincs elmentve).")

    def on_peers(self):
        if not self._require_link():
            return
        dialog = PeersDialog(self.master, self.link)
        self.master.wait_window(dialog)
        self._set_status("Eszköz-hozzárendelés lezárva. A megőrzéshez mentsd "
                         "az eszköz memóriájába.")

    # -- fájl műveletek --

    def on_save_file(self):
        path = filedialog.asksaveasfilename(
            title="Kiosztás mentése", defaultextension=".json",
            filetypes=[("JSON fájl", "*.json"), ("Minden fájl", "*.*")])
        if not path:
            return
        data = {
            "format": FILE_FORMAT,
            "version": FILE_VERSION,
            "modes": [
                {
                    "name": MODE_NAMES[m],
                    "target": self.targets[m],
                    "buttons": [
                        {EVENT_KEYS[e]: self.keymap[m][b][e].to_dict()
                         for e in range(NUM_EVENTS)}
                        for b in range(NUM_BUTTONS)
                    ],
                }
                for m in range(NUM_MODES)
            ],
        }
        try:
            with open(path, "w", encoding="utf-8") as fh:
                json.dump(data, fh, indent=2, ensure_ascii=False)
        except OSError as exc:
            messagebox.showerror("Mentési hiba", str(exc))
            return
        # A munka fájlba került, tehát kilépéskor már nincs mit félteni. (Az
        # eszközre küldés ettől függetlenül külön lépés – ezt írja a státusz is.)
        self.dirty = False
        self._set_status(f"Elmentve: {path} – az eszközre külön kell elküldeni.")

    def on_open_file(self):
        path = filedialog.askopenfilename(
            title="Kiosztás megnyitása",
            filetypes=[("JSON fájl", "*.json"), ("Minden fájl", "*.*")])
        if not path:
            return
        try:
            with open(path, "r", encoding="utf-8") as fh:
                data = json.load(fh)
        except (OSError, ValueError) as exc:
            messagebox.showerror("Megnyitási hiba", str(exc))
            return
        # Érvényes JSON is lehet lista vagy szám: ilyenkor a .get() hívás
        # kezeletlen kivétellel szállna el, a felhasználó pedig semmit nem látna.
        if not isinstance(data, dict) or data.get("format") != FILE_FORMAT:
            messagebox.showerror("Ismeretlen fájl",
                                 "Ez nem Zwift Buttons kiosztás-fájl.")
            return
        notes = []
        try:
            keymap, targets = _parse_keymap_file(data, notes)
        except (KeyError, TypeError, ValueError, IndexError) as exc:
            messagebox.showerror("Hibás fájl", f"A fájl tartalma hibás: {exc}")
            return
        self.keymap = keymap
        self.targets = targets
        self._refresh_all_cells()
        # A betöltött tartalom megvan a fájlban, tehát kilépéskor nem veszne el;
        # az eszközre viszont még nem ment ki.
        self.dirty = False
        if notes:
            # A régi formátum átalakítása nem maradhat néma: a felhasználó
            # szándékos beállítását is érintheti.
            messagebox.showinfo("Régi kiosztás-fájl", "\n\n".join(notes))
        self._set_status(f"Betöltve: {path} – az eszközre külön kell elküldeni.")

    def _load_local_defaults(self):
        """Induláskor a gyári kiosztás betöltése a repóban lévő fájlból.

        Mindent-vagy-semmit: egy hibás fájlból nem töltünk be részleges
        kiosztást, mert az üres celláknak látszana, és a „Küldés az eszközre”
        felülírná velük az eszköz valódi beállításait.
        """
        if not DEFAULTS_PATH.exists():
            return
        try:
            keymap, targets = _parse_keymap_file(
                json.loads(DEFAULTS_PATH.read_text(encoding="utf-8")))
        except (OSError, ValueError, KeyError, TypeError, IndexError) as exc:
            print(f"Figyelem: a {DEFAULTS_PATH.name} nem olvasható ({exc}); "
                  "üres kiosztással indulok.", file=sys.stderr)
            return
        self.keymap, self.targets = keymap, targets

    # -- segédek --

    def _busy(self, on):
        try:
            self.master.configure(cursor="watch" if on else "")
            self.master.update_idletasks()
        except tk.TclError:
            pass

    def _set_status(self, text, error=False, update=False):
        self.status.configure(text=text, foreground="#a03030" if error else "#206020")
        if update:
            self.master.update_idletasks()

    def on_close(self):
        # A kapcsolat meglététől függetlenül kérdezünk: eszköz nélkül szerkesztve
        # is elveszne a munka, ha nem mentették fájlba.
        if self.dirty:
            if not messagebox.askokcancel(
                    "Kilépés",
                    "Vannak el nem mentett módosítások. Biztosan kilépsz?"):
                return
        self.link.close()
        self.master.destroy()


def main():
    root = tk.Tk()
    try:
        root.call("tk", "scaling", 1.2)
    except tk.TclError:
        pass
    App(root)
    root.minsize(900, 520)
    root.mainloop()


if __name__ == "__main__":
    main()
