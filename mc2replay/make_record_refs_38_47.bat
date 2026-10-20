@echo off
setlocal

rem === Nastaveni ===
set "WORKDIR=C:\prenos\dosbox-x-remc2\mc2replay"
set "LOG=%WORKDIR%\refs_38_47.txt"
set "LEVELS=38 39 40 41 42 43 44 45 46 47"
set "JOBS=5"
rem Pokud se Python nenajde sam, odkomentuj a dopln plnou cestu:
rem set "PYTHON_EXE=C:\Users\vesely\AppData\Local\Programs\Python\Python312\python.exe"

rem === Nalezeni Pythonu ===
if not defined PYTHON_EXE where python >nul 2>&1 && set "PYTHON_EXE=python"
if not defined PYTHON_EXE where py >nul 2>&1 && set "PYTHON_EXE=py"
if not defined PYTHON_EXE for /d %%D in ("%LOCALAPPDATA%\Programs\Python\Python3*") do if exist "%%D\python.exe" set "PYTHON_EXE=%%D\python.exe"
if not defined PYTHON_EXE for /d %%D in ("C:\Python3*") do if exist "%%D\python.exe" set "PYTHON_EXE=%%D\python.exe"
if not defined PYTHON_EXE goto :nopython
echo Pouzivam: %PYTHON_EXE%

cd /d "%WORKDIR%"
if errorlevel 1 goto :nodir

rem === Kontrola syntaxe skriptu ===
"%PYTHON_EXE%" -c "import ast;ast.parse(open('make_record_refs.py').read());print('syntax ok')"
if errorlevel 1 goto :badsyntax

rem === Spusteni generovani na pozadi, vystup do logu ===
echo Spoustim generovani pro levely: %LEVELS% - jobs=%JOBS%
start "make_record_refs" /min cmd /c ""%PYTHON_EXE%" make_record_refs.py %LEVELS% --jobs %JOBS% > "%LOG%" 2>&1"

timeout /t 2 /nobreak >nul

rem === Kontrola, ze bezi dosbox-x ===
echo Pocet bezicich procesu dosbox-x:
tasklist | find /c /i "dosbox-x"

echo.
echo Log: %LOG%
pause
exit /b 0

:nopython
echo Python nenalezen. Odkomentuj a dopln PYTHON_EXE na zacatku souboru.
echo Cestu zjistis v Git Bashi prikazem: which python
pause
exit /b 1

:nodir
echo Nelze prejit do %WORKDIR%
pause
exit /b 1

:badsyntax
echo Kontrola syntaxe selhala. Bud se Python nespustil, nebo je chyba v make_record_refs.py
pause
exit /b 1
