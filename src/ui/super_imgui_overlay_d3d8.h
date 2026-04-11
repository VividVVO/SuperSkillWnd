#pragma once

#include <windows.h>

struct ImFont;

bool SuperD3D8OverlayEnsureInitialized(HWND hwnd, void* device8, float mainScale, const char* assetPath);
void SuperD3D8OverlayShutdown();
void SuperD3D8OverlaySetVisible(bool visible);
void SuperD3D8OverlaySetAnchor(int x, int y);
void SuperD3D8OverlayResetPanelState();
void SuperD3D8OverlayOnDeviceLost();
void SuperD3D8OverlayOnDeviceReset(void* device8);
bool SuperD3D8OverlayHandleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void SuperD3D8OverlayRender(void* device8);
bool SuperD3D8OverlayIsInitialized();
bool SuperD3D8OverlayWantsMouseCapture();
bool SuperD3D8OverlayShouldSuppressGameMouse();
void SuperD3D8OverlayCancelMouseCapture();
HWND SuperD3D8OverlayGetGameHwnd();
ImFont* SuperD3D8OverlayGetConsolasFont();
