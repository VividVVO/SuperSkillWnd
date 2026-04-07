#pragma once

#include <windows.h>
#include <d3d9.h>

bool SuperImGuiOverlayEnsureInitialized(HWND hwnd, IDirect3DDevice9* device, float mainScale, const char* assetPath);
void SuperImGuiOverlayShutdown();
void SuperImGuiOverlaySetVisible(bool visible);
void SuperImGuiOverlaySetAnchor(int x, int y);
void SuperImGuiOverlayResetPanelState();
void SuperImGuiOverlayOnDeviceLost();
void SuperImGuiOverlayOnDeviceReset(IDirect3DDevice9* device);
bool SuperImGuiOverlayHandleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void SuperImGuiOverlayRender(IDirect3DDevice9* device);
bool SuperImGuiOverlayIsInitialized();
bool SuperImGuiOverlayWantsMouseCapture();
bool SuperImGuiOverlayShouldSuppressGameMouse();
void SuperImGuiOverlayCancelMouseCapture();
HWND SuperImGuiOverlayGetGameHwnd();
