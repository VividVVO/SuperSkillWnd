#pragma once

struct ImFont;

void OverlayConfigureImGuiStyle(float mainScale);
void OverlayLoadMainAndConsolasFonts(float mainScale, ImFont** outMainFont, ImFont** outConsolasFont);

