#include "ui/overlay_cursor_utils.h"
#include "core/Common.h"

namespace
{
    static const int kMaxShowCursorAdjustIterations = 32;

    void AdjustShowCursorHiddenState(bool hideCursor)
    {
        int lastResult = 0;
        for (int i = 0; i < kMaxShowCursorAdjustIterations; ++i)
        {
            lastResult = ::ShowCursor(hideCursor ? FALSE : TRUE);
            if (hideCursor ? (lastResult < 0) : (lastResult >= 0))
                return;
        }

        static LONG s_showCursorClampWarnBudget = 12;
        const LONG budgetAfterDecrement = InterlockedDecrement(&s_showCursorClampWarnBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[Cursor] WARN: ShowCursor did not converge hide=%d last=%d iterations=%d",
                hideCursor ? 1 : 0,
                lastResult,
                kMaxShowCursorAdjustIterations);
        }
    }
}

void OverlayUpdateCursorSuppression(
    bool& cursorSuppressed,
    bool& showCursorHidden,
    HCURSOR& savedCursor,
    bool shouldSuppress)
{
    if (shouldSuppress)
    {
        if (!cursorSuppressed)
        {
            savedCursor = ::GetCursor();
            cursorSuppressed = true;
        }
        if (!showCursorHidden)
        {
            AdjustShowCursorHiddenState(true);
            showCursorHidden = true;
        }
        ::SetCursor(nullptr);
        return;
    }

    if (cursorSuppressed)
    {
        if (showCursorHidden)
        {
            AdjustShowCursorHiddenState(false);
            showCursorHidden = false;
        }
        ::SetCursor(savedCursor);
        savedCursor = nullptr;
        cursorSuppressed = false;
    }
}
