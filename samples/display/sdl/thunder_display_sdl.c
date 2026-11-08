/* A Thunder display built on SDL2: a real window for a program run outside the IDE.
 *
 *     thundervm --display sdl 28_snake.thc
 *     ./snake --thunder-display sdl          (a packaged program)
 *
 * It implements the whole of <thunder/display.h>: open a window, draw each frame's shapes,
 * and send key and mouse events back to the program. Nothing here is Thunder-specific beyond
 * that header - the same shape of file would wrap any other graphics library.
 *
 * Build (Linux/macOS):
 *     cc -std=c11 -shared -fPIC -I<thunder>/include $(pkg-config --cflags sdl2) \
 *        -o libthunder_display_sdl.so thunder_display_sdl.c $(pkg-config --libs sdl2)
 *
 * Text is drawn with a small built-in 5x7 font so that the plug-in needs SDL2 alone and no
 * font files; swap in SDL_ttf if you want real typography (and Pashto letter shaping).
 */

#include <thunder/display.h>

#include <SDL.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct SdlState
{
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* picture; /* the canvas, kept between frames so "keep" costs nothing */
    int32_t width;
    int32_t height;
    ThunderEvent pending[64];
    int pending_count;
    int pending_read;
    char key_text[64][16]; /* the key names the events point at */
} SdlState;

/* ------------------------------------------------------------------ colors */

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return 0;
}

/* Thunder always sends "#rrggbb". */
static void set_color(SDL_Renderer* renderer, const char* color)
{
    Uint8 rgb[3] = {0, 0, 0};
    int index;
    if (color != NULL && strlen(color) == 7 && color[0] == '#')
    {
        for (index = 0; index < 3; ++index)
        {
            rgb[index] = (Uint8)(hex_digit(color[1 + index * 2]) * 16 + hex_digit(color[2 + index * 2]));
        }
    }
    SDL_SetRenderDrawColor(renderer, rgb[0], rgb[1], rgb[2], 255);
}

/* ------------------------------------------------------------------ shapes */

static void fill_circle(SDL_Renderer* renderer, double cx, double cy, double radius)
{
    int dy;
    const int r = (int)(radius + 0.5);
    for (dy = -r; dy <= r; ++dy)
    {
        const double span = sqrt((double)r * r - (double)dy * dy);
        SDL_RenderDrawLine(renderer, (int)(cx - span), (int)(cy + dy), (int)(cx + span), (int)(cy + dy));
    }
}

static void draw_circle(SDL_Renderer* renderer, double cx, double cy, double radius, double width)
{
    double outer = radius + width / 2;
    double inner = radius - width / 2;
    int dy;
    const int r = (int)(outer + 0.5);
    for (dy = -r; dy <= r; ++dy)
    {
        const double squared = (double)r * r - (double)dy * dy;
        const double span = squared > 0 ? sqrt(squared) : 0;
        double hole = inner * inner - (double)dy * dy;
        if (hole > 0)
        {
            const double gap = sqrt(hole);
            SDL_RenderDrawLine(renderer, (int)(cx - span), (int)(cy + dy), (int)(cx - gap), (int)(cy + dy));
            SDL_RenderDrawLine(renderer, (int)(cx + gap), (int)(cy + dy), (int)(cx + span), (int)(cy + dy));
        }
        else
        {
            SDL_RenderDrawLine(renderer, (int)(cx - span), (int)(cy + dy), (int)(cx + span), (int)(cy + dy));
        }
    }
}

static void thick_line(SDL_Renderer* renderer, double x1, double y1, double x2, double y2, double width)
{
    int step;
    const int passes = width < 2 ? 1 : (int)width;
    /* A plain line plus a few neighbours is enough for a teaching display. */
    for (step = 0; step < passes; ++step)
    {
        const double shift = (double)step - (double)(passes - 1) / 2;
        const double dx = x2 - x1, dy = y2 - y1;
        const double length = sqrt(dx * dx + dy * dy);
        const double nx = length > 0 ? -dy / length * shift : 0;
        const double ny = length > 0 ? dx / length * shift : shift;
        SDL_RenderDrawLine(renderer, (int)(x1 + nx), (int)(y1 + ny), (int)(x2 + nx), (int)(y2 + ny));
    }
}

/* A 5x7 pixel font for the printable ASCII range: enough for scores and labels. Each glyph is
 * five columns, one byte per column, bit 0 at the top. */
static const unsigned char FONT[95][5] = {
    {0,0,0,0,0},{0,0,0x5f,0,0},{0,7,0,7,0},{0x14,0x7f,0x14,0x7f,0x14},{0x24,0x2a,0x7f,0x2a,0x12},
    {0x23,0x13,8,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0,5,3,0,0},{0,0x1c,0x22,0x41,0},
    {0,0x41,0x22,0x1c,0},{0x14,8,0x3e,8,0x14},{8,8,0x3e,8,8},{0,0x50,0x30,0,0},{8,8,8,8,8},
    {0,0x60,0x60,0,0},{0x20,0x10,8,4,2},{0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},
    {0x27,0x45,0x45,0x45,0x39},{0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},{0x36,0x49,0x49,0x49,0x36},
    {6,0x49,0x49,0x29,0x1e},{0,0x36,0x36,0,0},{0,0x56,0x36,0,0},{8,0x14,0x22,0x41,0},
    {0x14,0x14,0x14,0x14,0x14},{0,0x41,0x22,0x14,8},{2,1,0x51,9,6},{0x32,0x49,0x79,0x41,0x3e},
    {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
    {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,9,9,9,1},{0x3e,0x41,0x49,0x49,0x7a},
    {0x7f,8,8,8,0x7f},{0,0x41,0x7f,0x41,0},{0x20,0x40,0x41,0x3f,1},{0x7f,8,0x14,0x22,0x41},
    {0x7f,0x40,0x40,0x40,0x40},{0x7f,2,0xc,2,0x7f},{0x7f,4,8,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
    {0x7f,9,9,9,6},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,9,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {1,1,0x7f,1,1},{0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},
    {0x63,0x14,8,0x14,0x63},{7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43},{0,0x7f,0x41,0x41,0},
    {2,4,8,0x10,0x20},{0,0x41,0x41,0x7f,0},{4,2,1,2,4},{0x40,0x40,0x40,0x40,0x40},{0,1,2,4,0},
    {0x20,0x54,0x54,0x54,0x78},{0x7f,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7f},{0x38,0x54,0x54,0x54,0x18},{8,0x7e,9,1,2},{0xc,0x52,0x52,0x52,0x3e},
    {0x7f,8,4,4,0x78},{0,0x44,0x7d,0x40,0},{0x20,0x40,0x44,0x3d,0},{0x7f,0x10,0x28,0x44,0},
    {0,0x41,0x7f,0x40,0},{0x7c,4,0x18,4,0x78},{0x7c,8,4,4,0x78},{0x38,0x44,0x44,0x44,0x38},
    {0x7c,0x14,0x14,0x14,8},{8,0x14,0x14,0x18,0x7c},{0x7c,8,4,4,8},{0x48,0x54,0x54,0x54,0x20},
    {4,0x3f,0x44,0x40,0x20},{0x3c,0x40,0x40,0x20,0x7c},{0x1c,0x20,0x40,0x20,0x1c},
    {0x3c,0x40,0x30,0x40,0x3c},{0x44,0x28,0x10,0x28,0x44},{0xc,0x50,0x50,0x50,0x3c},
    {0x44,0x64,0x54,0x4c,0x44},{0,8,0x36,0x41,0},{0,0,0x7f,0,0},{0,0x41,0x36,8,0},{8,4,8,0x10,8},
};

static void draw_text(SDL_Renderer* renderer, double x, double y, double size, const char* text)
{
    /* The glyphs are seven pixels tall; scale them to the size the program asked for. */
    const int scale = (int)(size / 7 + 0.5) < 1 ? 1 : (int)(size / 7 + 0.5);
    int pen = (int)x;
    const unsigned char* at = (const unsigned char*)(text != NULL ? text : "");
    for (; *at != 0; ++at)
    {
        int column;
        if (*at < 32 || *at > 126)
        {
            /* Anything outside ASCII (Pashto, for instance) needs a real font: draw a box, so
               that it is visible that something is there. Use SDL_ttf for the real thing. */
            SDL_Rect box = {pen, (int)y, 5 * scale, 7 * scale};
            SDL_RenderDrawRect(renderer, &box);
            pen += 6 * scale;
            continue;
        }
        for (column = 0; column < 5; ++column)
        {
            int row;
            const unsigned char bits = FONT[*at - 32][column];
            for (row = 0; row < 7; ++row)
            {
                if ((bits >> row) & 1)
                {
                    SDL_Rect dot = {pen + column * scale, (int)y + row * scale, scale, scale};
                    SDL_RenderFillRect(renderer, &dot);
                }
            }
        }
        pen += 6 * scale;
    }
}

static void draw_shape(SDL_Renderer* renderer, const ThunderShape* shape, int32_t width, int32_t height)
{
    set_color(renderer, shape->color);
    if (strcmp(shape->kind, "clear") == 0)
    {
        SDL_Rect all = {0, 0, width, height};
        SDL_RenderFillRect(renderer, &all);
    }
    else if (strcmp(shape->kind, "pixel") == 0)
    {
        SDL_RenderDrawPoint(renderer, (int)shape->a, (int)shape->b);
    }
    else if (strcmp(shape->kind, "line") == 0)
    {
        thick_line(renderer, shape->a, shape->b, shape->c, shape->d, shape->size);
    }
    else if (strcmp(shape->kind, "rect") == 0)
    {
        SDL_Rect box = {(int)shape->a, (int)shape->b, (int)shape->c, (int)shape->d};
        SDL_RenderFillRect(renderer, &box);
    }
    else if (strcmp(shape->kind, "rect_outline") == 0)
    {
        int pass;
        const int passes = shape->size < 2 ? 1 : (int)shape->size;
        for (pass = 0; pass < passes; ++pass)
        {
            SDL_Rect box = {(int)shape->a + pass, (int)shape->b + pass, (int)shape->c - 2 * pass,
                (int)shape->d - 2 * pass};
            SDL_RenderDrawRect(renderer, &box);
        }
    }
    else if (strcmp(shape->kind, "circle") == 0)
    {
        fill_circle(renderer, shape->a, shape->b, shape->c);
    }
    else if (strcmp(shape->kind, "circle_outline") == 0)
    {
        draw_circle(renderer, shape->a, shape->b, shape->c, shape->size);
    }
    else if (strcmp(shape->kind, "text") == 0)
    {
        draw_text(renderer, shape->a, shape->b, shape->size, shape->text);
    }
}

/* ------------------------------------------------------------------ the interface */

static int sdl_open(ThunderDisplay* display, int32_t width, int32_t height, const char* title,
    const char** error)
{
    SdlState* state = (SdlState*)display->state;
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        *error = SDL_GetError();
        return 0;
    }
    state->window = SDL_CreateWindow(title != NULL ? title : "Thunder", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_SHOWN);
    if (state->window == NULL)
    {
        *error = SDL_GetError();
        return 0;
    }
    state->renderer = SDL_CreateRenderer(state->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (state->renderer == NULL)
    {
        state->renderer = SDL_CreateRenderer(state->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (state->renderer == NULL)
    {
        *error = SDL_GetError();
        return 0;
    }
    /* The picture lives in a texture, so a frame that only adds shapes draws only those. */
    state->picture = SDL_CreateTexture(state->renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET, width, height);
    state->width = width;
    state->height = height;
    if (state->picture != NULL)
    {
        SDL_SetRenderTarget(state->renderer, state->picture);
        SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, 255);
        SDL_RenderClear(state->renderer);
        SDL_SetRenderTarget(state->renderer, NULL);
    }
    return 1;
}

static void sdl_present(ThunderDisplay* display, const ThunderFrame* frame)
{
    SdlState* state = (SdlState*)display->state;
    int32_t index;
    if (state->renderer == NULL)
    {
        return;
    }
    if (state->picture != NULL)
    {
        SDL_SetRenderTarget(state->renderer, state->picture);
    }
    for (index = 0; index < frame->count; ++index)
    {
        draw_shape(state->renderer, &frame->shapes[index], state->width, state->height);
    }
    if (state->picture != NULL)
    {
        SDL_SetRenderTarget(state->renderer, NULL);
        SDL_RenderCopy(state->renderer, state->picture, NULL, NULL);
    }
    SDL_RenderPresent(state->renderer);
}

/* SDL's key names into Thunder's (see display.h). Anything else keeps SDL's own name in
 * lower case, which is already right for letters and digits. */
static const char* key_name(SDL_Keycode code)
{
    switch (code)
    {
    case SDLK_LEFT: return "left";
    case SDLK_RIGHT: return "right";
    case SDLK_UP: return "up";
    case SDLK_DOWN: return "down";
    case SDLK_SPACE: return "space";
    case SDLK_RETURN: case SDLK_KP_ENTER: return "enter";
    case SDLK_ESCAPE: return "escape";
    case SDLK_BACKSPACE: return "backspace";
    case SDLK_TAB: return "tab";
    default: return SDL_GetKeyName(code);
    }
}

static ThunderEvent* next_slot(SdlState* state)
{
    const int capacity = (int)(sizeof state->pending / sizeof state->pending[0]);
    if (state->pending_count >= capacity)
    {
        return NULL;
    }
    return &state->pending[state->pending_count++];
}

static void remember_key(SdlState* state, ThunderEvent* event, const char* name)
{
    const int slot = (int)(event - state->pending);
    size_t length;
    char* text = state->key_text[slot];
    strncpy(text, name != NULL ? name : "", sizeof state->key_text[0] - 1);
    text[sizeof state->key_text[0] - 1] = 0;
    for (length = 0; text[length] != 0; ++length)
    {
        if (text[length] >= 'A' && text[length] <= 'Z')
        {
            text[length] = (char)(text[length] - 'A' + 'a');
        }
    }
    event->key = text;
}

static int sdl_poll(ThunderDisplay* display, ThunderEvent* event)
{
    SdlState* state = (SdlState*)display->state;
    SDL_Event sdl_event;
    if (state->pending_read < state->pending_count)
    {
        *event = state->pending[state->pending_read++];
        return 1;
    }
    state->pending_count = 0;
    state->pending_read = 0;
    while (SDL_PollEvent(&sdl_event))
    {
        ThunderEvent* slot = next_slot(state);
        if (slot == NULL)
        {
            break;
        }
        memset(slot, 0, sizeof *slot);
        if (sdl_event.type == SDL_QUIT)
        {
            slot->kind = THUNDER_EVENT_QUIT;
        }
        else if (sdl_event.type == SDL_KEYDOWN || sdl_event.type == SDL_KEYUP)
        {
            if (sdl_event.key.repeat)
            {
                --state->pending_count;
                continue;
            }
            slot->kind = sdl_event.type == SDL_KEYDOWN ? THUNDER_EVENT_KEY_DOWN : THUNDER_EVENT_KEY_UP;
            remember_key(state, slot, key_name(sdl_event.key.keysym.sym));
        }
        else if (sdl_event.type == SDL_MOUSEMOTION || sdl_event.type == SDL_MOUSEBUTTONDOWN
            || sdl_event.type == SDL_MOUSEBUTTONUP)
        {
            int x = 0, y = 0;
            const Uint32 buttons = SDL_GetMouseState(&x, &y);
            slot->kind = THUNDER_EVENT_MOUSE;
            slot->x = x;
            slot->y = y;
            slot->button = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        }
        else
        {
            --state->pending_count;
        }
    }
    if (state->pending_read < state->pending_count)
    {
        *event = state->pending[state->pending_read++];
        return 1;
    }
    return 0;
}

static void sdl_close(ThunderDisplay* display)
{
    SdlState* state = (SdlState*)display->state;
    if (state->picture != NULL) { SDL_DestroyTexture(state->picture); }
    if (state->renderer != NULL) { SDL_DestroyRenderer(state->renderer); }
    if (state->window != NULL) { SDL_DestroyWindow(state->window); }
    SDL_Quit();
    free(state);
    display->state = NULL;
}

THUNDER_DISPLAY_EXPORT int thunder_display_init(ThunderDisplay* display)
{
    SdlState* state = (SdlState*)calloc(1, sizeof(SdlState));
    if (state == NULL)
    {
        return 0;
    }
    display->abi_version = THUNDER_DISPLAY_ABI;
    display->state = state;
    display->open = sdl_open;
    display->present = sdl_present;
    display->poll = sdl_poll;
    display->close = sdl_close;
    return 1;
}
