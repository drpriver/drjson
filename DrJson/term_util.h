//
// Copyright © 2022, David Priver
//
#ifndef TERM_UTIL_H
#define TERM_UTIL_H
#ifdef _WIN32
#ifndef _CRT_NONSTDC_NO_DEPRECATE
#define _CRT_NONSTDC_NO_DEPRECATE
#endif
#endif
#include <stdlib.h>

typedef struct TermSize {
    int columns, rows;
} TermSize;
//
// Returns the size of the terminal.
// On error, we return 80 columns and 24 rows.
//
static inline TermSize get_terminal_size(void);

#ifdef _WIN32

#include <io.h>
#include <stdio.h>
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
static inline
int fileno(FILE* stream){
    return _fileno(stream);
}
static inline
int isatty(int fd){
    return _isatty(fd);
}

#include "windowsheader.h"
static inline
TermSize
get_terminal_size(void){
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    BOOL success = GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    if(success){
        int columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        int rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        return (TermSize){columns, rows};
    }
    return (TermSize){80, 24};
}
#elif defined(WASM)
static inline
TermSize
get_terminal_size(void){
    return (TermSize){80, 24};
}
#else
#include <unistd.h>
#include <sys/ioctl.h>

static inline
TermSize
get_terminal_size(void){
    struct TermSize result = {80,24};
    struct winsize w;
    int err = ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    if(err == -1){
        char* cols_s = getenv("COLUMNS");
        if(!cols_s)
            goto err;
        char* rows_s = getenv("ROWS");
        if(!rows_s)
            goto err;
        int cols = atoi(cols_s);
        if(!cols)
            goto err;
        int rows = atoi(rows_s);
        if(!rows)
            goto err;
        result = (TermSize){cols, rows};
    }
    else {
        if(!w.ws_col || !w.ws_row)
            goto err;
        result = (TermSize){w.ws_col, w.ws_row};
    }
    err:
    return result;
}
#endif


#endif
