#!/usr/bin/env bash
# A teljes tesztkészlet lefuttatása. Használat: tools/tests/run_all.sh
#
# Szükséges: g++ (C++11), python3 tkinter modullal, és fejnélküli futtatáshoz
# xvfb-run (Debian/Ubuntu: sudo apt install g++ python3-tk xvfb).
set -u
cd "$(dirname "$0")"

# A firmware-t ugyanazzal a szabvánnyal fordítjuk, amit a Seeed/Adafruit nRF52
# board csomag használ (platform.txt: -std=gnu++11), hogy a teszt ne engedjen át
# olyan kódot, ami az eszközön nem fordulna le.
CXXFLAGS="-std=gnu++11 -Wall -Wextra -Istubs"
# A GUI-teszthez tkinter kell. Ha a PYTHON nincs megadva, megkeressük az első
# olyan python3-at, amiben van tkinter (több gépen ez nem az alapértelmezett).
PY="${PYTHON:-}"
if [ -z "$PY" ]; then
  for cand in python3 python3.13 python3.12 python3.11 python3.10 python; do
    if command -v "$cand" >/dev/null 2>&1 && "$cand" -c "import tkinter" >/dev/null 2>&1; then
      PY="$cand"; break
    fi
  done
fi
if [ -z "$PY" ]; then
  echo "Nem találtam tkinter-es python3-at (Debian/Ubuntu: sudo apt install python3-tk)."
  exit 1
fi
if command -v xvfb-run >/dev/null 2>&1; then GUI="xvfb-run -a $PY"; else GUI="$PY"; fi
echo "python: $($PY -V 2>&1)  ($PY)"

fail=0
run() {  # run <nev> <parancs...>
  printf '%-34s' "$1"
  shift
  if out=$("$@" 2>&1); then echo "OK"; else echo "HIBA"; echo "$out" | tail -20; fail=1; fi
}

echo "== fordítás =="
for src in firmware_test:firmware_test bridge:bridge single_conn:single_conn; do
  name="${src%%:*}"
  printf '%-34s' "g++ $name.cpp"
  if out=$(g++ $CXXFLAGS -o "$name" "$name.cpp" 2>&1); then echo "OK"; else
    echo "HIBA"; echo "$out" | head -20; fail=1
  fi
done

echo
echo "== tesztek =="
run "firmware (stub headerek ellen)" ./firmware_test
run "egykapcsolatos mód"             ./single_conn
run "GUI (Tkinter)"                  $GUI gui_test.py
run "integrációs (valódi firmware)"  $GUI integration_test.py
run "kapcsolatkezelés"               $GUI conn_test.py
run "protokoll"                      $GUI proto_test.py

echo
if [ "$fail" -eq 0 ]; then echo "MINDEN TESZT SIKERES"; else echo "VOLT HIBA"; fi
exit $fail
