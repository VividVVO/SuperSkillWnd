#pragma once

#include <windows.h>

bool OverlayIsMouseMessage(UINT msg);
bool OverlayIsKeyboardMessage(UINT msg);
bool OverlayIsMouseButtonMessage(UINT msg);
bool OverlayIsMouseButtonDownMessage(UINT msg);
bool OverlayIsMouseButtonUpMessage(UINT msg);
int OverlayToImGuiMouseButton(UINT msg, WPARAM wParam);
bool OverlayGetClientMousePointFromMessage(HWND hwnd, UINT msg, LPARAM lParam, POINT* outPoint);
bool OverlayAreAnyPhysicalMouseButtonsDown();

