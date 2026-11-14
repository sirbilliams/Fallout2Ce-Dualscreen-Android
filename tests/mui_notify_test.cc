// Exercise the actual notification drawing with a narrow combat sidebar.
// Drawing stubs check that plates, text and the typing cursor are clipped.
#include "../src/mui_notify.cc"

#include <cstdlib>

namespace fallout {
namespace {
    int clipDepth;
    int draws;
    bool expectClip;
    MuiRect expectedArea;

    void check(bool value)
    {
        if (!value) std::abort();
    }

    void drawn()
    {
        check(clipDepth == (expectClip ? 1 : 0));
        ++draws;
    }
}

const MuiTheme& muiTheme()
{
    static MuiTheme theme {};
    return theme;
}
std::u32string muiDecodeGameText(const char*) { return U"An enemy was hit."; }
std::vector<std::u32string> muiWrapText(const std::u32string& text, float, float)
{
    // An unbreakable line wider than the sidebar must still be clipped.
    return { text };
}
float muiLineHeight(float size) { return size; }
float muiTextWidth(const std::u32string& text, float size) { return text.size() * size; }
void muiFillRect(const MuiRect&, MuiColor) { drawn(); }
void muiFillRoundRect(const MuiRect&, float, MuiColor) { drawn(); }
void muiDrawTextAligned(const std::u32string&, const MuiRect&, float, MuiColor, MuiAlign, MuiAlign) { drawn(); }
void muiPushClip(const MuiRect& area)
{
    check(clipDepth++ == 0);
    check(area.x == expectedArea.x && area.y == expectedArea.y
        && area.w == expectedArea.w && area.h == expectedArea.h);
}
void muiPopClip() { check(--clipDepth == 0); }
}

int main()
{
    using namespace fallout;
    MuiContext ui;
    ui.pixelsPerDp = 1.0f;
    ui.now = 1100;
    Notice notice;
    notice.text = "An enemy was hit.";
    notice.kind = MuiNoticeKind::Log;
    notice.shownTime = 1000;
    expectedArea = { 10.0f, 800.0f, 52.0f, 104.0f };

    // Combat: every draw, including the partially typed cursor, is clipped.
    expectClip = true;
    muiNotifyHudArea(expectedArea, false, true);
    drawHud(ui, { &notice });
    check(draws == 3 && clipDepth == 0);

    // Leaving combat resets the clip through the existing two-argument API.
    expectClip = false;
    draws = 0;
    muiNotifyHudArea({ 70.0f, 800.0f, 400.0f, 104.0f }, false);
    drawHud(ui, { &notice });
    check(draws == 3 && clipDepth == 0);
}
