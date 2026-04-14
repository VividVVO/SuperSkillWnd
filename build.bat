@echo off
setlocal

cd /d "%~dp0"

set "VCVARS32=C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VCVARS32%" (
    echo ERROR: vcvars32.bat not found: "%VCVARS32%"
    exit /b 1
)

where /Q cl
if errorlevel 1 (
    rem Keep the inherited shell environment small before entering vcvars32.
    set "PATH=%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem;%SystemRoot%\System32\WindowsPowerShell\v1.0\"
    set "INCLUDE="
    set "LIB="
    set "LIBPATH="
    call "%VCVARS32%"
    if errorlevel 1 (
        echo ERROR: vcvars32.bat failed!
        exit /b 1
    )
    echo === vcvars32 OK ===
) else (
    echo === using existing MSVC environment ===
)

if not exist "build\Debug" mkdir "build\Debug"
echo [0/4] Skipping Reader runtime publish (json/local package mode) ...

echo [1/4] Compiling resources ...
rc /nologo /fo"build\Debug\resource.res" src\resource.rc
if errorlevel 1 (
    echo ERROR: Resource compilation failed!
    pause
    exit /b 1
)

> "build\Debug\compile.rsp" (
    echo /nologo
    echo /Zi
    echo /MD
    echo /utf-8
    echo /EHsc
    echo /I"src"
    echo /I"src\third_party\imgui"
    echo /I"src\third_party\imgui\backends"
    echo /c
    echo /Fo"build\Debug\\"
    echo src\dllmain.cpp
    echo src\hook\win32_input_spoof.cpp
    echo src\runtime\init_pipeline.cpp
    echo src\runtime\cleanup_pipeline.cpp
    echo src\util\runtime_paths.cpp
    echo src\skill\skill_local_data.cpp
    echo src\skill\skill_overlay_source.cpp
    echo src\skill\skill_overlay_source_manager.cpp
    echo src\skill\skill_overlay_source_game.cpp
    echo src\skill\skill_overlay_bridge.cpp
    echo src\ui\retro_skill_app.cpp
    echo src\ui\retro_skill_assets.cpp
    echo src\ui\retro_render_backend.cpp
    echo src\ui\retro_skill_panel.cpp
    echo src\ui\retro_skill_state.cpp
    echo src\ui\retro_skill_text_dwrite.cpp
    echo src\ui\overlay_input_utils.cpp
    echo src\ui\overlay_cursor_utils.cpp
    echo src\ui\overlay_style_utils.cpp
    echo src\ui\super_imgui_overlay.cpp
    echo src\ui\super_imgui_overlay_d3d8.cpp
    echo src\d3d8\d3d8_renderer.cpp
    echo src\third_party\imgui\imgui.cpp
    echo src\third_party\imgui\imgui_draw.cpp
    echo src\third_party\imgui\imgui_tables.cpp
    echo src\third_party\imgui\imgui_widgets.cpp
    echo src\third_party\imgui\backends\imgui_impl_dx9.cpp
    echo src\third_party\imgui\backends\imgui_impl_d3d8.cpp
    echo src\third_party\imgui\backends\imgui_impl_win32.cpp
)

echo [2/4] Compiling sources ...
cl @"build\Debug\compile.rsp"
if errorlevel 1 (
    echo ERROR: Compilation failed!
    pause
    exit /b 1
)

> "build\Debug\link.rsp" (
    echo /nologo
    echo /DLL
    echo /DEBUG
    echo /OUT:"build\Debug\SS.dll"
    echo build\Debug\dllmain.obj
    echo build\Debug\win32_input_spoof.obj
    echo build\Debug\init_pipeline.obj
    echo build\Debug\cleanup_pipeline.obj
    echo build\Debug\runtime_paths.obj
    echo build\Debug\skill_local_data.obj
    echo build\Debug\skill_overlay_source.obj
    echo build\Debug\skill_overlay_source_manager.obj
    echo build\Debug\skill_overlay_source_game.obj
    echo build\Debug\skill_overlay_bridge.obj
    echo build\Debug\retro_skill_app.obj
    echo build\Debug\retro_skill_assets.obj
    echo build\Debug\retro_render_backend.obj
    echo build\Debug\retro_skill_panel.obj
    echo build\Debug\retro_skill_state.obj
    echo build\Debug\retro_skill_text_dwrite.obj
    echo build\Debug\overlay_input_utils.obj
    echo build\Debug\overlay_cursor_utils.obj
    echo build\Debug\overlay_style_utils.obj
    echo build\Debug\super_imgui_overlay.obj
    echo build\Debug\super_imgui_overlay_d3d8.obj
    echo build\Debug\d3d8_renderer.obj
    echo build\Debug\imgui.obj
    echo build\Debug\imgui_draw.obj
    echo build\Debug\imgui_tables.obj
    echo build\Debug\imgui_widgets.obj
    echo build\Debug\imgui_impl_dx9.obj
    echo build\Debug\imgui_impl_d3d8.obj
    echo build\Debug\imgui_impl_win32.obj
    echo build\Debug\resource.res
    echo d3d9.lib
    echo user32.lib
    echo gdi32.lib
    echo shell32.lib
    echo ole32.lib
    echo oleaut32.lib
    echo crypt32.lib
)

echo [3/4] Linking SuperSkillWnd.dll ...
link @"build\Debug\link.rsp"
if errorlevel 1 (
    echo ERROR: Link failed!
    pause
    exit /b 1
)

copy /Y "build\Debug\SS.dll" "build\Debug\hook.dll" >nul
copy /Y "build\Debug\SS.pdb" "build\Debug\hook.pdb" >nul

echo.
echo [4/4] Build complete.
echo === BUILD OK: build\Debug\SS.dll ===
endlocal
