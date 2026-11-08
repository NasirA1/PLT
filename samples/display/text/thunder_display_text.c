/* The smallest possible Thunder display: it writes each frame to standard error as text.
 *
 * There is no window and nothing to install, so this is the plug-in to read first and the one
 * to copy when writing your own. The SDL version next door does the same work with pixels.
 *
 *     thundervm --display text 25_bouncing_ball.thc
 *
 * Three environment variables make it useful for trying a game out without a window:
 *
 *     THUNDER_DISPLAY_TEXT_FRAMES=20        stop the program after 20 frames
 *     THUNDER_DISPLAY_TEXT_KEYS=left,space  press (and release) these keys at the first frame
 *     THUNDER_DISPLAY_TEXT_MOUSE=120,45,1   put the mouse there, with the button held down
 *     THUNDER_DISPLAY_TEXT_QUIET=1          do all of that, but print nothing
 *
 * Build (Linux/macOS):
 *     cc -std=c11 -shared -fPIC -I<thunder>/include -o libthunder_display_text.so \
 *        thunder_display_text.c
 */

#include <thunder/display.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXT_MAX_KEYS 16

typedef struct TextState
{
    int32_t width;
    int32_t height;
    uint64_t frames;
    int quit_after;     /* THUNDER_DISPLAY_TEXT_FRAMES: stop after this many (0: never) */
    int quit_reported;
    char keys[TEXT_MAX_KEYS][16]; /* THUNDER_DISPLAY_TEXT_KEYS, split on commas */
    int key_count;
    int keys_sent;      /* how many of the scripted events have been handed over */
    int mouse_x, mouse_y, mouse_button;
    int has_mouse;
    int mouse_sent;
    int quiet;          /* THUNDER_DISPLAY_TEXT_QUIET: work as usual, but print nothing */
} TextState;

/* Splits "left,space" into the key list. */
static void read_keys(TextState* state, const char* list)
{
    size_t length = 0;
    if (list == NULL)
    {
        return;
    }
    while (*list != 0 && state->key_count < TEXT_MAX_KEYS)
    {
        if (*list == ',')
        {
            if (length > 0)
            {
                state->keys[state->key_count++][length] = 0;
                length = 0;
            }
        }
        else if (length + 1 < sizeof state->keys[0])
        {
            state->keys[state->key_count][length++] = *list;
        }
        ++list;
    }
    if (length > 0 && state->key_count < TEXT_MAX_KEYS)
    {
        state->keys[state->key_count++][length] = 0;
    }
}

static int text_open(ThunderDisplay* display, int32_t width, int32_t height, const char* title,
    const char** error)
{
    TextState* state = (TextState*)display->state;
    (void)error;
    state->width = width;
    state->height = height;
    if (!state->quiet)
    {
        fprintf(stderr, "[display] \"%s\" %dx%d\n", title != NULL ? title : "", width, height);
    }
    return 1;
}

static void text_present(ThunderDisplay* display, const ThunderFrame* frame)
{
    TextState* state = (TextState*)display->state;
    int32_t index;
    state->frames = frame->number;
    if (state->quiet)
    {
        return;
    }
    fprintf(stderr, "[display] frame %llu: keeps %d, adds %d\n", (unsigned long long)frame->number,
        frame->keep, frame->count);
    for (index = 0; index < frame->count; ++index)
    {
        const ThunderShape* shape = &frame->shapes[index];
        fprintf(stderr, "    %-14s %8.3f %8.3f %8.3f %8.3f  %-7s %s\n", shape->kind, shape->a, shape->b,
            shape->c, shape->d, shape->color, shape->text);
    }
    fflush(stderr);
}

/* There is no window, so the events are the scripted ones: the keys and the mouse position
 * given in the environment, and finally "the user has had enough" after the frame limit. */
static int text_poll(ThunderDisplay* display, ThunderEvent* event)
{
    TextState* state = (TextState*)display->state;
    memset(event, 0, sizeof *event);
    if (state->has_mouse && !state->mouse_sent)
    {
        state->mouse_sent = 1;
        event->kind = THUNDER_EVENT_MOUSE;
        event->x = state->mouse_x;
        event->y = state->mouse_y;
        event->button = state->mouse_button;
        return 1;
    }
    /* Each key is pressed, then released: key_down() is true for exactly one frame. */
    if (state->keys_sent < state->key_count * 2)
    {
        const int index = state->keys_sent / 2;
        const int down = state->keys_sent % 2 == 0;
        ++state->keys_sent;
        event->kind = down ? THUNDER_EVENT_KEY_DOWN : THUNDER_EVENT_KEY_UP;
        event->key = state->keys[index];
        return 1;
    }
    if (state->quit_after > 0 && !state->quit_reported && state->frames >= (uint64_t)state->quit_after)
    {
        state->quit_reported = 1;
        event->kind = THUNDER_EVENT_QUIT;
        return 1;
    }
    return 0;
}

static void text_close(ThunderDisplay* display)
{
    TextState* state = (TextState*)display->state;
    if (state != NULL && !state->quiet)
    {
        fprintf(stderr, "[display] closed after %llu frame(s)\n", (unsigned long long)state->frames);
        fflush(stderr);
    }
    free(state);
    display->state = NULL;
}

THUNDER_DISPLAY_EXPORT int thunder_display_init(ThunderDisplay* display)
{
    TextState* state = (TextState*)calloc(1, sizeof(TextState));
    const char* limit = getenv("THUNDER_DISPLAY_TEXT_FRAMES");
    const char* mouse = getenv("THUNDER_DISPLAY_TEXT_MOUSE");
    if (state == NULL)
    {
        return 0;
    }
    state->quit_after = limit != NULL ? atoi(limit) : 0;
    state->quiet = getenv("THUNDER_DISPLAY_TEXT_QUIET") != NULL;
    read_keys(state, getenv("THUNDER_DISPLAY_TEXT_KEYS"));
    if (mouse != NULL && sscanf(mouse, "%d,%d,%d", &state->mouse_x, &state->mouse_y, &state->mouse_button) == 3)
    {
        state->has_mouse = 1;
    }
    display->abi_version = THUNDER_DISPLAY_ABI;
    display->state = state;
    display->open = text_open;
    display->present = text_present;
    display->poll = text_poll;
    display->close = text_close;
    return 1;
}
