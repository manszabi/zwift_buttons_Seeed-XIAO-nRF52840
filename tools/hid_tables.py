"""HID billentyűkódok és Tkinter keysym -> HID leképezés.

A kódok a USB HID Usage Tables (Keyboard/Keypad page 0x07, Consumer page 0x0C)
szerintiek, ugyanazok, amiket az Adafruit TinyUSB `HID_KEY_*` és
`HID_USAGE_CONSUMER_*` makrói használnak.
"""

# --- Művelet-típusok (egyeznek a firmware ZwActionType enumjával) ---
ACT_NONE = 0
ACT_KEY = 1
ACT_CONSUMER = 2
ACT_MODE_NEXT = 3
ACT_VIEW_CYCLE = 4

# --- Események (egyeznek a firmware ZwEvent enumjával) ---
EV_CLICK = 0
EV_DOUBLE = 1
EV_LONG = 2

EVENT_NAMES = ["Rövid nyomás", "Dupla kattintás", "Hosszú nyomás"]
EVENT_KEYS = ["click", "double", "long"]
MODE_NAMES = ["Normál (Zwift)", "Verseny / Edzés", "Média vezérlő"]

# --- Cél-eszköz fiókok (egyeznek a firmware ZW_SLOT_* / ZW_TARGET_* értékeivel) ---
SLOT_PC = 0
SLOT_PHONE = 1
SLOT_NAMES = ["Windows PC", "Telefon"]

TARGET_PC = 1 << SLOT_PC       # 0x01
TARGET_PHONE = 1 << SLOT_PHONE  # 0x02
TARGET_ALL = TARGET_PC | TARGET_PHONE

TARGET_CHOICES = [
    (TARGET_PC, "Csak a Windows PC-re"),
    (TARGET_PHONE, "Csak a telefonra"),
    (TARGET_ALL, "Mindkét eszközre"),
]

# A firmware gyári beállítása: a Zwift vezérlés a PC-re, a média mindkettőre.
DEFAULT_TARGETS = [TARGET_PC, TARGET_PC, TARGET_ALL]


def target_label(mask):
    for value, name in TARGET_CHOICES:
        if value == mask:
            return name
    return "Ismeretlen (0x{:02X})".format(mask)

# --- Módosító bitek (KEYBOARD_MODIFIER_*) ---
MOD_LCTRL = 0x01
MOD_LSHIFT = 0x02
MOD_LALT = 0x04
MOD_LGUI = 0x08
MOD_RCTRL = 0x10
MOD_RSHIFT = 0x20
MOD_RALT = 0x40
MOD_RGUI = 0x80

# Sorrend számít: a felirat ebben a sorrendben áll össze.
MODIFIERS = [
    (MOD_LCTRL, "Ctrl"),
    (MOD_LSHIFT, "Shift"),
    (MOD_LALT, "Alt"),
    (MOD_LGUI, "Win"),
    (MOD_RCTRL, "R-Ctrl"),
    (MOD_RSHIFT, "R-Shift"),
    (MOD_RALT, "AltGr"),
    (MOD_RGUI, "R-Win"),
]


def _build_key_names():
    names = {0x00: "(nincs)"}
    for i in range(26):
        names[0x04 + i] = chr(ord("A") + i)
    for i in range(9):
        names[0x1E + i] = str(i + 1)
    names[0x27] = "0"
    names.update({
        0x28: "Enter",
        0x29: "Esc",
        0x2A: "Backspace",
        0x2B: "Tab",
        0x2C: "Szóköz",
        0x2D: "- (kötőjel)",
        0x2E: "= (egyenlő)",
        0x2F: "[",
        0x30: "]",
        0x31: "\\",
        0x32: "# (Europe1)",
        0x33: "; (pontosvessző)",
        0x34: "' (aposztróf)",
        0x35: "` (backtick)",
        0x36: ", (vessző)",
        0x37: ". (pont)",
        0x38: "/ (per)",
        0x39: "Caps Lock",
    })
    for i in range(12):
        names[0x3A + i] = "F{}".format(i + 1)
    names.update({
        0x46: "Print Screen",
        0x47: "Scroll Lock",
        0x48: "Pause",
        0x49: "Insert",
        0x4A: "Home",
        0x4B: "Page Up",
        0x4C: "Delete",
        0x4D: "End",
        0x4E: "Page Down",
        0x4F: "Jobbra nyíl",
        0x50: "Balra nyíl",
        0x51: "Le nyíl",
        0x52: "Fel nyíl",
        0x53: "Num Lock",
        0x54: "Numpad /",
        0x55: "Numpad *",
        0x56: "Numpad -",
        0x57: "Numpad +",
        0x58: "Numpad Enter",
    })
    for i in range(9):
        names[0x59 + i] = "Numpad {}".format(i + 1)
    names.update({
        0x62: "Numpad 0",
        0x63: "Numpad .",
        0x65: "Menü (Application)",
    })
    for i in range(12):
        names[0x68 + i] = "F{}".format(i + 13)
    return names


# HID keycode -> emberi olvasható név
KEY_NAMES = _build_key_names()

# A legördülő listában használt sorrend
KEY_CHOICES = sorted(KEY_NAMES.items(), key=lambda kv: kv[0])


def _build_keysym_map():
    m = {}
    for i in range(26):
        ch = chr(ord("a") + i)
        m[ch] = 0x04 + i
        m[ch.upper()] = 0x04 + i
    for i in range(9):
        m[str(i + 1)] = 0x1E + i
    m["0"] = 0x27
    m.update({
        "Return": 0x28,
        "Escape": 0x29,
        "BackSpace": 0x2A,
        "Tab": 0x2B,
        "ISO_Left_Tab": 0x2B,
        "space": 0x2C,
        "minus": 0x2D,
        "equal": 0x2E,
        "bracketleft": 0x2F,
        "bracketright": 0x30,
        "backslash": 0x31,
        "semicolon": 0x33,
        "apostrophe": 0x34,
        "grave": 0x35,
        "comma": 0x36,
        "period": 0x37,
        "slash": 0x38,
        "Caps_Lock": 0x39,
        # Shift-tel elért jelek: a fizikai billentyűre képezzük le
        "exclam": 0x1E, "at": 0x1F, "numbersign": 0x20, "dollar": 0x21,
        "percent": 0x22, "asciicircum": 0x23, "ampersand": 0x24,
        "asterisk": 0x25, "parenleft": 0x26, "parenright": 0x27,
        "underscore": 0x2D, "plus": 0x2E, "braceleft": 0x2F,
        "braceright": 0x30, "bar": 0x31, "colon": 0x33, "quotedbl": 0x34,
        "asciitilde": 0x35, "less": 0x36, "greater": 0x37, "question": 0x38,
        # Navigáció / vezérlés
        "Print": 0x46, "Sys_Req": 0x46, "Scroll_Lock": 0x47, "Pause": 0x48,
        "Insert": 0x49, "Home": 0x4A, "Prior": 0x4B, "Delete": 0x4C,
        "End": 0x4D, "Next": 0x4E,
        "Right": 0x4F, "Left": 0x50, "Down": 0x51, "Up": 0x52,
        "Num_Lock": 0x53,
        "KP_Divide": 0x54, "KP_Multiply": 0x55, "KP_Subtract": 0x56,
        "KP_Add": 0x57, "KP_Enter": 0x58, "KP_Decimal": 0x63,
        "KP_Delete": 0x63, "KP_Separator": 0x63,
        "Menu": 0x65, "App": 0x65,
    })
    for i in range(24):
        m["F{}".format(i + 1)] = (0x3A + i) if i < 12 else (0x68 + i - 12)
    for i in range(9):
        m["KP_{}".format(i + 1)] = 0x59 + i
    m["KP_0"] = 0x62
    # Numpad Num Lock nélkül (X11 nevek)
    m.update({
        "KP_End": 0x59, "KP_Down": 0x5A, "KP_Next": 0x5B, "KP_Left": 0x5C,
        "KP_Begin": 0x5D, "KP_Right": 0x5E, "KP_Home": 0x5F, "KP_Up": 0x60,
        "KP_Prior": 0x61, "KP_Insert": 0x62,
    })
    return m


KEYSYM_TO_HID = _build_keysym_map()

# Módosító billentyűk keysym -> módosító bit
MODIFIER_KEYSYMS = {
    "Control_L": MOD_LCTRL,
    "Control_R": MOD_RCTRL,
    "Shift_L": MOD_LSHIFT,
    "Shift_R": MOD_RSHIFT,
    "Alt_L": MOD_LALT,
    "Alt_R": MOD_RALT,
    "Meta_L": MOD_LALT,
    "Meta_R": MOD_RALT,
    "ISO_Level3_Shift": MOD_RALT,
    "Super_L": MOD_LGUI,
    "Super_R": MOD_RGUI,
    "Win_L": MOD_LGUI,
    "Win_R": MOD_RGUI,
}

# --- Média (consumer) usage kódok ---
CONSUMER_KEYS = [
    (0x00CD, "Lejátszás / Szünet"),
    (0x00B0, "Lejátszás"),
    (0x00B1, "Szünet"),
    (0x00B7, "Stop"),
    (0x00B5, "Következő szám"),
    (0x00B6, "Előző szám"),
    (0x00B3, "Gyors előre"),
    (0x00B4, "Visszatekerés"),
    (0x00E2, "Némítás"),
    (0x00E9, "Hangerő +"),
    (0x00EA, "Hangerő -"),
    (0x006F, "Fényerő +"),
    (0x0070, "Fényerő -"),
    (0x0183, "Médialejátszó indítása"),
    (0x018A, "E-mail"),
    (0x0192, "Számológép"),
    (0x0194, "Fájlkezelő"),
    (0x0221, "Keresés"),
    (0x0223, "Böngésző: Kezdőlap"),
    (0x0224, "Böngésző: Vissza"),
    (0x0225, "Böngésző: Előre"),
    (0x0226, "Böngésző: Leállítás"),
    (0x0227, "Böngésző: Frissítés"),
    (0x022A, "Könyvjelzők"),
]

CONSUMER_NAMES = dict(CONSUMER_KEYS)


def modifier_label(modifier):
    """0x0C -> 'Alt+Win'"""
    return "+".join(name for bit, name in MODIFIERS if modifier & bit)


def key_label(modifier, code):
    """Billentyű-kombináció felirata, pl. 'Ctrl+Alt+R'."""
    name = KEY_NAMES.get(code, "0x{:02X}".format(code))
    mods = modifier_label(modifier)
    return (mods + "+" + name) if mods else name


def consumer_label(code):
    return CONSUMER_NAMES.get(code, "Média 0x{:04X}".format(code))


def keysym_to_hid(keysym):
    """Tkinter keysym -> HID keycode, vagy None ha nem ismert."""
    return KEYSYM_TO_HID.get(keysym)
