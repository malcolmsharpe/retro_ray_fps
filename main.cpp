#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <vector>

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define FR(i,a,b) for(int i=(a);i<(b);++i)
#define FOR(i,n) FR(i,0,n)
#define BEND(v) (v).begin(),(v).end()

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

void failIMG(const char * msg)
{
    std::printf("IMG %s failed: %s\n", msg, IMG_GetError());
    exit(1);
}
#define CHECK_IMG(expr) if ((expr) < 0) failIMG(#expr)

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

SDL_Texture * LoadTexture(SDL_Renderer * ren, const char * path)
{
    SDL_Texture *tex = IMG_LoadTexture(ren, path);
    if (!tex) failIMG("LoadTexture");
    return tex;
}

// SDL data, cleanup, etc.
SDL_Window * win = NULL;
TTF_Font * font = NULL;
SDL_Renderer * ren = NULL;

sdl_ptr<SDL_Texture> pixel_screen;

sdl_ptr<SDL_Texture> red_brick;
sdl_ptr<SDL_Texture> green_brick;
sdl_ptr<SDL_Texture> red_panel;
sdl_ptr<SDL_Texture> green_panel;
sdl_ptr<SDL_Texture> red_2panel;
sdl_ptr<SDL_Texture> green_2panel;

sdl_ptr<SDL_Texture> frog_sprite;

void cleanup()
{
    red_brick.reset();
    green_brick.reset();
    red_panel.reset();
    green_panel.reset();
    red_2panel.reset();
    green_2panel.reset();

    frog_sprite.reset();

    pixel_screen.reset();

    if (ren) SDL_DestroyRenderer(ren);
    if (font) TTF_CloseFont(font);
    if (win) SDL_DestroyWindow(win);

    IMG_Quit();
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
const int TILE_COLS = 320;//256;//128;
const int TILE_ROWS = 240;//192;//96;

const int WIN_WIDTH = 1600;
const int WIN_HEIGHT = 1200;

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
    "#..........f...#",
    "###......f.....#",
    "##.............#",
    "#......####..###",
    "#......#.......#",
    "#......#.......#",
    "#..............#",
    "#......#########",
    "#..............#",
    "################",
};

const double EPS = 1e-8;

const double PLAYER_MOVE_SPEED = 2.0;

double player_x;
double player_y;
double player_angle; // in interval [0,1)

double player_dx;
double player_dy;
double screen_tan_max;

struct Vector3D {
    double x,y,z;
};

struct SceneRect {
    double z,x,y,w,h;
};

struct ViewRect {
    double x,y,w,h;
};

Vector3D world_to_scene(Vector3D v_world)
{
    Vector3D v_scene;
    v_scene.z = player_dx * (v_world.x - player_x) + player_dy * (v_world.y - player_y);
    v_scene.x = -player_dy * (v_world.x - player_x) + player_dx * (v_world.y - player_y);
    v_scene.y = -v_world.z;
    return v_scene;
}

ViewRect scene_to_view(SceneRect r_scene)
{
    ViewRect r_view;
    r_view.x = r_scene.x / r_scene.z;
    r_view.y = r_scene.y / r_scene.z;
    r_view.w = r_scene.w / r_scene.z;
    r_view.h = r_scene.h / r_scene.z;
    return r_view;
}

SDL_Rect view_to_sdl(ViewRect r_view)
{
    double tile_per_view = (TILE_COLS-1) / (2.0 * screen_tan_max);

    SDL_Rect r_sdl;
    r_sdl.x = static_cast<int>(round((r_view.x) * tile_per_view + TILE_COLS/2.0));
    r_sdl.y = static_cast<int>(round((r_view.y) * tile_per_view + TILE_ROWS/2.0));
    int x2 = static_cast<int>(round((r_view.x + r_view.w) * tile_per_view + TILE_COLS/2.0));
    int y2 = static_cast<int>(round((r_view.y + r_view.h) * tile_per_view + TILE_ROWS/2.0));
    r_sdl.w = x2 - r_sdl.x;
    r_sdl.h = y2 - r_sdl.y;
    return r_sdl;
}

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

struct Entity
{
    SDL_Texture * sprite;
    double x;
    double y;

    double height_scene;
    double width_scene;
    Vector3D scene_coords;

    int sprite_w;
    int sprite_h;
    double depth;

    Entity(SDL_Texture & sprite_, double x_, double y_)
        : sprite(&sprite_)
        , x(x_)
        , y(y_)
    {
        depth = 0;
        height_scene = 0.8; // TODO
        width_scene = 0.8;

        CHECK_SDL(SDL_QueryTexture(sprite, NULL, NULL, &sprite_w, &sprite_h));
    }

    Vector3D world_coords()
    {
        Vector3D ret;
        ret.x = x;
        ret.y = y;
        ret.z = -0.5;
        return ret;
    }
};
std::vector<Entity> entities;

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

void drawtilerect(int x, int y, int w, int h)
{
    SDL_Rect rect;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    CHECK_SDL(SDL_RenderFillRect(ren, &rect));
}

void drawtile(int x, int y)
{
    drawtilerect(x, y, 1, 1);
}

void render()
{
    //// useful global values
    player_dx = cos(2*M_PI * player_angle);
    player_dy = sin(2*M_PI * player_angle);

    double screen_angle_max = FOV/2;
    screen_tan_max = tan(2*M_PI * screen_angle_max);

    //// floor & ceiling
    CHECK_SDL(SDL_SetRenderTarget(ren, pixel_screen.get()));

    CHECK_SDL(SDL_SetRenderDrawColor(ren, 40, 40, 40, 255));
    CHECK_SDL(SDL_RenderClear(ren));

    CHECK_SDL(SDL_SetRenderDrawColor(ren, 135, 206, 235, 255));
    drawtilerect(0, 0, TILE_COLS, TILE_ROWS/2);

    //// ray-casting
    double straight_x = 0;
    double straight_y = 0;
    double straight_dist = 0;

    FOR(screen_col, TILE_COLS) {
        double col_tan = -screen_tan_max + 2*screen_tan_max * screen_col / (TILE_COLS-1);
        double col_angle_offset = atan(col_tan) / (2*M_PI);
        double col_angle = player_angle + col_angle_offset;
        wrap_angle(col_angle);

        double proj_xs[4] = {};
        double proj_ys[4] = {};
        double proj_dists[4] = {};
        int proj_colors[4] = {};
        int proj_tex_offsets[4] = {};

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
                    proj_tex_offsets[0] = static_cast<int>(16*(y-yf));
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
                    proj_tex_offsets[1] = static_cast<int>(16*(x-xf));
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
                    proj_tex_offsets[2] = static_cast<int>(16*(y-yf));
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
                    proj_tex_offsets[3] = static_cast<int>(16*(x-xf));
                    break;
                }
            }
        }

        // render the column
        double proj_x = 0;
        double proj_y = 0;
        double proj_dist = 0;
        int proj_color = 0;
        int proj_tex_offset = 0;
        FOR(dir,4) if (proj_dists[dir] != 0 && (proj_dist == 0 || proj_dists[dir] < proj_dist)) {
            proj_x = proj_xs[dir];
            proj_y = proj_ys[dir];
            proj_dist = proj_dists[dir];
            proj_color = proj_colors[dir];
            proj_tex_offset = proj_tex_offsets[dir];
        }

        if (proj_dist != 0) {
            // TODO: Be more sensible when proj_dist is close to zero.
            double viewport_unit_per_wall_unit = 1.0 / proj_dist;
            double viewport_dist_per_tile = 2 * screen_tan_max / (TILE_COLS-1);

            double wall_viewport_height = viewport_unit_per_wall_unit;
            double wall_tile_height = wall_viewport_height / viewport_dist_per_tile;

            int wall_half_tile_height = static_cast<int>(round(wall_tile_height/2));

            int screen_y1 = TILE_ROWS/2-wall_half_tile_height;
            int screen_y2 = TILE_ROWS/2+wall_half_tile_height;

            double color_mult = 1.0;

            color_mult = 0.2 * std::min(1.0/proj_dist, 1.0) + 0.8;

            SDL_Texture * tex = NULL;
            if (proj_color == 2) {
                tex = green_2panel.get();
            } else {
                tex = red_2panel.get();
            }

            SDL_Rect srcrect = { proj_tex_offset, 0, 1, 16 };
            SDL_Rect dstrect = { screen_col, screen_y1, 1, screen_y2-screen_y1 };
            CHECK_SDL(SDL_RenderCopy(ren, tex, &srcrect, &dstrect));

            // for diagnostics
            if (screen_col == TILE_COLS/2) {
                straight_x = proj_x;
                straight_y = proj_y;
                straight_dist = proj_dist;
            }
        }
    }

    //// sprites
    // Sort by decreasing depth
    for (auto & e : entities) {
        e.scene_coords = world_to_scene(e.world_coords());
    }
    std::sort(BEND(entities), [](Entity const & a, Entity const & b) { return a.scene_coords.z > b.scene_coords.z; });

    for (auto & e : entities) {
        if (e.scene_coords.z > EPS) {
            SceneRect ent_rect_scene;
            ent_rect_scene.z = e.scene_coords.z;
            ent_rect_scene.x = e.scene_coords.x - e.width_scene/2.0;
            ent_rect_scene.y = e.scene_coords.y - e.height_scene;
            ent_rect_scene.w = e.width_scene;
            ent_rect_scene.h = e.height_scene;

            ViewRect ent_rect_view = scene_to_view(ent_rect_scene);
            SDL_Rect ent_rect_sdl = view_to_sdl(ent_rect_view);

            CHECK_SDL(SDL_RenderCopy(ren, e.sprite, NULL, &ent_rect_sdl));
        }
    }

    //// mini-map
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

    //// scale up
    CHECK_SDL(SDL_SetRenderTarget(ren, NULL));
    CHECK_SDL(SDL_RenderCopy(ren, pixel_screen.get(), NULL, NULL));

    //// diagnostics
    CHECK_SDL(SDL_SetRenderDrawColor(ren, 255, 255, 255, 255));
    char buf[256];

    snprintf(buf, sizeof(buf),
        "X=%.2lf, Y=%.2lf, A=%.2lf, dX=%.2lf, dY=%.2lf ;  X=%.2lf, Y=%.2lf, D=%.2lf ;  t=%.1lf ms",
        player_x, player_y, player_angle, player_dx, player_dy,
        straight_x, straight_y, straight_dist,
        avgFrameTime_ms());
    DrawText(ren, font, buf, {255, 255, 255, 255}, 0, 0, NULL, NULL, false);

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

    int flags = IMG_INIT_PNG;
    if ((IMG_Init(flags) & flags) != flags) failIMG("IMG_Init");

    font = TTF_OpenFont("data/Vera.ttf", FONT_HEIGHT);
    if (!font) failTTF("TTF_OpenFont");

    win = SDL_CreateWindow("SDL Simple FPS",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_WIDTH, WIN_HEIGHT, SDL_WINDOW_SHOWN);
    if (!win) failSDL("SDL_CreateWindow");

    ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) failSDL("SDL_CreateRenderer");

    pixel_screen.reset(SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, TILE_COLS, TILE_ROWS));
    if (!pixel_screen) failSDL("SDL_CreateTexture");

    // load textures
    red_brick.reset(LoadTexture(ren, "data/red_brick.png"));
    green_brick.reset(LoadTexture(ren, "data/green_brick.png"));
    red_panel.reset(LoadTexture(ren, "data/red_panel.png"));
    green_panel.reset(LoadTexture(ren, "data/green_panel.png"));
    red_2panel.reset(LoadTexture(ren, "data/red_2panel.png"));
    green_2panel.reset(LoadTexture(ren, "data/green_2panel.png"));
    frog_sprite.reset(LoadTexture(ren, "data/frog.png"));

    // init game
    player_x = 1.5;
    player_y = 14.5;
    player_angle = 0;

    FOR(y,MAP_HEIGHT) {
        FOR(x,MAP_HEIGHT) {
            if (map_grid[y][x] == 'f') {
                entities.push_back(Entity(*frog_sprite, x + 0.5, y + 0.5));
            }
        }
    }

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
