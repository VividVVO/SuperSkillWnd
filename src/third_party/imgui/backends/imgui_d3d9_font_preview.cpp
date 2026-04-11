
#include <windows.h>
#include <d3d9.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <filesystem>
#include <sstream>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx9.h"

#pragma comment(lib, "d3d9.lib")

namespace fs = std::filesystem;

// Data
static LPDIRECT3D9 g_pD3D = nullptr;
static LPDIRECT3DDEVICE9 g_pd3dDevice = nullptr;
static D3DPRESENT_PARAMETERS g_d3dpp = {};
static UINT g_dpi = 96;

struct FontItem
{
    std::string display_name;
    std::wstring file_path;
    float size_px = 13.0f;
    ImFont *font = nullptr;
    bool loaded = false;
};

static std::vector<FontItem> g_fonts;

static std::string Narrow(const std::wstring &ws)
{
    if (ws.empty())
        return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(len ? len - 1 : 0, '\0');
    if (len > 1)
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, s.data(), len, nullptr, nullptr);
    return s;
}

static std::wstring GetWindowsFontsDir()
{
    wchar_t winDir[MAX_PATH] = {};
    GetWindowsDirectoryW(winDir, MAX_PATH);
    std::wstring path = winDir;
    path += L"\\Fonts\\";
    return path;
}

static void AddFontCandidate(const char *displayName, const wchar_t *fileName, float sizePx = 13.0f)
{
    std::wstring full = GetWindowsFontsDir() + fileName;
    FontItem item;
    item.display_name = displayName;
    item.file_path = full;
    item.size_px = sizePx;
    g_fonts.push_back(item);
}

static void BuildFontCandidates()
{
    g_fonts.clear();

    // Sans / UI
    AddFontCandidate("Segoe UI", L"segoeui.ttf");
    AddFontCandidate("Tahoma", L"tahoma.ttf");
    AddFontCandidate("Verdana", L"verdana.ttf");
    AddFontCandidate("Arial", L"arial.ttf");
    AddFontCandidate("Microsoft Sans Serif", L"micross.ttf");
    AddFontCandidate("Calibri", L"calibri.ttf");
    AddFontCandidate("Bahnschrift", L"bahnschrift.ttf");
    AddFontCandidate("Trebuchet MS", L"trebuc.ttf");

    // Serif
    AddFontCandidate("Georgia", L"georgia.ttf");
    AddFontCandidate("Times New Roman", L"times.ttf");
    AddFontCandidate("Cambria", L"cambria.ttc");

    // Monospace
    AddFontCandidate("Consolas", L"consola.ttf");
    AddFontCandidate("Courier New", L"cour.ttf");
    AddFontCandidate("Lucida Console", L"lucon.ttf");

    // CJK
    AddFontCandidate("Microsoft YaHei", L"msyh.ttc");
    AddFontCandidate("SimSun", L"simsun.ttc");
    AddFontCandidate("SimHei", L"simhei.ttf");
    AddFontCandidate("Microsoft JhengHei", L"msjh.ttc");
    AddFontCandidate("MingLiU", L"mingliu.ttc");
    AddFontCandidate("Yu Gothic UI", L"YuGothR.ttc");
}

static bool CreateDeviceD3D(HWND hWnd)
{
    if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == nullptr)
        return false;

    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    if (g_pD3D->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            hWnd,
            D3DCREATE_HARDWARE_VERTEXPROCESSING,
            &g_d3dpp,
            &g_pd3dDevice) < 0)
    {
        if (g_pD3D->CreateDevice(
                D3DADAPTER_DEFAULT,
                D3DDEVTYPE_HAL,
                hWnd,
                D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                &g_d3dpp,
                &g_pd3dDevice) < 0)
        {
            return false;
        }
    }
    return true;
}

static void CleanupDeviceD3D()
{
    if (g_pd3dDevice)
    {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
    if (g_pD3D)
    {
        g_pD3D->Release();
        g_pD3D = nullptr;
    }
}

static void ResetDevice()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
    if (hr == D3DERR_INVALIDCALL)
        IM_ASSERT(0);
    ImGui_ImplDX9_CreateDeviceObjects();
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void LoadFontsIntoImGui()
{
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFontConfig cfg;
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = true;

    // Default font
    io.Fonts->AddFontDefault();

    for (auto &f : g_fonts)
    {
        f.loaded = false;
        f.font = nullptr;
        if (!fs::exists(f.file_path))
            continue;

        ImFont *font = io.Fonts->AddFontFromFileTTF(
            Narrow(f.file_path).c_str(),
            f.size_px,
            &cfg,
            io.Fonts->GetGlyphRangesDefault());
        if (font)
        {
            f.font = font;
            f.loaded = true;
        }
    }
}

static void ReloadFonts()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    LoadFontsIntoImGui();
    ImGui_ImplDX9_CreateDeviceObjects();
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
        {
            g_d3dpp.BackBufferWidth = LOWORD(lParam);
            g_d3dpp.BackBufferHeight = HIWORD(lParam);
            ResetDevice();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DPICHANGED:
        g_dpi = HIWORD(wParam);
        if (const RECT *suggested = reinterpret_cast<RECT *>(lParam))
        {
            SetWindowPos(hWnd, nullptr,
                         suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void SetupImGuiStyle()
{
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.ItemSpacing = ImVec2(8, 6);
    style.FramePadding = ImVec2(8, 6);
}

static void DrawFontPreviewTable()
{
    static const char *sample = "0123456789   111111   000000   888888   1470   2468   9999";
    static bool use_demo_bg = false;

    ImGui::Checkbox("浅灰底块", &use_demo_bg);
    ImGui::SameLine();
    if (ImGui::Button("重新加载字体"))
        ReloadFonts();

    ImGui::Separator();
    ImGui::TextWrapped("说明：这是 ImGui 的字体 atlas 渲染效果，不是 Windows GDI / ID3DXFont。"
                       " 适合看你在 ImGui 里真正会看到的 13px 数字效果。");
    ImGui::Spacing();

    if (ImGui::BeginTable("font_preview", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupColumn("字体");
        ImGui::TableSetupColumn("文件");
        ImGui::TableSetupColumn("状态");
        ImGui::TableSetupColumn("13px 数字预览", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (auto &f : g_fonts)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(f.display_name.c_str());

            ImGui::TableSetColumnIndex(1);
            std::string file = fs::exists(f.file_path) ? Narrow(fs::path(f.file_path).filename().wstring()) : "(missing)";
            ImGui::TextUnformatted(file.c_str());

            ImGui::TableSetColumnIndex(2);
            if (f.loaded && f.font)
                ImGui::TextUnformatted("loaded");
            else
                ImGui::TextUnformatted("missing/failed");

            ImGui::TableSetColumnIndex(3);
            if (f.loaded && f.font)
            {
                ImGui::PushFont(f.font);
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));

                if (use_demo_bg)
                {
                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeightWithSpacing() + 4.0f);
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(242, 242, 242, 255), 4.0f);
                    ImGui::Dummy(ImVec2(0, 2));
                }

                ImGui::TextUnformatted(sample);

                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
            else
            {
                ImGui::TextDisabled("-");
            }
        }

        ImGui::EndTable();
    }
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    BuildFontCandidates();

    WNDCLASSEXW wc = {
        sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L,
        GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
        L"ImGuiD3D9FontPreviewWnd", nullptr};
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowW(
        wc.lpszClassName,
        L"ImGui + D3D9 Font Preview (13px Numbers)",
        WS_OVERLAPPEDWINDOW,
        100, 100, 1500, 950,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        MessageBoxW(hwnd, L"CreateDeviceD3D failed.", L"Error", MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    SetupImGuiStyle();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_pd3dDevice);

    LoadFontsIntoImGui();

    bool done = false;
    while (!done)
    {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        if (g_pd3dDevice->TestCooperativeLevel() == D3DERR_DEVICELOST)
        {
            Sleep(10);
            continue;
        }

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(1460, 900), ImGuiCond_Once);
        ImGui::Begin("13px Font Preview", nullptr, ImGuiWindowFlags_NoCollapse);

        ImGui::Text("Renderer: ImGui + D3D9");
        ImGui::SameLine();
        ImGui::TextDisabled("| DPI: %u", g_dpi);
        ImGui::Separator();

        DrawFontPreviewTable();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextWrapped("注意：ImGui 的字体路径和系统原生 GDI 不同。"
                           " 如果你是要复刻游戏原生小字，ImGui 看到的效果通常会更平滑、"
                           "更 atlas 化，不会完全等同于系统 API 或游戏私有文字管线。");

        ImGui::End();

        ImGui::EndFrame();

        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        D3DCOLOR clear_col_dx = D3DCOLOR_RGBA(255, 255, 255, 255);
        g_pd3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_col_dx, 1.0f, 0);

        if (g_pd3dDevice->BeginScene() >= 0)
        {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_pd3dDevice->EndScene();
        }

        HRESULT result = g_pd3dDevice->Present(nullptr, nullptr, nullptr, nullptr);
        if (result == D3DERR_DEVICELOST && g_pd3dDevice->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
            ResetDevice();
    }

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}
