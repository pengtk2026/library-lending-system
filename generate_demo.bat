@echo off
chcp 65001 >nul
rem Rebuild the large demonstration dataset: 1000 books / 100 students / 500 loans
setlocal
set "ROOT=%~dp0"
cd /d "%ROOT%"
if not exist "%ROOT%data" mkdir "%ROOT%data"

echo [1/2] Generating demo dataset ...
"%ROOT%build\cmake\DemoDataTool.exe"
if errorlevel 1 goto :fail

echo.
echo [2/2] Report: data\demo_data_report.txt
type "%ROOT%data\demo_data_report.txt"
echo.
echo Done. Run build\cmake\LibraryApp.exe to browse the data.
goto :eof

:fail
echo.
echo Generation FAILED. See data\demo_data_report.txt for details.
exit /b 1
