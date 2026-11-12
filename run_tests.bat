@echo off
rem Build and run the domain / DAO / service smoke tests (TC-01 ~ TC-12)
setlocal
set "ROOT=%~dp0"
set "TOOLCHAIN=%ROOT%tools\msys2root\mingw64\bin"
set "PATH=%TOOLCHAIN%;%PATH%"
set "SQLITE_DIR=%ROOT%tools\sqlite-amalgamation-3540100"
if not exist "%ROOT%build\obj" mkdir "%ROOT%build\obj"

"%TOOLCHAIN%\gcc.exe" -O1 -c "%SQLITE_DIR%\sqlite3.c" -o "%ROOT%build\obj\sqlite3.o"
if errorlevel 1 exit /b 1

"%TOOLCHAIN%\g++.exe" -std=c++17 -O1 -static ^
  -I"%ROOT%include" -I"%SQLITE_DIR%" ^
  "%ROOT%tests\smoke_test.cpp" ^
  "%ROOT%src\domain.cpp" "%ROOT%src\database.cpp" ^
  "%ROOT%src\daos.cpp" "%ROOT%src\service.cpp" ^
  "%ROOT%build\obj\sqlite3.o" ^
  -o "%ROOT%build\smoke_test.exe"
if errorlevel 1 exit /b 1

"%ROOT%build\smoke_test.exe"
exit /b %errorlevel%
