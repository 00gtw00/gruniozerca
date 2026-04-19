@echo off
REM =============================================================================
REM Gruniożerca DOS — Skrypt konfiguracji środowiska (Windows)
REM Instaluje DJGPP przez MSYS2/pacman lub instruuje jak zainstalować ręcznie
REM =============================================================================

echo === Konfiguracja środowiska DJGPP dla Windows ===
echo.

REM Sprawdź czy MSYS2 jest dostępny
where pacman >nul 2>&1
if %errorlevel% equ 0 goto :msys2_install

where winget >nul 2>&1
if %errorlevel% equ 0 goto :winget_install

goto :manual_install

:msys2_install
echo [MSYS2 znalezione] Instalacja przez pacman...
pacman -S --noconfirm mingw-w64-i686-cross-gcc
echo.
echo Dodaj do PATH: %MSYS2_ROOT%\mingw64\bin
goto :done

:winget_install
echo [winget znalezione] Instalacja MSYS2...
winget install MSYS2.MSYS2
echo.
echo Po instalacji MSYS2 uruchom to polecenie w terminalu MSYS2:
echo   pacman -S mingw-w64-i686-cross-gcc make
echo.
goto :done

:manual_install
echo Automatyczna instalacja niemożliwa.
echo.
echo === Instrukcja ręczna ===
echo.
echo OPCJA 1 — MSYS2 (zalecana):
echo   1. Pobierz MSYS2 z: https://www.msys2.org/
echo   2. Zainstaluj i uruchom terminal MSYS2
echo   3. Wpisz: pacman -S mingw-w64-i686-cross-gcc make
echo   4. Dodaj do PATH systemu: C:\msys64\mingw64\bin
echo.
echo OPCJA 2 — Gotowe pakiety DJGPP dla Windows:
echo   1. Pobierz z: https://github.com/andrewwutw/build-djgpp/releases
echo   2. Rozpakuj do C:\djgpp
echo   3. Dodaj C:\djgpp\bin do zmiennej PATH
echo.
echo OPCJA 3 — WSL2 (Windows Subsystem for Linux):
echo   1. Włącz WSL2: wsl --install
echo   2. W WSL: uruchom setup_env.sh
echo.

:done
echo.
echo Weryfikacja instalacji:
echo   i686-pc-msdosdjgpp-gcc --version
echo.
echo Kompilacja gry:
echo   cd dos
echo   make all
echo.
echo Opcjonalnie zainstaluj DOSBox-X do testowania:
echo   https://dosbox-x.com/
echo.
pause
