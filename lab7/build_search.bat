@echo off
setlocal

if not exist bin mkdir bin

g++ -O2 -std=c++17 -Wall -Wextra ^
  src\search.cpp src\utf8.cpp src\stem_ru.cpp ^
  -o bin\search.exe

if errorlevel 1 (
  echo Build failed.
  exit /b 1
)

echo Build OK: bin\search.exe
endlocal
