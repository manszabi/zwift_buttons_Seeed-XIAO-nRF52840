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
import os
import sys
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from hid_tables import (  # noqa: E402
    ACT_CONSUMER, ACT_KEY, ACT_MODE_NEXT, ACT_NONE, ACT_VIEW_CYCLE,
    CONSUMER_KEYS, EVENT_KEYS, EVENT_NAMES, EV_LONG, KEY_CHOICES, KEY_NAMES,
    MODE_NAMES, MODIFIERS, MODIFIER_KEYSYMS, consumer_label, key_label,
    keysym_to_hid,
)

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover
    serial = None
    list_ports = None

BAUDRATE = 115200
NUM_MODES = 3
NUM_BUTTONS = 5
NUM_EVENTS = 3
FILE_FORMAT = "zwift-buttons-keymap"
FILE_VERSION = 1


# ---------------------------------------------------------------------------
# Adatmodell
# ---------------------------------------------------------------------------

class Action(object):
    """Egy (üzemmód, gomb, esemény) hármashoz tartozó művelet."""

    __slots__ = ("type", "modifier", "code", "repeat", "repeat_ms")

    def __init__(self, type=ACT_NONE, modifier=0, code=0, repeat=0, repeat_ms=60):
        self.type = type
        self.modifier = modifier
        self.code = code
        self.repeat = repeat
        self.repeat_ms = repeat_ms

    def copy(self):
        return Action(self.type, self.modifier, self.code, self.repeat, self.repeat_ms)

    def label(self):
        if self.type == ACT_KEY:
            text = key_label(self.modifier, self.code)
        elif self.type == ACT_CONSUMER:
            text = consumer_label(self.code)
        elif self.type == ACT_MODE_NEXT:
            return "Üzemmód váltás"
        elif self.type == ACT_VIEW_CYCLE:
            return "Nézetváltás (1-9)"
        else:
            return "—"
        if self.repeat:
            text += "  (ismétlő {} ms)".format(self.repeat_ms)
        return text

    def to_dict(self):
        return {
            "type": self.type,
            "modifier": self.modifier,
            "code": self.code,
            "repeat": self.repeat,
            "repeat_ms": self.repeat_ms,
        }

    @staticmethod
    def from_dict(d):
        return Action(
            int(d.get("type", ACT_NONE)),
            int(d.get("modifier", 0)),
            int(d.get("code", 0)),
            int(d.get("repeat", 0)),
            int(d.get("repeat_ms", 60)),
        )


def empty_keymap():
    return [[[Action() for _ in range(NUM_EVENTS)]
             for _ in range(NUM_BUTTONS)]
            for _ in range(NUM_MODES)]


# ---------------------------------------------------------------------------
# Soros kapcsolat
# ---------------------------------------------------------------------------

class DeviceError(Exception):
    pass


class DeviceLink(object):
    """A firmware sor alapú konfigurációs protokollja fölötti burkoló."""

    def __init__(self):
        self.ser = None
        self.info = ""

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
            raise DeviceError("A port nem nyitható meg ({}): {}".format(port, exc))
        # A TinyUSB CDC-nek kell egy pillanat, mire kész a kapcsolat.
        time.sleep(0.4)
        self.ser.reset_input_buffer()
        self.info = self.command("PING", timeout=3.0)
        if not self.info.startswith("OK ZWIFT_BUTTONS"):
            self.close()
            raise DeviceError("Az eszköz nem Zwift Buttons konfigurációs firmware-t futtat.")
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

    # -- alacsony szint --

    def _write(self, cmd):
        if not self.connected:
            raise DeviceError("Nincs kapcsolat az eszközzel.")
        try:
            self.ser.reset_input_buffer()
            self.ser.write((cmd + "\n").encode("ascii"))
            self.ser.flush()
        except Exception as exc:
            raise DeviceError("Írási hiba: {}".format(exc))

    def _readline(self):
        try:
            raw = self.ser.readline()
        except Exception as exc:
            raise DeviceError("Olvasási hiba: {}".format(exc))
        return raw.decode("ascii", errors="replace").strip()

    def command(self, cmd, timeout=2.0):
        """Parancs küldése, amire egy OK/ERR sor a válasz.

        A firmware egyéb kiírásait (debug sorok) átugorja.
        """
        self._write(cmd)
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self._readline()
            if not line:
                continue
            if line.startswith("OK"):
                return line
            if line.startswith("ERR"):
                raise DeviceError("Az eszköz hibát jelzett: {} (parancs: {})".format(line, cmd))
        raise DeviceError("Időtúllépés, nincs válasz erre: {}".format(cmd))

    # -- magas szint --

    def read_keymap(self, timeout=6.0):
        self._write("GET")
        keymap = empty_keymap()
        seen = 0
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self._readline()
            if not line:
                continue
            if line == "END":
                if seen != NUM_MODES * NUM_BUTTONS * NUM_EVENTS:
                    raise DeviceError("Hiányos kiosztás érkezett ({} sor).".format(seen))
                return keymap
            if line.startswith("ERR"):
                raise DeviceError("Az eszköz hibát jelzett: {}".format(line))
            if not line.startswith("MAP "):
                continue
            parts = line.split()
            if len(parts) != 9:
                continue
            try:
                m, b, e, t, mod, code, rep, ms = (int(x) for x in parts[1:])
            except ValueError:
                continue
            if m >= NUM_MODES or b >= NUM_BUTTONS or e >= NUM_EVENTS:
                continue
            keymap[m][b][e] = Action(t, mod, code, rep, ms)
            seen += 1
        raise DeviceError("Időtúllépés a kiosztás beolvasása közben.")

    def write_keymap(self, keymap, progress=None):
        total = NUM_MODES * NUM_BUTTONS * NUM_EVENTS
        done = 0
        for m in range(NUM_MODES):
            for b in range(NUM_BUTTONS):
                for e in range(NUM_EVENTS):
                    a = keymap[m][b][e]
                    self.command("SET {} {} {} {} {} {} {} {}".format(
                        m, b, e, a.type, a.modifier, a.code,
                        1 if a.repeat else 0, a.repeat_ms))
                    done += 1
                    if progress is not None:
                        progress(done, total)

    def save_to_flash(self):
        return self.command("SAVE", timeout=5.0)

    def load_defaults(self):
        return self.command("DEFAULTS", timeout=3.0)

    def set_mode(self, mode):
        return self.command("MODE {}".format(mode), timeout=2.0)


# ---------------------------------------------------------------------------
# Művelet szerkesztő ablak
# ---------------------------------------------------------------------------

class ActionDialog(tk.Toplevel):
    """Modális ablak egy művelet beállításához, billentyű-felvétellel."""

    def __init__(self, master, title, action, allow_repeat):
        tk.Toplevel.__init__(self, master)
        self.title(title)
        self.resizable(False, False)
        self.transient(master)
        self.result = None
        self.allow_repeat = allow_repeat

        self._pressed_mods = set()

        self.type_var = tk.IntVar(value=action.type)
        self.key_var = tk.IntVar(value=action.code if action.type == ACT_KEY else 0)
        self.consumer_var = tk.IntVar(
            value=action.code if action.type == ACT_CONSUMER else CONSUMER_KEYS[0][0])
        self.mod_vars = {}
        for bit, name in MODIFIERS:
            self.mod_vars[bit] = tk.IntVar(
                value=1 if (action.type == ACT_KEY and action.modifier & bit) else 0)
        self.repeat_var = tk.IntVar(value=1 if action.repeat else 0)
        self.repeat_ms_var = tk.IntVar(value=action.repeat_ms or 60)

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
                            command=self._update_preview).grid(
                row=1 + i // 4, column=i % 4, sticky="w", padx=(0, 10))

        # Média billentyű
        self.consumer_box = ttk.LabelFrame(outer, text="Média billentyű", padding=8)
        self.consumer_box.pack(fill="x", pady=(8, 0))
        self.consumer_combo = ttk.Combobox(
            self.consumer_box, state="readonly", width=32,
            values=[name for _, name in CONSUMER_KEYS])
        self.consumer_combo.pack(fill="x")
        self.consumer_combo.bind("<<ComboboxSelected>>", self._on_consumer_combo)

        # Ismétlés (csak hosszú nyomásnál)
        if self.allow_repeat:
            self.repeat_box = ttk.LabelFrame(outer, text="Nyomva tartás", padding=8)
            self.repeat_box.pack(fill="x", pady=(8, 0))
            ttk.Checkbutton(self.repeat_box,
                            text="Ismétlés, amíg nyomva tartod",
                            variable=self.repeat_var,
                            command=self._refresh_state).grid(row=0, column=0, sticky="w")
            ttk.Label(self.repeat_box, text="Ismétlési idő (ms):").grid(
                row=1, column=0, sticky="w", pady=(4, 0))
            self.repeat_spin = ttk.Spinbox(self.repeat_box, from_=10, to=2000,
                                           increment=10, width=8,
                                           textvariable=self.repeat_ms_var,
                                           command=self._update_preview)
            self.repeat_spin.grid(row=1, column=1, sticky="w", padx=(8, 0), pady=(4, 0))
        else:
            self.repeat_box = None

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
            self.geometry("+{}+{}".format(max(x, 0), max(y, 0)))
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
            self.consumer_var.set(CONSUMER_KEYS[idx][0])
            self._refresh_state()

    # -- állapot --

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

        key_state = "normal" if atype == ACT_KEY else "disabled"
        self._set_widget_state(self.key_box, key_state)
        if atype == ACT_KEY:
            self.key_combo.configure(state="readonly")
            code = self.key_var.get()
            index = next((i for i, (c, _) in enumerate(KEY_CHOICES) if c == code), 0)
            self.key_combo.current(index)

        consumer_state = "normal" if atype == ACT_CONSUMER else "disabled"
        self._set_widget_state(self.consumer_box, consumer_state)
        if atype == ACT_CONSUMER:
            self.consumer_combo.configure(state="readonly")
            code = self.consumer_var.get()
            index = next((i for i, (c, _) in enumerate(CONSUMER_KEYS) if c == code), 0)
            self.consumer_combo.current(index)
            self.consumer_var.set(CONSUMER_KEYS[index][0])

        if self.repeat_box is not None:
            can_repeat = atype in (ACT_KEY, ACT_CONSUMER)
            if not can_repeat:
                self.repeat_var.set(0)
            self._set_widget_state(self.repeat_box, "normal" if can_repeat else "disabled")
            if can_repeat and not self.repeat_var.get():
                self.repeat_spin.configure(state="disabled")

        self._update_preview()

    def _update_preview(self):
        action = self._build_action()
        hint = ""
        if self.type_var.get() == ACT_KEY and self.focus_get() is self.capture:
            live = "+".join(name for bit, name in MODIFIERS if bit in self._pressed_mods)
            if live:
                hint = "   [lenyomva: {}+…]".format(live)
        self.preview.configure(text="Beállítás: " + action.label() + hint)

    def _build_action(self):
        atype = self.type_var.get()
        repeat = self.repeat_var.get() if self.repeat_box is not None else 0
        try:
            repeat_ms = int(self.repeat_ms_var.get())
        except (tk.TclError, ValueError):
            repeat_ms = 60
        repeat_ms = min(max(repeat_ms, 10), 2000)
        if atype == ACT_KEY:
            return Action(ACT_KEY, self._current_modifier(), self.key_var.get(),
                          repeat, repeat_ms)
        if atype == ACT_CONSUMER:
            return Action(ACT_CONSUMER, 0, self.consumer_var.get(), repeat, repeat_ms)
        return Action(atype, 0, 0, 0, repeat_ms)

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
# Főablak
# ---------------------------------------------------------------------------

class App(ttk.Frame):

    def __init__(self, master):
        ttk.Frame.__init__(self, master, padding=10)
        self.pack(fill="both", expand=True)
        self.master.title("Zwift Buttons – gomb-kiosztás konfiguráló")

        self.link = DeviceLink()
        self.keymap = empty_keymap()
        self.cells = {}
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

        ttk.Label(frame, text="Gomb", font=("TkDefaultFont", 9, "bold")).grid(
            row=0, column=0, padx=4, pady=(0, 6), sticky="w")
        for e in range(NUM_EVENTS):
            ttk.Label(frame, text=EVENT_NAMES[e], font=("TkDefaultFont", 9, "bold")).grid(
                row=0, column=e + 1, padx=4, pady=(0, 6))

        for b in range(NUM_BUTTONS):
            ttk.Label(frame, text="Gomb {}".format(b + 1)).grid(
                row=b + 1, column=0, padx=4, pady=3, sticky="w")
            for e in range(NUM_EVENTS):
                btn = ttk.Button(frame, text="—", width=26,
                                 command=lambda m=mode, bb=b, ee=e: self.edit_cell(m, bb, ee))
                btn.grid(row=b + 1, column=e + 1, padx=4, pady=3, sticky="we")
                self.cells[(mode, b, e)] = btn
        return frame

    # -- cellák --

    def _refresh_all_cells(self):
        for (m, b, e), btn in self.cells.items():
            btn.configure(text=self.keymap[m][b][e].label())

    def edit_cell(self, mode, button, event):
        title = "{} – Gomb {} – {}".format(MODE_NAMES[mode], button + 1, EVENT_NAMES[event])
        dialog = ActionDialog(self.master, title, self.keymap[mode][button][event],
                              allow_repeat=(event == EV_LONG))
        self.master.wait_window(dialog)
        if dialog.result is not None:
            self.keymap[mode][button][event] = dialog.result
            self.cells[(mode, button, event)].configure(text=dialog.result.label())
            self.dirty = True
            self._set_status(self.status.cget("text"))

    # -- kapcsolat --

    def refresh_ports(self):
        if list_ports is None:
            self.port_combo["values"] = []
            self._set_status("Hiányzik a pyserial csomag – telepítsd: pip install pyserial",
                             error=True)
            return
        ports = list(list_ports.comports())
        labels = ["{} – {}".format(p.device, p.description) for p in ports]
        self._ports = [p.device for p in ports]
        self.port_combo["values"] = labels
        if labels:
            # Ha van XIAO/nRF52/Adafruit jellegű port, azt válasszuk elsőként.
            preferred = 0
            for i, p in enumerate(ports):
                text = "{} {}".format(p.description, p.manufacturer or "").lower()
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
        self._set_status("Csatlakozva: {}  ({})".format(port, self.link.info))
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
            self.keymap = self.link.read_keymap()
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
            self.link.write_keymap(
                self.keymap,
                progress=lambda d, t: self._set_status(
                    "Küldés… {}/{}".format(d, t), update=True))
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
            self.keymap = self.link.read_keymap()
        except DeviceError as exc:
            messagebox.showerror("Hiba", str(exc))
            return
        finally:
            self._busy(False)
        self._refresh_all_cells()
        self.dirty = False
        self._set_status("Gyári kiosztás betöltve az eszközre (még nincs elmentve).")

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
        self._set_status("Elmentve: {}".format(path))

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
        if data.get("format") != FILE_FORMAT:
            messagebox.showerror("Ismeretlen fájl",
                                 "Ez nem Zwift Buttons kiosztás-fájl.")
            return
        try:
            keymap = empty_keymap()
            for m, mode in enumerate(data["modes"][:NUM_MODES]):
                for b, button in enumerate(mode["buttons"][:NUM_BUTTONS]):
                    for e in range(NUM_EVENTS):
                        entry = button.get(EVENT_KEYS[e])
                        if entry:
                            keymap[m][b][e] = Action.from_dict(entry)
        except (KeyError, TypeError, ValueError) as exc:
            messagebox.showerror("Hibás fájl", "A fájl tartalma hibás: {}".format(exc))
            return
        self.keymap = keymap
        self._refresh_all_cells()
        self.dirty = True
        self._set_status("Betöltve: {}".format(path))

    def _load_local_defaults(self):
        """Induláskor a repóban lévő alapértelmezett kiosztás betöltése (ha van)."""
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "default_keymap.json")
        if not os.path.exists(path):
            return
        try:
            with open(path, "r", encoding="utf-8") as fh:
                data = json.load(fh)
            for m, mode in enumerate(data["modes"][:NUM_MODES]):
                for b, button in enumerate(mode["buttons"][:NUM_BUTTONS]):
                    for e in range(NUM_EVENTS):
                        entry = button.get(EVENT_KEYS[e])
                        if entry:
                            self.keymap[m][b][e] = Action.from_dict(entry)
        except (OSError, ValueError, KeyError, TypeError):
            pass

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
        if self.dirty and self.link.connected:
            if not messagebox.askokcancel(
                    "Kilépés",
                    "Vannak el nem küldött módosítások. Biztosan kilépsz?"):
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
