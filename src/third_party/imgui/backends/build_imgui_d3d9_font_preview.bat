@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "IMGUI_DIR=%SCRIPT_DIR%.."
set "SRC=%SCRIPT_DIR%imgui_d3d9_font_preview.cpp"
set "OUT=%SCRIPT_DIR%imgui_d3d9_font_preview.exe"

if not exist "%SRC%" (
  echo [ERROR] Source file not found:
  echo %SRC%
  pause
  exit /b 1
)

if not exist "%IMGUI_DIR%\imgui.h" (
  echo [ERROR] imgui.h not found:
  echo %IMGUI_DIR%\imgui.h
  pause
  exit /b 1
)

cl /nologo /std:c++17 /EHsc /utf-8 ^
  "%SRC%" ^
  "%IMGUI_DIR%\imgui.cpp" ^
  "%IMGUI_DIR%\imgui_draw.cpp" ^
  "%IMGUI_DIR%\imgui_tables.cpp" ^
  "%IMGUI_DIR%\imgui_widgets.cpp" ^
  "%SCRIPT_DIR%imgui_impl_win32.cpp" ^
  "%SCRIPT_DIR%imgui_impl_dx9.cpp" ^
  /I "%IMGUI_DIR%" ^
  /I "%SCRIPT_DIR%" ^
  user32.lib gdi32.lib d3d9.lib ^
  /Fe:"%OUT%"

if errorlevel 1 (
  echo.
  echo Build failed.
  pause
  exit /b 1
)

echo.
echo Build succeeded:
echo %OUT%
pause