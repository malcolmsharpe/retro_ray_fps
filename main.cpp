#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define FR(i,a,b) for(int i=(a);i<(b);++i)
#define FOR(i,n) FR(i,0,n)

// SDL utilities
struct delete_sdl
{
    void operator()(SDL_Texture * p) const
    {
        SDL_DestroyTexture(p);
    }

    void operator()(SDL_Surface * p) const
    {
        SDL_FreeSurface(p);
    }
};

template<class T>
using sdl_ptr = std::unique_ptr<T, delete_sdl>;

void failSDL(const char * msg)
{
    std::printf("SDL %s failed: %s\n", msg, SDL_GetError());
    exit(1);
}
#define CHECK_SDL(expr) if ((expr) < 0) failSDL(#expr)

void failTTF(const char * msg)
{
    std::printf("TTF %s failed: %s\n", msg, TTF_GetError());
    exit(1);
}
#define CHECK_TTF(expr) if ((expr) < 0) failTTF(#expr)

void DrawText(SDL_Renderer * ren, TTF_Font * font, const char * s, SDL_Color color, int x, int y, int * textW, int * textH, bool center = false)
{
    int tW, tH;
    if (textW == NULL) textW = &tW;
    if (textH == NULL) textH = &tH;

    sdl_ptr<SDL_Surface> textSurf(TTF_RenderText_Solid(font, s, color));
    if (!textSurf) failTTF("TTF_RenderText_Solid");

    sdl_ptr<SDL_Texture> textTex(SDL_CreateTextureFromSurface(ren, textSurf.get()));
    if (!textTex) failSDL("SDL_CreateTextureFromSurface");

    if (SDL_QueryTexture(textTex.get(), NULL, NULL, textW, textH) < 0) failSDL("SDL_QueryTexture");

    if (center) {
        x -= *textW / 2;
        y -= *textH / 2;
    }

    SDL_Rect dst = { x, y, *textW, *textH };
    if (SDL_RenderCopy(ren, textTex.get(), NULL, &dst) < 0) failSDL("SDL_RenderCopy");
}

// SDL data, cleanup, etc.
SDL_Window * win = NULL;
TTF_Font * font = NULL;
SDL_Renderer * ren = NULL;

void cleanup()
{
    if (ren) SDL_DestroyRenderer(ren);
    if (font) TTF_CloseFont(font);
    if (win) SDL_DestroyWindow(win);

    TTF_Quit();
    SDL_Quit();
}

// FPS tracking
const int CIRCBUF_LEN = 64;
Uint32 circbuf[CIRCBUF_LEN];
int circbuf_i;

void initFPSTracking()
{
    memset(circbuf, 0, sizeof(circbuf));
    circbuf_i = 0;
}

void accumTime(Uint32 ms)
{
    circbuf[circbuf_i++] = ms;
    if (circbuf_i >= CIRCBUF_LEN) circbuf_i = 0;
}

double avgFrameTime_ms()
{
    double ret = 0;
    FOR(i, CIRCBUF_LEN) ret += circbuf[i];
    return ret / CIRCBUF_LEN;
}

// main code
const int TILE_COLS = 128;
const int TILE_ROWS = 96;
const int TILE_SIZE = 8;

const int WIN_WIDTH = TILE_SIZE * TILE_COLS;
const int WIN_HEIGHT = TILE_SIZE * TILE_ROWS;

const int FONT_HEIGHT = 16;

const int MAP_HEIGHT = 16;
const int MAP_WIDTH = 16;

char map_grid[MAP_HEIGHT][MAP_WIDTH+1] = {
    "#########.......",
    "#..............#",
    "#.......########",
    "#..............#",
    "#......##......#",
    "#......##......#",
    "#..............#",
    "###............#",
    "##.............#",
    "#......####..###",
    "#......#.......#",
    "#......#.......#",
    "#..............#",
    "#......#########",
    "#..............#",
    "################",
};

const double PLAYER_MOVE_SPEED = 2.0;

double player_x;
double player_y;
double player_angle; // in interval [0,1)

void wrap_angle(double & angle) {
    double intpart;
    double fracpart = modf(angle, &intpart);
    if (fracpart < 0) fracpart += 1;

    angle = fracpart;
}

double deltaFrame_s;

void moveplayer(double amt, double angle)
{
    amt *= deltaFrame_s;
    player_x += amt * cos(2*M_PI * angle);
    player_y += amt * sin(2*M_PI * angle);
}

void moveplayer(double amt)
{
    moveplayer(amt, player_angle);
}

void strafeplayer(double amt)
{
    double angle = player_angle + 0.25;
    wrap_angle(angle);
    moveplayer(amt, angle);
}

void rotateplayer(double amt)
{
    amt *= deltaFrame_s;
    player_angle += amt;
    wrap_angle(player_angle);
}

bool quitRequested;
void update()
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            quitRequested = true;
        }

        if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_ESCAPE) {
                quitRequested = true;
            }
        }
    }

    Uint8 const * state = SDL_GetKeyboardState(NULL);
    if (state[SDL_SCANCODE_S] || state[SDL_SCANCODE_DOWN]) {
        moveplayer(-PLAYER_MOVE_SPEED);
    }
    if (state[SDL_SCANCODE_W] || state[SDL_SCANCODE_UP]) {
        moveplayer(PLAYER_MOVE_SPEED);
    }
    if (state[SDL_SCANCODE_LEFT]) {
        rotateplayer(-1.0/2.0);
    }
    if (state[SDL_SCANCODE_RIGHT]) {
        rotateplayer(1.0/2.0);
    }
    if (state[SDL_SCANCODE_A]) {
        strafeplayer(-PLAYER_MOVE_SPEED);
    }
    if (state[SDL_SCANCODE_D]) {
        strafeplayer(PLAYER_MOVE_SPEED);
    }
}

const double FOV = 0.25; // in interval [0,1)

void drawtile(int x, int y)
{
    SDL_Rect rect;
    rect.x = x * TILE_SIZE;
    rect.y = y * TILE_SIZE;
    rect.w = TILE_SIZE;
    rect.h = TILE_SIZE;
    CHECK_SDL(SDL_RenderFillRect(ren, &rect));
}

void drawtilerect(int x, int y, int w, int h)
{
    SDL_Rect rect;
    rect.x = x * TILE_SIZE;
    rect.y = y * TILE_SIZE;
    rect.w = w * TILE_SIZE;
    rect.h = h * TILE_SIZE;
    CHECK_SDL(SDL_RenderFillRect(ren, &rect));
}

void render()
{
    CHECK_SDL(SDL_SetRenderDrawColor(ren, 40, 40, 40, 255));
    CHECK_SDL(SDL_RenderClear(ren));

    CHECK_SDL(SDL_SetRenderDrawColor(ren, 135, 206, 235, 255));

    drawtilerect(0, 0, TILE_COLS, TILE_ROWS/2);

    double straight_x = 0;
    double straight_y = 0;
    double straight_dist = 0;

    double player_dx = cos(2*M_PI * player_angle);
    double player_dy = sin(2*M_PI * player_angle);

    FOR(screen_col, TILE_COLS) {
        double screen_angle_max = FOV/2;
        double screen_tan_max = tan(2*M_PI * screen_angle_max);

        double col_tan = -screen_tan_max + 2*screen_tan_max * screen_col / (TILE_COLS-1);
        double col_angle_offset = atan(col_tan) / (2*M_PI);
        double col_angle = player_angle + col_angle_offset;
        wrap_angle(col_angle);

        double proj_xs[4] = {};
        double proj_ys[4] = {};
        double proj_dists[4] = {};
        int proj_colors[4] = {};

        if (col_angle < 0.25 || 0.75 < col_angle) {
            // intersect ray with west-facing walls
            double slope = tan(2*M_PI * col_angle);

            int x1 = static_cast<int>(ceil(player_x));
            x1 = std::max(x1, 0);

            FR(x, x1, MAP_WIDTH) {
                double y = (x-player_x) * slope + player_y;
                int yf = static_cast<int>(y);

                if (0 <= yf && yf < MAP_HEIGHT && map_grid[yf][x] == '#') {
                    proj_xs[0] = x;
                    proj_ys[0] = y;
                    proj_dists[0] = player_dx * (x-player_x) + player_dy * (y-player_y);
                    proj_colors[0] = 1;
                    break;
                }
            }
        }

        if (0.0 < col_angle && col_angle < 0.5) {
            // intersect ray with north-facing walls
            double slope = tan(2*M_PI * (0.25-col_angle));

            int y1 = static_cast<int>(ceil(player_y));
            y1 = std::max(y1, 0);

            FR(y, y1, MAP_HEIGHT) {
                double x = (y-player_y) * slope + player_x;
                int xf = static_cast<int>(x);

                if (0 <= xf && xf < MAP_WIDTH && map_grid[y][xf] == '#') {
                    proj_xs[1] = x;
                    proj_ys[1] = y;
                    proj_dists[1] = player_dx * (x-player_x) + player_dy * (y-player_y);
                    proj_colors[1] = 2;
                    break;
                }
            }
        }

        if (0.25 < col_angle && col_angle < 0.75) {
            // intersect ray with east-facing walls
            double slope = tan(2*M_PI * col_angle);

            int x1 = static_cast<int>(floor(player_x));
            x1 = std::min(x1, MAP_WIDTH-1);

            for (int x = x1; x >= 1; --x) {
                double y = (x-player_x) * slope + player_y;
                int yf = static_cast<int>(y);

                if (0 <= yf && yf < MAP_HEIGHT && map_grid[yf][x-1] == '#') {
                    proj_xs[2] = x;
                    proj_ys[2] = y;
                    proj_dists[2] = player_dx * (x-player_x) + player_dy * (y-player_y);
                    proj_colors[2] = 1;
                    break;
                }
            }
        }

        if (0.5 < col_angle && col_angle < 1.0) {
            // intersect ray with south-facing walls
            double slope = tan(2*M_PI * (0.25-col_angle));

            int y1 = static_cast<int>(floor(player_y));
            y1 = std::min(y1, MAP_HEIGHT-1);

            for (int y = y1; y >= 1; --y) {
                double x = (y-player_y) * slope + player_x;
                int xf = static_cast<int>(x);

                if (0 <= xf && xf < MAP_WIDTH && map_grid[y-1][xf] == '#') {
                    proj_xs[3] = x;
                    proj_ys[3] = y;
                    proj_dists[3] = player_dx * (x-player_x) + player_dy * (y-player_y);
                    proj_colors[3] = 2;
                    break;
                }
            }
        }

        // render the column
        double proj_x = 0;
        double proj_y = 0;
        double proj_dist = 0;
        int proj_color = 0;
        FOR(dir,4) if (proj_dists[dir] != 0 && (proj_dist == 0 || proj_dists[dir] < proj_dist)) {
            proj_x = proj_xs[dir];
            proj_y = proj_ys[dir];
            proj_dist = proj_dists[dir];
            proj_color = proj_colors[dir];
        }

        if (proj_dist != 0) {
            double height_mult = 1.0 / proj_dist;

            int screen_half_height = static_cast<int>(round((TILE_ROWS/2) * height_mult));

            int screen_y1 = TILE_ROWS/2-screen_half_height;
            int screen_y2 = TILE_ROWS/2+screen_half_height;

            int r = 0, g = 0, b = 0;

            switch (proj_color) {
            case 1: r = 255; break;
            case 2: g = 255; break;
            }

            double color_mult = 1.0;

            color_mult = 0.2 * std::min(1.0/proj_dist, 1.0) + 0.8;

            r = static_cast<int>(color_mult * r);
            g = static_cast<int>(color_mult * g);
            b = static_cast<int>(color_mult * b);

            CHECK_SDL(SDL_SetRenderDrawColor(ren, r, g, b, 255));

            drawtilerect(screen_col, screen_y1, 1, screen_y2 - screen_y1);

            // for diagnostics
            if (screen_col == TILE_COLS/2) {
                straight_x = proj_x;
                straight_y = proj_y;
                straight_dist = proj_dist;
            }
        }
    }

    // diagnostics
    CHECK_SDL(SDL_SetRenderDrawColor(ren, 255, 255, 255, 255));
    char buf[256];

    snprintf(buf, sizeof(buf),
        "X=%.2lf, Y=%.2lf, A=%.2lf ;  X=%.2lf, Y=%.2lf, D=%.2lf ;  t=%.1lf ms",
        player_x, player_y, player_angle,
        straight_x, straight_y, straight_dist,
        avgFrameTime_ms());
    DrawText(ren, font, buf, {255, 255, 255, 255}, 0, 0, NULL, NULL, false);

    // mini-map
    FOR(y,MAP_HEIGHT) {
        FOR(x,MAP_WIDTH) {
            if (map_grid[y][x] == '#') {
                CHECK_SDL(SDL_SetRenderDrawColor(ren, 255, 255, 255, 255));
            } else {
                CHECK_SDL(SDL_SetRenderDrawColor(ren, 0, 0, 0, 255));
            }

            drawtile(TILE_COLS - MAP_WIDTH + x, y);
        }
    }
    int minimap_x = static_cast<int>(player_x);
    int minimap_y = static_cast<int>(player_y);
    if (0 <= minimap_x && minimap_x < MAP_WIDTH && 0 <= minimap_y && minimap_y < MAP_HEIGHT) {
        CHECK_SDL(SDL_SetRenderDrawColor(ren, 150, 63, 255, 255));
        drawtile(TILE_COLS - MAP_WIDTH + minimap_x, minimap_y);
    }

    SDL_RenderPresent(ren);
}

Uint32 prevFrame_ms;
void main_loop()
{
    Uint32 thisFrame_ms = SDL_GetTicks();
    Uint32 deltaFrame_ms = thisFrame_ms - prevFrame_ms;
    accumTime(deltaFrame_ms);
    deltaFrame_s = deltaFrame_ms / 1000.0;
    update();
    render();

    prevFrame_ms = thisFrame_ms;
}

int main()
{
    atexit(cleanup);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) failSDL("SDL_Init");
    if (TTF_Init() == -1) failTTF("TTF_Init");

    font = TTF_OpenFont("data/Vera.ttf", FONT_HEIGHT);
    if (!font) failTTF("TTF_OpenFont");

    win = SDL_CreateWindow("SDL ASCII FPS",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_WIDTH, WIN_HEIGHT, SDL_WINDOW_SHOWN);
    if (!win) failSDL("SDL_CreateWindow");

    ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) failSDL("SDL_CreateRenderer");

    // init game
    player_x = 1.5;
    player_y = 14.5;
    player_angle = 0;

    // IO loop
    prevFrame_ms = SDL_GetTicks();
    quitRequested = false;

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(main_loop, 0, 0);
#else
    while (!quitRequested) {
        main_loop();
    }
#endif

    return 0;
}
