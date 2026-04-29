# AuthClient Build And Run

## Overview

This directory contains the Win32 C++ authorization client source code.

Current default build behavior:

- Target architecture: `x86`
- Compiler toolchain: `clang++` from `llvm-mingw`
- Output style: standalone single-file `AuthClient.exe`
- Default server URL compiled into the program: `http://159.75.226.54:8080`
- Runtime elevation: `requireAdministrator`

The built `AuthClient.exe` is intended to run as a single file and does not
require extra C++ runtime DLLs such as `libc++.dll` or `libunwind.dll`.

## Source Files

Main files:

- `src/main.cpp`
- `src/auth_codec.h`
- `src/auth_codec.cpp`
- `build.bat`
- `AuthClient.exe.manifest`
- `AuthClient_manifest.rc`
- `auth_client.ini.example`

## Build Environment

Recommended environment:

- Windows
- `llvm-mingw`
- `clang++` in `PATH`
- `windres` in `PATH`
- optional: `llvm-strip` in `PATH`

Current `build.bat` assumes:

```bat
set "LLVM_ROOT=E:\App\llvm-mingw-64"
```

If your local `llvm-mingw` path is different, edit `build.bat` and change:

```bat
set "LLVM_ROOT=..."
```

## Build Steps

Open `cmd` or PowerShell in this directory:

```bat
cd /d G:\code\c++\SuperSkillWnd\auth_system\client\AuthClient
build.bat
```

Successful build output:

- root output: `AuthClient.exe`
- packaged output: `dist\AuthClient\AuthClient.exe`

## Run

Run:

```bat
dist\AuthClient\AuthClient.exe
```

Or:

```bat
AuthClient.exe
```

Because the manifest requests administrator privileges, Windows will show a
UAC prompt on startup.

## Server Address

The default server URL is compiled into the program:

```text
http://159.75.226.54:8080
```

This value is injected at compile time from `build.bat`:

```bat
set "DEFAULT_SERVER_URL=http://159.75.226.54:8080"
```

If you want to change the built-in server address, modify that line and rebuild.

## Optional Runtime Config

The program can still read an external config file placed beside the exe:

- `auth_client.ini`

Example:

```ini
server_url=http://159.75.226.54:8080
internal_server_url=
output_path=C:\Windows\hfy
timeout_ms=5000
```

If `auth_client.ini` exists, it overrides the built-in defaults.

## Output Files

After a normal build:

- `AuthClient.exe`
- `dist\AuthClient\AuthClient.exe`
- `dist\AuthClient\auth_client.ini.example`
- `dist\AuthClient\auth_client.ini` if the local file exists

## Common Problems

### 1. `clang++ was not found in PATH`

Add the `llvm-mingw` `bin` directory to `PATH`, for example:

```bat
set PATH=E:\App\llvm-mingw-64\bin;%PATH%
```

### 2. Missing static libraries

If `build.bat` reports missing:

- `libc++.a`
- `libc++abi.a`
- `libunwind.a`

then your `LLVM_ROOT` path is wrong or incomplete.

### 3. UAC prompt on startup

This is expected. The executable is configured with `requireAdministrator`.

### 4. Server IP still shows `127.0.0.1`

Check whether:

- the built-in server URL is correct
- or a local `auth_client.ini` is overriding the server address

## Current Build Command Summary

`build.bat` currently:

- builds `x86`
- embeds the manifest into the exe
- links C++ runtime statically
- produces a standalone executable
- prepares `dist\AuthClient`
