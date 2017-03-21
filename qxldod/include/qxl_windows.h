#ifndef _H_QXL_WINDOWS
#define _H_QXL_WINDOWS

enum {
    QXL_ESCAPE_SET_CUSTOM_DISPLAY = 0x10001,
    QXL_ESCAPE_MONITOR_CONFIG
};

typedef struct QXLEscapeSetCustomDisplay {
    uint32_t xres;
    uint32_t yres;
    uint32_t bpp;
} QXLEscapeSetCustomDisplay;

#endif /* _H_QXL_WINDOWS */
