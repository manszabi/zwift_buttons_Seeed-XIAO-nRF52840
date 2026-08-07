@echo off
setlocal EnableExtensions
title Zwift Buttons - konfiguralo program

rem ---------------------------------------------------------------------------
rem  Zwift Buttons - konfiguralo program inditasa Windows alatt.
rem
rem  Ellenorzi a Python telepitest es a szukseges csomagokat, a hianyzo
rem  csomagokat telepiti, majd elinditja a grafikus feluletet.
rem
rem  Megjegyzes: ez a fajl szandekosan ekezet nelkuli, mert a cmd.exe a
rem  parancsfajlokat a rendszer kodlapjaval olvassa, es az ekezetes karakterek
rem  elronthatjak a vegrehajtast.
rem ---------------------------------------------------------------------------

cd /d "%~dp0"

echo ============================================================
echo   Zwift Buttons - gomb-kiosztas konfigurala
echo ============================================================
echo.

rem --- 1. Python kereses (eloszor a "py" launcher, aztan a "python") ---------
set "PY="
py -3 -c "import sys" >nul 2>&1 && set "PY=py -3"
if not defined PY (
    python -c "import sys" >nul 2>&1 && set "PY=python"
)
if not defined PY (
    echo [HIBA] Nem talalhato Python a rendszeren.
    echo.
    echo  Telepitsd a Python 3-at innen: https://www.python.org/downloads/
    echo  A telepitonel pipald be az "Add python.exe to PATH" opciot,
    echo  valamint a "tcl/tk and IDLE" komponenst.
    goto :error
)

set "PYVER=ismeretlen"
for /f "delims=" %%v in ('%PY% -c "import sys;print(sys.version.split()[0])" 2^>nul') do set "PYVER=%%v"
echo [OK]   Python megtalalva: %PY%  (verzio: %PYVER%)

rem --- 2. Verzio ellenorzes (3.7 vagy ujabb kell) ----------------------------
%PY% -c "import sys;sys.exit(0 if sys.version_info>=(3,7) else 1)" >nul 2>&1
if errorlevel 1 (
    echo [HIBA] Tul regi Python verzio: %PYVER%
    echo        Legalabb Python 3.7 szukseges.
    goto :error
)

rem --- 3. tkinter ellenorzes (nem telepitheto pip-pel) -----------------------
%PY% -c "import tkinter" >nul 2>&1
if errorlevel 1 (
    echo [HIBA] A tkinter modul hianyzik a Python telepitesbol.
    echo.
    echo  Inditsd ujra a Python telepitot ^(Modify^), es pipald be a
    echo  "tcl/tk and IDLE" komponenst, vagy telepitsd ujra a Pythont
    echo  a https://www.python.org/downloads/ oldalrol.
    goto :error
)
echo [OK]   tkinter elerheto

rem --- 4. pyserial ellenorzes, szukseg eseten telepites ----------------------
%PY% -c "import serial" >nul 2>&1
if not errorlevel 1 (
    echo [OK]   pyserial elerheto
    goto :run
)

echo [..]   A pyserial csomag hianyzik, telepites...
echo.

%PY% -m pip --version >nul 2>&1
if errorlevel 1 (
    echo [..]   A pip hianyzik, telepitese...
    %PY% -m ensurepip --upgrade >nul 2>&1
    %PY% -m pip --version >nul 2>&1
    if errorlevel 1 (
        echo [HIBA] A pip nem erheto el, a csomag nem telepitheto.
        echo        Probald kezzel:  %PY% -m pip install pyserial
        goto :error
    )
)

%PY% -m pip install --disable-pip-version-check -r requirements.txt
if errorlevel 1 (
    echo.
    echo [..]   Nem sikerult, ujraprobalkozas felhasznaloi modban ^(--user^)...
    echo.
    %PY% -m pip install --disable-pip-version-check --user -r requirements.txt
)

%PY% -c "import serial" >nul 2>&1
if errorlevel 1 (
    echo.
    echo [HIBA] A pyserial telepitese nem sikerult.
    echo        Probald kezzel, rendszergazdakent:
    echo            %PY% -m pip install pyserial
    goto :error
)
echo.
echo [OK]   pyserial telepitve

:run
echo.
echo Program inditasa...
echo ^(Ez az ablak nyitva marad; ide irja ki a program a hibauzeneteket.^)
echo.
%PY% zwift_config_gui.py
if errorlevel 1 (
    echo.
    echo [HIBA] A program hibaval allt le. A reszletek fentebb lathatok.
    goto :error
)

endlocal
exit /b 0

:error
echo.
echo ------------------------------------------------------------
pause
endlocal
exit /b 1
