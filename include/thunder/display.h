/* thunder/display.h - the Thunder display interface.
 *
 * A Thunder program that imports "graphics" draws into a canvas; something else has to put
 * that canvas on a screen. The IDE does it over a pipe (docs/graphics.md, section 8), but a
 * program run from a terminal - or a packaged application - has no IDE. A *display plug-in*
 * fills that gap: a shared library that opens a window, draws the frames the program shows and
 * sends key and mouse events back.
 *
 *     thundervm --display sdl game.thc      # loads libthunder_display_sdl.so
 *     ./game --thunder-display sdl          # the same, for a packaged program
 *
 * A plug-in exports one function, thunder_display_init, which fills in the struct below. This
 * header is plain C (and valid C++) and uses only <stdint.h>: a plug-in links against nothing
 * of Thunder's, so it can be built with any compiler and against any graphics library.
 *
 * samples/display/ has two complete plug-ins: one built on SDL2, and a tiny one that writes
 * the frames as text. docs/display.md explains the interface and how to add another.
 */
#ifndef THUNDER_DISPLAY_H
#define THUNDER_DISPLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped whenever the structs below change; the VM refuses a plug-in built against another. */
#define THUNDER_DISPLAY_ABI 1

#if defined(_WIN32)
#define THUNDER_DISPLAY_EXPORT __declspec(dllexport)
#elif defined(__GNUC__)
#define THUNDER_DISPLAY_EXPORT __attribute__((visibility("default")))
#else
#define THUNDER_DISPLAY_EXPORT
#endif

/* One shape of a frame. 'kind' is one of the names below; which of a..d and text are used
 * depends on it, exactly as in the canvas (include/thunder/graphics.hpp):
 *
 *   "clear"                                        the whole canvas in 'color'
 *   "pixel"           a = x, b = y
 *   "line"            a = x1, b = y1, c = x2, d = y2,      size = line width
 *   "rect"            a = x,  b = y,  c = width, d = height
 *   "rect_outline"    a = x,  b = y,  c = width, d = height, size = line width
 *   "circle"          a = x,  b = y,  c = radius
 *   "circle_outline"  a = x,  b = y,  c = radius,            size = line width
 *   "text"            a = x,  b = y,  text = the message,    size = text height
 *
 * Coordinates are decimals and may lie outside the canvas; a display clips them.
 */
typedef struct ThunderShape
{
    const char* kind;
    double a, b, c, d;
    double size;
    const char* color; /* always "#rrggbb" */
    const char* text;  /* UTF-8, "" unless kind is "text" */
} ThunderShape;

/* One frame. The display already holds the first 'keep' shapes of the picture: a frame that
 * only adds to the picture carries just the new ones. A frame whose first shape is "clear"
 * (and whose keep is 0) starts a fresh picture. */
typedef struct ThunderFrame
{
    uint64_t number;             /* 1 for the first frame shown */
    int32_t width, height;       /* the canvas size in pixels */
    int32_t keep;                /* shapes the display already has */
    int32_t count;               /* how many shapes follow */
    const ThunderShape* shapes;
} ThunderFrame;

typedef enum ThunderEventKind
{
    THUNDER_EVENT_NONE = 0,
    THUNDER_EVENT_KEY_DOWN = 1,
    THUNDER_EVENT_KEY_UP = 2,
    THUNDER_EVENT_MOUSE = 3, /* movement or a button: x, y and button together */
    THUNDER_EVENT_QUIT = 4   /* the user closed the window: the program is stopped */
} ThunderEventKind;

/* An event on its way back to the program. 'key' uses Thunder's English key names - "left",
 * "right", "up", "down", "space", "enter", "escape", "backspace", "tab", or the character
 * itself for a letter or digit ("a", "7"). The VM translates them into the program's language.
 */
typedef struct ThunderEvent
{
    int32_t kind; /* a ThunderEventKind */
    const char* key;
    int32_t x, y;
    int32_t button; /* 1 while a mouse button is held down */
} ThunderEvent;

/* The plug-in. thunder_display_init fills in 'state' and the functions; the VM then calls
 * open once, present and poll per frame, and close at the end. Every call comes from the one
 * thread that runs the program, so a plug-in needs no locking. */
typedef struct ThunderDisplay
{
    uint32_t abi_version;
    void* state; /* the plug-in's own data; Thunder never looks at it */

    /* Opens the window. Returns non-zero on success; on failure 'error' may be set to a
     * message, which the VM reports. Called once, before the first frame. */
    int (*open)(struct ThunderDisplay* display, int32_t width, int32_t height, const char* title,
        const char** error);

    /* Draws a frame. */
    void (*present)(struct ThunderDisplay* display, const ThunderFrame* frame);

    /* Fills 'event' with the next event and returns non-zero; returns 0 when there are none
     * left. The VM drains this after every frame. */
    int (*poll)(struct ThunderDisplay* display, ThunderEvent* event);

    /* Closes the window and frees what thunder_display_init allocated. Called exactly once,
     * when the program ends - also when open was never called or failed, so it must cope with
     * a state that was never finished. */
    void (*close)(struct ThunderDisplay* display);
} ThunderDisplay;

/* Every display plug-in defines this. Return non-zero on success. */
THUNDER_DISPLAY_EXPORT int thunder_display_init(ThunderDisplay* display);
typedef int (*ThunderDisplayInit)(ThunderDisplay* display);

#ifdef __cplusplus
}
#endif

#endif /* THUNDER_DISPLAY_H */
