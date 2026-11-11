/* A Thunder display for Windows, drawn with GDI: a real window with nothing to install.
 *
 *     thundervm --display win32 28_snake.thc
 *     28_snake.exe                           (a packaged program picks it up by itself)
 *
 * Every Windows has GDI, so this is the plug-in the toolchain always builds on Windows and the
 * one a packaged graphics program is given when SDL2 is not around. It implements the whole of
 * <thunder/display.h>: open a window, draw each frame's shapes and send key and mouse events
 * back to the program. Text is drawn with a real font, so Pashto labels come out shaped.
 *
 * The picture lives in a memory bitmap that is kept between frames: a frame that only adds
 * shapes draws only those (the "keep" count of a frame), and WM_PAINT just copies the bitmap.
 *
 * Build (Developer Command Prompt):
 *     cl /W4 /LD /I<thunder>\include thunder_display_win32.c user32.lib gdi32.lib ^
 *        /Fe:thunder_display_win32.dll
 */

#include <thunder/display.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_PENDING_MAX 64
#define WIN32_KEY_TEXT_MAX 16

typedef struct Win32State
{
    HWND window;
    HDC memory;             /* the picture, kept between frames */
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    int32_t width;
    int32_t height;
    int quit;               /* the window was closed */
    ThunderEvent pending[WIN32_PENDING_MAX];
    char key_text[WIN32_PENDING_MAX][WIN32_KEY_TEXT_MAX]; /* the key names the events point at */
    int pending_count;
    int pending_read;
    int mouse_down;
    int mouse_x, mouse_y;
    char error[256];
} Win32State;

static const wchar_t* const WINDOW_CLASS = L"ThunderDisplayWin32";

/* ------------------------------------------------------------------ colors */

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return 0;
}

/* Thunder always sends "#rrggbb". */
static COLORREF color_of(const char* color)
{
    int rgb[3] = {0, 0, 0};
    int index;
    if (color != NULL && strlen(color) == 7 && color[0] == '#')
    {
        for (index = 0; index < 3; ++index)
        {
            rgb[index] = hex_digit(color[1 + index * 2]) * 16 + hex_digit(color[2 + index * 2]);
        }
    }
    return RGB(rgb[0], rgb[1], rgb[2]);
}

/* ------------------------------------------------------------------ events */

static ThunderEvent* next_slot(Win32State* state)
{
    if (state->pending_count >= WIN32_PENDING_MAX)
    {
        return NULL;
    }
    memset(&state->pending[state->pending_count], 0, sizeof state->pending[0]);
    return &state->pending[state->pending_count++];
}

static void push_key(Win32State* state, int down, const char* name)
{
    ThunderEvent* slot = next_slot(state);
    char* text;
    if (slot == NULL || name == NULL || name[0] == 0)
    {
        return;
    }
    text = state->key_text[slot - state->pending];
    strncpy(text, name, WIN32_KEY_TEXT_MAX - 1);
    text[WIN32_KEY_TEXT_MAX - 1] = 0;
    slot->kind = down ? THUNDER_EVENT_KEY_DOWN : THUNDER_EVENT_KEY_UP;
    slot->key = text;
}

static void push_mouse(Win32State* state)
{
    ThunderEvent* slot = next_slot(state);
    if (slot == NULL)
    {
        return;
    }
    slot->kind = THUNDER_EVENT_MOUSE;
    slot->x = state->mouse_x;
    slot->y = state->mouse_y;
    slot->button = state->mouse_down;
}

/* Virtual-key codes into Thunder's key names (see display.h): letters and digits are
 * themselves, in lower case; anything Thunder has no name for is dropped. */
static const char* key_name(WPARAM key, char* buffer)
{
    switch (key)
    {
    case VK_LEFT: return "left";
    case VK_RIGHT: return "right";
    case VK_UP: return "up";
    case VK_DOWN: return "down";
    case VK_SPACE: return "space";
    case VK_RETURN: return "enter";
    case VK_ESCAPE: return "escape";
    case VK_BACK: return "backspace";
    case VK_TAB: return "tab";
    default:
        break;
    }
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))
    {
        buffer[0] = (char)(key >= 'A' ? key - 'A' + 'a' : key);
        buffer[1] = 0;
        return buffer;
    }
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9)
    {
        buffer[0] = (char)('0' + (key - VK_NUMPAD0));
        buffer[1] = 0;
        return buffer;
    }
    return NULL;
}

static LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    Win32State* state = (Win32State*)GetWindowLongPtrW(window, GWLP_USERDATA);
    char buffer[2];
    switch (message)
    {
    case WM_CREATE:
    {
        CREATESTRUCTW* created = (CREATESTRUCTW*)lparam;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)created->lpCreateParams);
        return 0;
    }
    case WM_CLOSE:
        if (state != NULL)
        {
            ThunderEvent* slot = next_slot(state);
            state->quit = 1;
            if (slot != NULL)
            {
                slot->kind = THUNDER_EVENT_QUIT;
            }
        }
        return 0; /* the VM ends the program; close() destroys the window */
    case WM_PAINT:
    {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        if (state != NULL && state->memory != NULL)
        {
            BitBlt(dc, 0, 0, state->width, state->height, state->memory, 0, 0, SRCCOPY);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1; /* the bitmap covers the whole client area */
    case WM_KEYDOWN:
    case WM_KEYUP:
        if (state != NULL && !(message == WM_KEYDOWN && (lparam & (1 << 30)))) /* no auto-repeat */
        {
            push_key(state, message == WM_KEYDOWN, key_name(wparam, buffer));
        }
        return 0;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        if (state != NULL)
        {
            state->mouse_x = GET_X_LPARAM(lparam);
            state->mouse_y = GET_Y_LPARAM(lparam);
            if (message == WM_LBUTTONDOWN)
            {
                state->mouse_down = 1;
                SetCapture(window);
            }
            else if (message == WM_LBUTTONUP)
            {
                state->mouse_down = 0;
                ReleaseCapture();
            }
            push_mouse(state);
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static void pump_messages(Win32State* state)
{
    MSG message;
    if (state->window == NULL)
    {
        return;
    }
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

/* ------------------------------------------------------------------ shapes */

static void draw_text(HDC dc, double x, double y, double size, const char* text, COLORREF color)
{
    HFONT font;
    HFONT old;
    int height = (int)(size + 0.5);
    int wide_length;
    wchar_t* wide;
    if (text == NULL || text[0] == 0)
    {
        return;
    }
    if (height < 4)
    {
        height = 4;
    }
    /* The text is UTF-8 (possibly Pashto); GDI wants UTF-16. */
    wide_length = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (wide_length <= 0)
    {
        return;
    }
    wide = (wchar_t*)malloc((size_t)wide_length * sizeof(wchar_t));
    if (wide == NULL)
    {
        return;
    }
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, wide_length);
    /* The size is the text height in pixels, so the font's cell height is negative size. */
    font = CreateFontW(-height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    old = (HFONT)SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    TextOutW(dc, (int)x, (int)y, wide, wide_length - 1);
    SelectObject(dc, old);
    DeleteObject(font);
    free(wide);
}

static void draw_shape(Win32State* state, const ThunderShape* shape)
{
    HDC dc = state->memory;
    const COLORREF color = color_of(shape->color);
    HBRUSH brush = CreateSolidBrush(color);
    const int width = shape->size < 1 ? 1 : (int)(shape->size + 0.5);
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    if (strcmp(shape->kind, "clear") == 0)
    {
        RECT all = {0, 0, state->width, state->height};
        FillRect(dc, &all, brush);
    }
    else if (strcmp(shape->kind, "pixel") == 0)
    {
        SetPixelV(dc, (int)shape->a, (int)shape->b, color);
    }
    else if (strcmp(shape->kind, "line") == 0)
    {
        MoveToEx(dc, (int)shape->a, (int)shape->b, NULL);
        LineTo(dc, (int)shape->c, (int)shape->d);
    }
    else if (strcmp(shape->kind, "rect") == 0)
    {
        RECT box = {(int)shape->a, (int)shape->b, (int)(shape->a + shape->c), (int)(shape->b + shape->d)};
        FillRect(dc, &box, brush);
    }
    else if (strcmp(shape->kind, "rect_outline") == 0)
    {
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, (int)shape->a, (int)shape->b, (int)(shape->a + shape->c), (int)(shape->b + shape->d));
    }
    else if (strcmp(shape->kind, "circle") == 0)
    {
        SelectObject(dc, GetStockObject(NULL_PEN));
        /* Ellipse leaves out the right and bottom edge: widen by one so the radius is kept. */
        Ellipse(dc, (int)(shape->a - shape->c), (int)(shape->b - shape->c), (int)(shape->a + shape->c) + 1,
            (int)(shape->b + shape->c) + 1);
    }
    else if (strcmp(shape->kind, "circle_outline") == 0)
    {
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, (int)(shape->a - shape->c), (int)(shape->b - shape->c), (int)(shape->a + shape->c) + 1,
            (int)(shape->b + shape->c) + 1);
    }
    else if (strcmp(shape->kind, "text") == 0)
    {
        draw_text(dc, shape->a, shape->b, shape->size, shape->text, color);
    }
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

/* ------------------------------------------------------------------ the interface */

static int win32_open(ThunderDisplay* display, int32_t width, int32_t height, const char* title, const char** error)
{
    Win32State* state = (Win32State*)display->state;
    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSW window_class;
    RECT frame = {0, 0, width, height};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    wchar_t wide_title[256];
    HDC screen;
    RECT all = {0, 0, width, height};

    memset(&window_class, 0, sizeof window_class);
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.lpszClassName = WINDOW_CLASS;
    /* Registering twice (a second display in one process) just fails harmlessly. */
    RegisterClassW(&window_class);

    if (title == NULL || MultiByteToWideChar(CP_UTF8, 0, title, -1, wide_title, 256) <= 0)
    {
        wcscpy(wide_title, L"Thunder");
    }
    /* The client area, not the whole window, must be the canvas size. */
    AdjustWindowRect(&frame, style, FALSE);
    state->window = CreateWindowW(WINDOW_CLASS, wide_title, style, CW_USEDEFAULT, CW_USEDEFAULT,
        frame.right - frame.left, frame.bottom - frame.top, NULL, NULL, instance, state);
    if (state->window == NULL)
    {
        strncpy(state->error, "CreateWindow failed", sizeof state->error - 1);
        *error = state->error;
        return 0;
    }
    state->width = width;
    state->height = height;

    screen = GetDC(state->window);
    state->memory = CreateCompatibleDC(screen);
    state->bitmap = CreateCompatibleBitmap(screen, width, height);
    ReleaseDC(state->window, screen);
    if (state->memory == NULL || state->bitmap == NULL)
    {
        strncpy(state->error, "cannot create the drawing surface", sizeof state->error - 1);
        *error = state->error;
        return 0;
    }
    state->old_bitmap = (HBITMAP)SelectObject(state->memory, state->bitmap);
    FillRect(state->memory, &all, (HBRUSH)GetStockObject(WHITE_BRUSH));

    ShowWindow(state->window, SW_SHOWNORMAL);
    UpdateWindow(state->window);
    SetForegroundWindow(state->window);
    pump_messages(state);
    return 1;
}

static void win32_present(ThunderDisplay* display, const ThunderFrame* frame)
{
    Win32State* state = (Win32State*)display->state;
    int32_t index;
    HDC dc;
    if (state->window == NULL || state->memory == NULL)
    {
        return;
    }
    for (index = 0; index < frame->count; ++index)
    {
        draw_shape(state, &frame->shapes[index]);
    }
    dc = GetDC(state->window);
    if (dc != NULL)
    {
        BitBlt(dc, 0, 0, state->width, state->height, state->memory, 0, 0, SRCCOPY);
        ReleaseDC(state->window, dc);
    }
    pump_messages(state);
}

static int win32_poll(ThunderDisplay* display, ThunderEvent* event)
{
    Win32State* state = (Win32State*)display->state;
    if (state->pending_read >= state->pending_count)
    {
        state->pending_count = 0;
        state->pending_read = 0;
        pump_messages(state);
    }
    if (state->pending_read < state->pending_count)
    {
        *event = state->pending[state->pending_read++];
        return 1;
    }
    return 0;
}

static void win32_close(ThunderDisplay* display)
{
    Win32State* state = (Win32State*)display->state;
    if (state == NULL)
    {
        return;
    }
    if (state->memory != NULL)
    {
        if (state->old_bitmap != NULL)
        {
            SelectObject(state->memory, state->old_bitmap);
        }
        DeleteDC(state->memory);
    }
    if (state->bitmap != NULL)
    {
        DeleteObject(state->bitmap);
    }
    if (state->window != NULL)
    {
        DestroyWindow(state->window);
        pump_messages(state);
    }
    free(state);
    display->state = NULL;
}

THUNDER_DISPLAY_EXPORT int thunder_display_init(ThunderDisplay* display)
{
    Win32State* state = (Win32State*)calloc(1, sizeof(Win32State));
    if (state == NULL)
    {
        return 0;
    }
    display->abi_version = THUNDER_DISPLAY_ABI;
    display->state = state;
    display->open = win32_open;
    display->present = win32_present;
    display->poll = win32_poll;
    display->close = win32_close;
    return 1;
}
