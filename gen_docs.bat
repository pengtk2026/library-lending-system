@echo off
rem Generate Chinese API documentation with Doxygen + Graphviz.
rem If the tools are installed elsewhere, edit the two paths below.
rem If doxygen and dot are already on your system PATH, the defaults just work.
setlocal
set "ROOT=%~dp0"
set "DOXYGEN=D:\tools\Doxygen\doxygen.exe"
set "DOTDIR=D:\tools\Graphviz\Graphviz-16.1.1-win64\bin"

cd /d "%ROOT%"
if not exist "%DOXYGEN%" set "DOXYGEN=doxygen"
if exist "%DOTDIR%" set "PATH=%DOTDIR%;%PATH%"

echo [1/1] Generating API documentation ...
"%DOXYGEN%" Doxyfile
if errorlevel 1 goto :fail
echo.
echo Done: %ROOT%build\doxygen\html\index.html
echo Note: Doxygen may print source-comment warnings; the HTML is still generated.
goto :eof

:fail
echo.
echo Documentation generation FAILED. Is Doxygen installed and is dot.exe on PATH?
exit /b 1
