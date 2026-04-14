#include "ui/overlay_input_utils.h"

bool OverlayIsMouseMessage(UINT msg)
{
    switch (msg)
    {
    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
        return true;
    default:
        return false;
    }
}

bool OverlayIsKeyboardMessage(UINT msg)
{
    switch (msg)
    {
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_IME_CHAR:
        return true;
    default:
        return false;
    }
}

bool OverlayIsMouseButtonMessage(UINT msg)
{
    switch (msg)
    {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
        return true;
    default:
        return false;
    }
}

bool OverlayIsMouseButtonDownMessage(UINT msg)
{
    switch (msg)
    {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
        return true;
    default:
        return false;
    }
}

bool OverlayIsMouseButtonUpMessage(UINT msg)
{
    switch (msg)
    {
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP:
        return true;
    default:
        return false;
    }
}

int OverlayToImGuiMouseButton(UINT msg, WPARAM wParam)
{
    switch (msg)
    {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
        return 0;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
        return 1;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
        return 2;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
        return (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? 3 : 4;
    default:
        return -1;
    }
}

bool OverlayGetClientMousePointFromMessage(HWND hwnd, UINT msg, LPARAM lParam, POINT* outPoint)
{
    if (!outPoint)
        return false;

    auto getSignedX = [](LPARAM lp) -> LONG { return (LONG)(short)LOWORD(lp); };
    auto getSignedY = [](LPARAM lp) -> LONG { return (LONG)(short)HIWORD(lp); };

    POINT pt = {};
    switch (msg)
    {
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        pt.x = getSignedX(lParam);
        pt.y = getSignedY(lParam);
        if (!hwnd || !::ScreenToClient(hwnd, &pt))
            return false;
        break;
    default:
        pt.x = getSignedX(lParam);
        pt.y = getSignedY(lParam);
        break;
    }

    *outPoint = pt;
    return true;
}

bool OverlayAreAnyPhysicalMouseButtonsDown()
{
    return ((::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) ||
           ((::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0) ||
           ((::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0) ||
           ((::GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0) ||
           ((::GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0);
}

