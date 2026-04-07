#pragma once

#include <windows.h>

bool Win32InputSpoofInstall();
void Win32InputSpoofUninstall();
void Win32InputSpoofSetSuppressMouse(bool suppress);
bool Win32InputSpoofIsInstalled();
bool Win32InputSpoofIsSuppressing();
LPARAM Win32InputSpoofMakeOffscreenMouseLParam();
