@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
if not exist dist mkdir dist
windres -I assets assets/app.rc -O coff -o build/app.res.o
if errorlevel 1 exit /b 1
g++ -std=c++17 -Os -Wall -Wextra -Werror -municode -mwindows -static -ffunction-sections -fdata-sections src/main.cpp build/app.res.o -o dist/ImageCompare.exe -Wl,--gc-sections -s -lgdiplus -lole32 -lshell32 -luuid -ldwmapi -lcomctl32 -lgdi32
if errorlevel 1 exit /b 1
echo Ready: dist\ImageCompare.exe
