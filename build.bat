@echo off
rem One-click build: portable MinGW-w64 + CMake + FLTK + SQLite
setlocal
set "ROOT=%~dp0"
set "TOOLCHAIN=%ROOT%tools\msys2root\mingw64\bin"
set "CMAKE_BIN=%ROOT%tools\cmake-3.31.6-windows-x86_64\bin"
if exist "%TOOLCHAIN%\g++.exe" (
  set "PATH=%TOOLCHAIN%;%CMAKE_BIN%;%PATH%"
  set "CC=%TOOLCHAIN%\gcc.exe"
  set "CXX=%TOOLCHAIN%\g++.exe"
) else (
  echo [info] portable toolchain not found; using gcc/g++/cmake/ninja from system PATH
  set "CC=gcc"
  set "CXX=g++"
)
set "FLTK_SRC=%ROOT%tools\fltk-1.4.5"
set "FLTK_INSTALL=%ROOT%tools\fltk-install"

if not exist "%FLTK_INSTALL%\CMake\FLTKConfig.cmake" (
  echo [1/4] Configuring static FLTK ...
  cmake -S "%FLTK_SRC%" -B "%ROOT%build\fltk" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER="%CC%" -DCMAKE_CXX_COMPILER="%CXX%" ^
    -DCMAKE_INSTALL_PREFIX="%FLTK_INSTALL%" ^
    -DFLTK_BUILD_SHARED_LIBS=OFF -DFLTK_BUILD_TEST=OFF ^
    -DFLTK_BUILD_EXAMPLES=OFF -DFLTK_BUILD_FLUID=OFF ^
    -DFLTK_BUILD_GL=OFF -DFLTK_BUILD_FORMS=OFF
  if errorlevel 1 goto :fail
  echo [2/4] Building and installing FLTK ...
  cmake --build "%ROOT%build\fltk" --target install
  if errorlevel 1 goto :fail
)

echo [3/4] Configuring LibraryApp ...
cmake -S "%ROOT%." -B "%ROOT%build\cmake" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER="%CC%" -DCMAKE_CXX_COMPILER="%CXX%" ^
  -DFLTK_DIR="%FLTK_INSTALL%\CMake"
if errorlevel 1 goto :fail

echo [4/4] Building LibraryApp ...
cmake --build "%ROOT%build\cmake"
if errorlevel 1 goto :fail

if not exist "%ROOT%data" mkdir "%ROOT%data"
echo.
echo Build finished: %ROOT%build\cmake\LibraryApp.exe
echo Run it from this project folder so data\library.db is created here.
goto :eof

:fail
echo.
echo Build FAILED. See the messages above.
exit /b 1
