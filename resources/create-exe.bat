@echo off

set cwd=%cd%
cd /D %~dp0

set DevCmd="C:\Program Files (x86)\Microsoft Visual Studio 14.0\Common7\Tools\VsDevCmd.bat"
set MSBuildOptions=/v:m /p:Configuration=Release
set BuildDir64="build-exe-64"
set BuildDir32="build-exe-32"

call %DevCmd%

echo Building 64-bit hdrview...
mkdir %BuildDir64%
cd %BuildDir64%
cmake -G "Visual Studio 15 2017 Win64" ..\..
msbuild %MSBuildOptions% hdrview.sln
move "Release\HDRView.exe" "..\..\HDRView.exe"
cd ..
rmdir /S /Q %BuildDir64%

echo Building 32-bit hdrview...
mkdir %BuildDir32%
cd %BuildDir32%
cmake -G "Visual Studio 15 2017" ..\..
msbuild %MSBuildOptions% hdrview.sln
move "Release\HDRView.exe" "..\..\HDRView-32bit.exe"
cd ..
rmdir /S /Q %BuildDir32%

echo Returning to original directory.
cd /D %cwd%
pause