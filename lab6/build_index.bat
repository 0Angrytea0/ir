@echo off
setlocal

if not exist bin mkdir bin
if not exist index mkdir index

g++ -O2 -std=c++17 -Wall -Wextra ^
  src\indexer.cpp src\win_files.cpp ^
  -o bin\indexer.exe

if errorlevel 1 (
  echo Build failed.
  exit /b 1
)

echo Build OK: bin\indexer.exe
endlocal
