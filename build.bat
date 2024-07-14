@echo off
setlocal
call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d "%~dp0"
if exist build_dir rd /s /q build_dir
md build_dir
cd build_dir
cmake -DCMAKE_BUILD_TYPE=Release -GNinja ..
cmake --build . --config Release --target all --parallel
copy /y gputester.exe ..\gputester.exe
cd ..
rd /s /q build_dir
endlocal
pause
exit /b 0