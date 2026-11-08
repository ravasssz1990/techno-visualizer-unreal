@echo off
rem Builds the editor module, runs the content setup, and packages a Shipping build.
rem Usage: Scripts\Build.bat [editor|setup|package|all]   (default: all)
setlocal
if "%UE_ROOT%"=="" set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "PROJ=%~dp0..\TechnoViz.uproject"
set "OUT=%~dp0..\Packaged"
set "STEP=%~1"
if "%STEP%"=="" set "STEP=all"

if /I "%STEP%"=="editor" goto editor
if /I "%STEP%"=="setup" goto setup
if /I "%STEP%"=="package" goto package

:editor
echo === Building TechnoVizEditor
call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" TechnoVizEditor Win64 Development -Project="%PROJ%" -WaitMutex -FromMsBuild || goto fail
if /I "%STEP%"=="editor" goto done

:setup
echo === Creating materials and level
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJ%" -run=pythonscript -script="%~dp0..\Content\Python\techno_setup.py" -unattended -nop4 -nosplash || goto fail
if /I "%STEP%"=="setup" goto done

:package
echo === Packaging Shipping build to %OUT%
call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%PROJ%" -platform=Win64 -clientconfig=Shipping -build -cook -stage -pak -iostore -compressed -prereqs -archive -archivedirectory="%OUT%" -nop4 -utf8output -unattended || goto fail

:done
echo === OK
exit /b 0

:fail
echo === FAILED
exit /b 1
