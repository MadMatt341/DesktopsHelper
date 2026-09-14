@echo off
setlocal
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" exit /b 1
for /f "usebackq delims=" %%I in (`"%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "nativeVisualStudio=%%I"
if not defined nativeVisualStudio exit /b 1
call "%nativeVisualStudio%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
set "prototypeOutput=taskbar-left"
if not "%~1"=="" set "prototypeOutput=%~1"
echo(%prototypeOutput%| findstr /r /x "[A-Za-z0-9_-][A-Za-z0-9_-]*" >nul
if errorlevel 1 exit /b 1
if not exist "build\%prototypeOutput%" mkdir "build\%prototypeOutput%"
cd "build\%prototypeOutput%"
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /DUNICODE /D_UNICODE /LD ..\..\src\taskbar-prototype\component.cpp /Fe:DesktopsHelper.TaskbarV4.dll /link /DEF:..\..\src\taskbar-prototype\component.def windowsapp.lib ole32.lib oleaut32.lib user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /DUNICODE /D_UNICODE ..\..\src\taskbar-prototype\controller.cpp /Fe:TaskbarPrototype.exe /link user32.lib shlwapi.lib gdi32.lib
if errorlevel 1 exit /b 1
if /i not "%~2"=="tests" exit /b 0
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT ..\..\src\taskbar-prototype\symbols.cpp /Fe:TaskbarSymbols.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT ..\..\tests\native-taskbar.cpp /Fe:NativeTaskbarTest.exe /link user32.lib ole32.lib oleaut32.lib uiautomationcore.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /DUNICODE /D_UNICODE /DDH_TASKBAR_TESTING /LD ..\..\src\taskbar-prototype\component.cpp /Fe:DesktopsHelper.TaskbarV4.Tests.dll /link /DEF:..\..\src\taskbar-prototype\component.def windowsapp.lib ole32.lib oleaut32.lib user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /DUNICODE /D_UNICODE /DDH_TASKBAR_TESTING ..\..\src\taskbar-prototype\controller.cpp /Fe:TaskbarTestController.exe /link user32.lib shlwapi.lib gdi32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /DUNICODE /D_UNICODE /DDH_TASKBAR_TESTING ..\..\tests\native-taskbar-lifecycle.cpp /Fe:NativeTaskbarLifecycle.exe /link user32.lib
exit /b %errorlevel%
