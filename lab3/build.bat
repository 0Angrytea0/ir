@echo off
setlocal

if not exist bin mkdir bin
if not exist out mkdir out
if not exist out\tokens mkdir out\tokens

g++ -O2 -std=c++17 -Wall -Wextra ^
  src\main.cpp src\utf8.cpp src\win_files.cpp src\tokenize.cpp ^
  -o bin\tokenize.exe

if errorlevel 1 (
  echo Build failed.
  exit /b 1
)

echo Build OK: bin\tokenize.exe
endlocal
