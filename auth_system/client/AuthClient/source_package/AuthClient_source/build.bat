@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "OUTPUT=AuthClient.exe"
set "LLVM_ROOT=E:\App\llvm-mingw-64"
set "STATIC_LIB_DIR=%LLVM_ROOT%\i686-w64-mingw32\lib"
set "DEFAULT_SERVER_URL=http://159.75.226.54:8080"
set "MANIFEST_OBJ=AuthClient_manifest.res.o"
set "DIST_DIR=%SCRIPT_DIR%dist\AuthClient"

pushd "%SCRIPT_DIR%"

where clang++ >nul 2>nul
if errorlevel 1 (
  echo clang++ was not found in PATH.
  popd
  exit /b 1
)

where windres >nul 2>nul
if errorlevel 1 (
  echo windres was not found in PATH.
  popd
  exit /b 1
)

if not exist "%STATIC_LIB_DIR%\libc++.a" (
  echo Missing static libc++ library: %STATIC_LIB_DIR%\libc++.a
  popd
  exit /b 1
)

if not exist "%STATIC_LIB_DIR%\libc++abi.a" (
  echo Missing static libc++abi library: %STATIC_LIB_DIR%\libc++abi.a
  popd
  exit /b 1
)

if not exist "%STATIC_LIB_DIR%\libunwind.a" (
  echo Missing static libunwind library: %STATIC_LIB_DIR%\libunwind.a
  popd
  exit /b 1
)

windres --target=pe-i386 -O coff -i "AuthClient_manifest.rc" -o "%MANIFEST_OBJ%"
if errorlevel 1 (
  popd
  exit /b 1
)

clang++ -target i686-w64-windows-gnu -std=c++17 -O2 -DNDEBUG ^
  -fstack-protector-strong -ffunction-sections -fdata-sections -mguard=cf ^
  -municode -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -nostdlib++ ^
  -DAUTH_CLIENT_DEFAULT_SERVER_URL=L\"%DEFAULT_SERVER_URL%\" ^
  src\main.cpp ^
  %MANIFEST_OBJ% ^
  -o %OUTPUT% ^
  -Wl,--subsystem=windows,--dynamicbase,--nxcompat,--guard-cf,--gc-sections,--icf=all,--strip-all ^
  -L%STATIC_LIB_DIR% ^
  -Wl,-Bstatic -lc++ -lc++abi -lunwind -Wl,-Bdynamic ^
  -lwinhttp -lcrypt32 -lbcrypt -lcomctl32 -lgdi32 -luser32
if errorlevel 1 (
  popd
  exit /b 1
)

where llvm-strip >nul 2>nul
if %errorlevel%==0 llvm-strip --strip-all "%OUTPUT%" >nul 2>nul

if exist "%MANIFEST_OBJ%" del /q "%MANIFEST_OBJ%" >nul 2>nul

if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
mkdir "%DIST_DIR%" >nul
copy /Y "%OUTPUT%" "%DIST_DIR%\" >nul
if exist "auth_client.ini" copy /Y "auth_client.ini" "%DIST_DIR%\" >nul
if exist "auth_client.ini.example" copy /Y "auth_client.ini.example" "%DIST_DIR%\" >nul

popd
echo Build finished: %SCRIPT_DIR%%OUTPUT% [x86 via clang, standalone=yes]
echo Portable package: %DIST_DIR%
