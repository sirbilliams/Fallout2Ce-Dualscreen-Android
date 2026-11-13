// Include the implementation so the test can set its private timer values
// without adding engine APIs just for tests. Unused engine code is discarded.
#include "../src/scripts.cc"

#include <cstdlib>

namespace fallout {
static int storyStage;
static int reputation;
static int enemyArroyo;
static int played;
static int areaChanges;
GameQuitRequest _game_user_wants_to_quit;

int gameGetGlobalVar(GameGlobalVar var)
{
    if (var == GVAR_FALLOUT_2) return storyStage;
    if (var == GVAR_ENEMY_ARROYO) return enemyArroyo;
    if (var == GVAR_TOWN_REP_ARROYO) return reputation;
    return 0;
}
int gameSetGlobalVar(GameGlobalVar var, int value)
{
    if (var == GVAR_TOWN_REP_ARROYO) reputation = value;
    return 0;
}
bool gameMovieIsSeen(int) { return false; }
int gameMoviePlay(int movie, int)
{
    played = movie;
    return 0;
}
bool wmAreaSetVisibleState(City, CityState, bool)
{
    ++areaChanges;
    return true;
}
bool wmAreaMarkVisitedState(City, VisitedState)
{
    ++areaChanges;
    return true;
}
void windowShow(int) { }
void windowHide(int) { }
void tileWindowRefresh() { }
} // namespace fallout

static void check(bool value)
{
    if (!value) std::abort();
}

int main()
{
    using namespace fallout;
    auto event = [](int day) {
        gGameTime = day * GAME_TIME_TICKS_PER_DAY;
        played = -1;
        int movie = -2;
        _scriptsCheckGameEvents(&movie, -1);
        check(movie == played);
        return movie;
    };
    // The repack disables all four timers. Passing days must never alter the
    // world map or reputation, even after vanilla's last deadline.
    gMovieTimerArtimer1 = gMovieTimerArtimer2 = gMovieTimerArtimer3 = gMovieTimerArtimer4 = -1;
    for (int day : { 0, 90, 180, 270, 360, 1000 })
        check(event(day) == -1);
    check(reputation == 0 && areaChanges == 0);
    // A disabled later event must not hide an enabled earlier event.
    gMovieTimerArtimer1 = 90;
    check(event(89) == -1);
    check(event(90) == MOVIE_ARTIMER1);
    gMovieTimerArtimer1 = -1;
    gMovieTimerArtimer2 = 180;
    check(event(180) == MOVIE_ARTIMER2);
    gMovieTimerArtimer2 = -1;
    gMovieTimerArtimer3 = 270;
    check(event(270) == MOVIE_ARTIMER3);
    gMovieTimerArtimer3 = -1;
    gMovieTimerArtimer4 = 360;
    check(event(359) == -1);
    check(event(360) == MOVIE_ARTIMER4);
    check(areaChanges == 3);
    // Plot-triggered endings still work with timers disabled.
    gMovieTimerArtimer4 = -1;
    storyStage = 3;
    check(event(0) == MOVIE_ARTIMER4);
    storyStage = 0;
    enemyArroyo = 1;
    check(event(0) == MOVIE_AFAILED);
    check(_game_user_wants_to_quit == GAME_QUIT_REQUEST_MAIN_MENU);
}
