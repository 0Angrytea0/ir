@echo off
setlocal

if not exist bin mkdir bin
if not exist out mkdir out

g++ -O2 -std=c++17 -Wall -Wextra ^
  src\main.cpp src\win_files.cpp src\freq.cpp ^
  -o bin\zipf.exe

if errorlevel 1 (
  echo Build failed.
  exit /b 1
)

echo Build OK: bin\zipf.exe
endlocal
