@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars32.bat"
if errorlevel 1 (
    echo ERROR: vcvars32.bat failed!
    exit /b 1
)
echo === vcvars32 OK ===
cd /d G:\code\c++\SuperSkillWnd

if not exist "build\Debug" mkdir "build\Debug"
echo [0/4] Skipping Reader runtime publish (json/local package mode) ...

echo [1/4] Compiling resources ...
rc /nologo /fo"build\Debug\resource.res" src\resource.rc
if errorlevel 1 (
    echo ERROR: Resource compilation failed!
    pause
    exit /b 1
)

echo [2/4] Compiling sources ...
cl /nologo /Zi /MD /utf-8 /EHsc ^
 /I"src" ^
 /I"src\\third_party\\imgui" ^
 /I"src\\third_party\\imgui\\backends" ^
 /c ^
 src\dllmain.cpp ^
 src\hook\win32_input_spoof.cpp ^
 src\skill\skill_local_data.cpp ^
 src\skill\skill_overlay_source.cpp ^
 src\skill\skill_overlay_source_manager.cpp ^
 src\skill\skill_overlay_source_game.cpp ^
 src\skill\skill_overlay_bridge.cpp ^
 src\ui\retro_skill_app.cpp ^
 src\ui\retro_skill_assets.cpp ^
 src\ui\retro_render_backend.cpp ^
 src\ui\retro_skill_panel.cpp ^
 src\ui\retro_skill_state.cpp ^
 src\ui\retro_skill_text_dwrite.cpp ^
 src\ui\super_imgui_overlay.cpp ^
 src\ui\super_imgui_overlay_d3d8.cpp ^
 src\d3d8\d3d8_renderer.cpp ^
 src\third_party\imgui\imgui.cpp ^
 src\third_party\imgui\imgui_draw.cpp ^
 src\third_party\imgui\imgui_tables.cpp ^
 src\third_party\imgui\imgui_widgets.cpp ^
 src\third_party\imgui\backends\imgui_impl_dx9.cpp ^
 src\third_party\imgui\backends\imgui_impl_d3d8.cpp ^
 src\third_party\imgui\backends\imgui_impl_win32.cpp ^
 /Fo"build\Debug\\"
if errorlevel 1 (
    echo ERROR: Compilation failed!
    pause
    exit /b 1
)

echo [3/4] Linking SuperSkillWnd.dll ...
link /nologo /DLL /DEBUG /OUT:"build\Debug\SS.dll" ^
 build\Debug\dllmain.obj ^
 build\Debug\win32_input_spoof.obj ^
 build\Debug\skill_local_data.obj ^
 build\Debug\skill_overlay_source.obj ^
 build\Debug\skill_overlay_source_manager.obj ^
 build\Debug\skill_overlay_source_game.obj ^
 build\Debug\skill_overlay_bridge.obj ^
 build\Debug\retro_skill_app.obj ^
 build\Debug\retro_skill_assets.obj ^
 build\Debug\retro_render_backend.obj ^
 build\Debug\retro_skill_panel.obj ^
 build\Debug\retro_skill_state.obj ^
 build\Debug\retro_skill_text_dwrite.obj ^
 build\Debug\super_imgui_overlay.obj ^
 build\Debug\super_imgui_overlay_d3d8.obj ^
 build\Debug\d3d8_renderer.obj ^
 build\Debug\imgui.obj ^
 build\Debug\imgui_draw.obj ^
 build\Debug\imgui_tables.obj ^
 build\Debug\imgui_widgets.obj ^
 build\Debug\imgui_impl_dx9.obj ^
 build\Debug\imgui_impl_d3d8.obj ^
 build\Debug\imgui_impl_win32.obj ^
 build\Debug\resource.res ^
 d3d9.lib user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib crypt32.lib
if errorlevel 1 (
    echo ERROR: Link failed!
    pause
    exit /b 1
)

echo.
echo [4/4] Build complete.
echo === BUILD OK: build\Debug\SuperSkillWnd.dll ===
