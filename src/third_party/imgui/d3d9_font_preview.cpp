
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <string>
#include <vector>
#include <sstream>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

struct FontSpec {
    const wchar_t* name;
    int height;
    int weight;
    BYTE quality;
};

struct FontEntry {
    FontSpec spec;
    ID3DXFont* font = nullptr;
};

static HWND g_hwnd = nullptr;
static IDirect3D9* g_d3d = nullptr;
static IDirect3DDevice9* g_dev = nullptr;
static D3DPRESENT_PARAMETERS g_pp = {};
static std::vector<FontEntry> g_fonts;
static int g_width = 1500;
static int g_height = 980;

static const wchar_t* kSample =
    L"0123456789   111111   000000   888888   1470  2468  9999";

static COLORREF kBg = RGB(250, 250, 250);

static std::wstring QualityName(BYTE q) {
    switch (q) {
    case NONANTIALIASED_QUALITY: return L"NONANTIALIASED_QUALITY";
    case ANTIALIASED_QUALITY:    return L"ANTIALIASED_QUALITY";
    case CLEARTYPE_QUALITY:      return L"CLEARTYPE_QUALITY";
    default: {
        std::wstringstream ss;
        ss << L"QUALITY=" << (int)q;
        return ss.str();
    }
    }
}

static void SafeRelease(IUnknown*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

static HRESULT CreateFontEntry(FontEntry& e) {
    if (!g_dev) return E_FAIL;
    return D3DXCreateFontW(
        g_dev,
        e.spec.height,          // Height
        0,                      // Width (0 = auto)
        e.spec.weight,
        1,                      // MipLevels
        FALSE,                  // Italic
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        e.spec.quality,
        DEFAULT_PITCH | FF_DONTCARE,
        e.spec.name,
        &e.font
    );
}

static void DestroyFonts() {
    for (auto& e : g_fonts) {
        if (e.font) {
            e.font->Release();
            e.font = nullptr;
        }
    }
}

static HRESULT CreateFonts() {
    DestroyFonts();
    for (auto& e : g_fonts) {
        HRESULT hr = CreateFontEntry(e);
        if (FAILED(hr)) return hr;
    }
    return S_OK;
}

static HRESULT InitD3D(HWND hwnd) {
    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_d3d) return E_FAIL;

    ZeroMemory(&g_pp, sizeof(g_pp));
    g_pp.Windowed = TRUE;
    g_pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_pp.BackBufferFormat = D3DFMT_UNKNOWN;
    g_pp.EnableAutoDepthStencil = FALSE;
    g_pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    HRESULT hr = g_d3d->CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
        &g_pp,
        &g_dev
    );
    if (FAILED(hr)) {
        hr = g_d3d->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
            &g_pp,
            &g_dev
        );
    }
    if (FAILED(hr)) return hr;

    return CreateFonts();
}

static void CleanupD3D() {
    DestroyFonts();
    IUnknown* unk = reinterpret_cast<IUnknown*>(g_dev);
    SafeRelease(unk);
    g_dev = nullptr;
    unk = reinterpret_cast<IUnknown*>(g_d3d);
    SafeRelease(unk);
    g_d3d = nullptr;
}

static void ResetDevice() {
    if (!g_dev) return;

    DestroyFonts();

    RECT rc{};
    GetClientRect(g_hwnd, &rc);
    g_width = rc.right - rc.left;
    g_height = rc.bottom - rc.top;

    g_pp.BackBufferWidth = g_width;
    g_pp.BackBufferHeight = g_height;

    HRESULT hr = g_dev->Reset(&g_pp);
    if (SUCCEEDED(hr)) {
        CreateFonts();
    }
}

static void DrawTextLine(ID3DXFont* font, int x, int y, D3DCOLOR color, const std::wstring& s) {
    RECT rc{ x, y, x + 3000, y + 200 };
    font->DrawTextW(nullptr, s.c_str(), -1, &rc, DT_LEFT | DT_NOCLIP, color);
}

static void Render() {
    if (!g_dev) return;

    HRESULT coop = g_dev->TestCooperativeLevel();
    if (coop == D3DERR_DEVICELOST) {
        Sleep(20);
        return;
    }
    if (coop == D3DERR_DEVICENOTRESET) {
        ResetDevice();
        return;
    }

    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(250, 250, 250), 1.0f, 0);
    if (SUCCEEDED(g_dev->BeginScene())) {
        int y = 18;

        // 标题
        if (!g_fonts.empty() && g_fonts[0].font) {
            DrawTextLine(g_fonts[0].font, 20, y, D3DCOLOR_XRGB(20, 20, 20),
                L"D3D9 + ID3DXFont 真实机内对比（13px）");
            y += 30;
            DrawTextLine(g_fonts[0].font, 20, y, D3DCOLOR_XRGB(90, 90, 90),
                L"说明：这是 D3D9 中常见的 ID3DXFont 渲染路径，和浏览器/CSS 不一样；"
                L"但仍可能与某些游戏原生文字管线略有差异。");
            y += 30;
        }

        std::wstring currentGroup;
        for (size_t i = 0; i < g_fonts.size(); ++i) {
            auto& e = g_fonts[i];
            std::wstring group = QualityName(e.spec.quality);
            if (group != currentGroup) {
                currentGroup = group;
                y += 10;
                if (e.font) {
                    DrawTextLine(e.font, 20, y, D3DCOLOR_XRGB(0, 70, 160), group);
                }
                y += 26;
            }

            if (e.font) {
                std::wstringstream left;
                left << e.spec.name << L"  (13px)";
                DrawTextLine(e.font, 28, y, D3DCOLOR_XRGB(40, 40, 40), left.str());
                DrawTextLine(e.font, 300, y, D3DCOLOR_XRGB(0, 0, 0), kSample);
                y += 26;
            }
        }

        y += 20;
        if (!g_fonts.empty() && g_fonts[0].font) {
            DrawTextLine(g_fonts[0].font, 20, y, D3DCOLOR_XRGB(110, 110, 110),
                L"Esc 退出，F5 重建设备与字体，窗口缩放可直接看小字号变化。");
        }

        g_dev->EndScene();
    }
    g_dev->Present(nullptr, nullptr, nullptr, nullptr);
}

static void BuildFontList() {
    g_fonts.clear();

    std::vector<const wchar_t*> faces = {
        L"Segoe UI",
        L"Tahoma",
        L"Verdana",
        L"Arial",
        L"Microsoft Sans Serif",
        L"Calibri",
        L"Bahnschrift",
        L"Trebuchet MS",
        L"Georgia",
        L"Times New Roman",
        L"Cambria",
        L"Consolas",
        L"Courier New",
        L"Lucida Console",
        L"Microsoft YaHei",
        L"SimSun",
        L"SimHei",
        L"Microsoft JhengHei",
        L"MingLiU",
        L"Yu Gothic UI"
    };

    std::vector<BYTE> qualities = {
        NONANTIALIASED_QUALITY,
        ANTIALIASED_QUALITY,
        CLEARTYPE_QUALITY
    };

    for (BYTE q : qualities) {
        for (auto* face : faces) {
            FontEntry e{};
            e.spec.name = face;
            e.spec.height = 13;
            e.spec.weight = FW_NORMAL;
            e.spec.quality = q;
            g_fonts.push_back(e);
        }
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        if (g_dev && wParam != SIZE_MINIMIZED) {
            ResetDevice();
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (wParam == VK_F5) {
            ResetDevice();
            return 0;
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    BuildFontList();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(kBg);
    wc.lpszClassName = L"D3D9FontPreviewWnd";

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"RegisterClassExW 失败", L"Error", MB_ICONERROR);
        return 1;
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rc{ 0, 0, g_width, g_height };
    AdjustWindowRect(&rc, style, FALSE);

    g_hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"D3D9 Font Preview - 13px Numbers",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInst, nullptr
    );

    if (!g_hwnd) {
        MessageBoxW(nullptr, L"CreateWindowExW 失败", L"Error", MB_ICONERROR);
        return 1;
    }

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    if (FAILED(InitD3D(g_hwnd))) {
        MessageBoxW(
            g_hwnd,
            L"初始化 D3D9 / D3DXFont 失败。\n\n"
            L"请确认：\n"
            L"1) 你的系统/环境有 d3d9.lib\n"
            L"2) 有 d3dx9.lib / d3dx9_43.dll（常见于 DirectX SDK June 2010）",
            L"Init Failed",
            MB_ICONERROR
        );
        CleanupD3D();
        return 1;
    }

    MSG msg{};
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        } else {
            Render();
        }
    }

    CleanupD3D();
    return 0;
}
