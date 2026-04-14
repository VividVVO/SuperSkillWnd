#pragma once

#include <windows.h>

void OverlayUpdateCursorSuppression(
    bool& cursorSuppressed,
    bool& showCursorHidden,
    HCURSOR& savedCursor,
    bool shouldSuppress);

