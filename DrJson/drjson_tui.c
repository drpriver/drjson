//
// Copyright © 2022-2024, David Priver <david@davidpriver.com>
//
#ifdef _WIN32
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include <stdarg.h>

#ifdef _WIN32
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _CRT_NONSTDC_NO_DEPRECATE
#define _CRT_NONSTDC_NO_DEPRECATE
#endif
#define WIN32_EXTRA_LEAN
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
typedef long long ssize_t;
#else
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#endif
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#define DRJSON_API static inline
#include "drjson.h"
// "drjson.c" is #included at the bottom
#include "argument_parsing.h"
#include "term_util.h"
#include "drt.h"
#include "drt.c"

#ifndef force_inline
#if defined(__GNUC__) || defined(__clang__)
#define force_inline static inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define force_inline static inline __forceinline
#else
#define force_inline static inline
#endif
#endif

// Chose to use libc's FILE api instead of OS APIs for portability.

force_inline
warn_unused
long long
file_size_from_fp(FILE* fp){
    long long result = 0;
    // sadly, the only way in standard c to do this.
    if(fseek(fp, 0, SEEK_END))
        goto errored;
    result = ftell(fp);
    if(result < 0)
        goto errored;
    if(fseek(fp, 0, SEEK_SET))
        goto errored;
    return result;

    errored:
    return -1;
}



static inline
LongString
read_file_streamed(FILE* fp){
    size_t nalloced = 1024;
    size_t used = 0;
    char* buff = malloc(nalloced);
    for(;;){
        size_t remainder = nalloced - used;
        size_t nread = fread(buff+used, 1, remainder, fp);
        if(nread == remainder){
            nalloced *= 2;
            char* newbuff = realloc(buff, nalloced);
            if(!newbuff){
                free(buff);
                return (LongString){0};
            }
            buff = newbuff;
        }
        used += nread;
        if(nread != remainder){
            if(feof(fp)) break;
            else{
                free(buff);
                return (LongString){0};
            }
        }
    }
    buff = realloc(buff, used+1);
    buff[used] = 0;
    return (LongString){used, buff};
}

static inline
LongString 
read_file(const char* filepath){
    LongString result = {0};
    FILE* fp = fopen(filepath, "rb");
    if(!fp) return result;
    long long size = file_size_from_fp(fp);
    if(size <= 0){
        result = read_file_streamed(fp);
        goto finally;
    }
    size_t nbytes = size;
    char* text = malloc(nbytes+1);
    if(!text) goto finally;
    size_t fread_result = fread(text, 1, nbytes, fp);
    if(fread_result != nbytes){
        free(text);
        goto finally;
    }
    text[nbytes] = '\0';
    result.text = text;
    result.length = nbytes;
finally:
    fclose(fp);
    return result;
}

enum {
    CTRL_A = 1,         // Ctrl-a
    CTRL_B = 2,         // Ctrl-b
    CTRL_C = 3,         // Ctrl-c
    CTRL_D = 4,         // Ctrl-d
    CTRL_E = 5,         // Ctrl-e
    CTRL_F = 6,         // Ctrl-f
    // CTRL_G = 7,      // unused
    CTRL_H = 8,         // Ctrl-h
    TAB    = 9,         // Tab / Ctrl-i
    CTRL_J = 10,        // Accept
    CTRL_K = 11,        // Ctrl-k
    CTRL_L = 12,        // Ctrl-l
    ENTER = 13,         // Enter / Ctrl-m
    CTRL_N = 14,        // Ctrl-n
    CTRL_O = 15,        // Ctrl-o
    CTRL_P = 16,        // Ctrl-p
    // CTRL_Q = 17,
    CTRL_R = 18,
    // CTRL_S = 19,
    CTRL_T = 20,        // Ctrl-t
    CTRL_U = 21,        // Ctrl-u
    CTRL_V = 22,        // Ctrl-v
    CTRL_W = 23,        // Ctrl-w
    // CTRL_X = 24,
    // CTRL_Y = 25,
    CTRL_Z = 26,        // Ctrl-z
    ESC = 27,           // Escape
    BACKSPACE =  127,   // Backspace
    // fake key codes
    #undef DELETE
    DELETE    = -1,
    UP        = -2,
    DOWN      = -3,
    LEFT      = -4,
    RIGHT     = -5,
    HOME      = -6,
    END       = -7,
    SHIFT_TAB = -8,
    PAGE_UP   = -9,
    PAGE_DOWN = -10,
    LCLICK_DOWN = -11,
    LCLICK_UP = -12,
    LDRAG = -13,
};
typedef struct TermState TermState;
struct TermState {
#ifdef _WIN32
    char pad;
#else
    struct termios raw;
    struct termios orig;
#endif
};

enum {
    DRAW_NONE    = 0x0,
    DRAW_HEADERS = 0x1,
    DRAW_LINES   = 0x2,
    DRAW_CELLS   = 0x4,
};
static struct {
    TermState TS;
    #ifdef _WIN32
    HANDLE STDIN, STDOUT;
    #endif
    int needs_recalc, needs_rescale, needs_redisplay;
    int screenw, screenh;
    Drt drt;
} globals = {
    .needs_recalc = 1, .needs_rescale = 1, .needs_redisplay = 1,
};

static
void
enable_raw(TermState* ts){
#ifdef _WIN32
    (void)ts;
#else
    if(tcgetattr(STDIN_FILENO, &ts->orig) == -1)
        return;
    ts->raw = ts->orig;
    ts->raw.c_iflag &= ~(0lu
            | BRKINT // no break
            | ICRNL  // don't map CR to NL
            | INPCK  // skip parity check
            | ISTRIP // don't strip 8th bit off char
            | IXON   // don't allow start/stop input control
            );
    ts->raw.c_oflag &= ~(0lu
        | OPOST // disable post processing
        );
    ts->raw.c_cflag |= CS8; // 8 bit chars
    ts->raw.c_lflag &= ~(0lu
            | ECHO    // disable echo
            | ICANON  // disable canonical processing
            | IEXTEN  // no extended functions
            // Currently allowing these so ^Z works, could disable them
            | ISIG    // disable signals
            );
    ts->raw.c_cc[VMIN] = 1; // read every single byte
    ts->raw.c_cc[VTIME] = 0; // no timeout

    // set and flush
    // Change will ocurr after all output has been transmitted.
    // Unread input will be discarded.
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &ts->raw) < 0)
        return;
#endif
}

static
void
disable_raw(struct TermState*ts){
#ifdef _WIN32
    (void)ts;
#else
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &ts->orig);
#endif
}

static inline
ssize_t
read_one(char* buff, _Bool block){
#ifdef _WIN32
    for(;;){
        INPUT_RECORD record;
        DWORD num_read = 0;
        if(!block){
            DWORD timeout = 0;
            DWORD ev = WaitForSingleObject(globals.STDIN, timeout);
            if(ev == WAIT_TIMEOUT){
                // LOG("WAIT_TIMEOUT\n");
                *buff = 0;
                return 0;
            }
            if(ev == WAIT_FAILED)
                return -1;
            if(ev != WAIT_OBJECT_0){
                if(0)LOG("%d WaitForSingleObject returned value other than TIMEOUT, OBJECT_FAILED OR OBJECT_O: %lu\n", __LINE__, ev);
                continue;
            }
        }
        // LOG("ReadConsoleInput\n");
        BOOL b = ReadConsoleInput(globals.STDIN, &record, 1, &num_read);
        // LOG("DoneReadConsoleInput\n");
        if(!b) return -1;
        if(record.EventType == WINDOW_BUFFER_SIZE_EVENT){
            globals.needs_rescale = 1;
            if(!block) continue;
            *buff = 0;
            if(0)LOG("window size event: %hd, %hd", record.Event.WindowBufferSizeEvent.dwSize.X, record.Event.WindowBufferSizeEvent.dwSize.Y);
            return 0;
        }
        if(record.EventType != KEY_EVENT){
            // LOG("record.EventType: %d\n", (int)record.EventType);
            continue;
        }
        if(!record.Event.KeyEvent.bKeyDown){
            // LOG("!record.Event.KeyEvent.bKeyDown\n");
            continue;
        }
        *buff = record.Event.KeyEvent.uChar.AsciiChar;
        return 1;
    }
#else
    if(block){
        ssize_t e;
        for(;;){
            e = read(STDIN_FILENO, buff, 1);
            if(e == -1 && errno == EINTR) {
                if(globals.needs_rescale){
                    *buff = 0;
                    e = 0;
                }
                else {
                    // LOG("EINTR\n");
                    continue;
                }
            }
            if(e == -1){
                // LOG("read_one errno: %d\n", errno);
            }
            break;
        }
        return e;
    }
    else {
        ssize_t e;
        int flags = fcntl(STDIN_FILENO, F_GETFL);
        int new_flags = flags | O_NONBLOCK;
        fcntl(STDIN_FILENO, F_SETFL, new_flags);

        for(;;){
            e = read(STDIN_FILENO, buff, 1);
            if(e == -1 && errno == EINTR) continue;
            if(e == -1 && errno == EWOULDBLOCK) {
                *buff = 0;
                e = 0;
            }
            if(e == -1){
                // LOG("read_one errno: %d\n", errno);
            }
            break;
        }
        fcntl(STDIN_FILENO, F_SETFL, flags);
        return e;
    }
#endif
}

static inline
ssize_t
read_one_nb(char* buff){
    return read_one(buff, /*block=*/0);
}

static inline
ssize_t
read_one_b(char* buff){
    return read_one(buff, /*block=*/1);
}


static
void
end_tui(void){
    disable_raw(&globals.TS);
    // show cursor
    printf("\033[?25h");
    fflush(stdout);
    // disable alt buffer
    printf("\033[?1049l");
    fflush(stdout);
    // Normal tracking mode?
    printf("\033[?1006;1002l");
    // enable line wrapping
    printf("\033[=7h");
    fflush(stdout);
}

static
void
begin_tui(void){
#ifdef _WIN32
    SetConsoleCP(65001);
    SetConsoleMode(globals.STDIN, ENABLE_VIRTUAL_TERMINAL_INPUT);
    SetConsoleMode(globals.STDOUT, ENABLE_PROCESSED_OUTPUT|ENABLE_WRAP_AT_EOL_OUTPUT|ENABLE_VIRTUAL_TERMINAL_PROCESSING|DISABLE_NEWLINE_AUTO_RETURN);
#endif
    // alternative buffer
    printf("\033[?1049h");
    fflush(stdout);
    // hide cursor
    printf("\033[?25l");
    fflush(stdout);
    // X11 Mouse Reporting
    // See https://invisible-island.net/xterm/ctlseqs/ctlseqs.html#h2-Mouse-Tracking
    printf("\033[?1006;1002h");
    // line wrapping
    printf("\033[=7l");
    fflush(stdout);
    enable_raw(&globals.TS);
#ifdef _WIN32
    SetConsoleCP(65001);
    SetConsoleMode(globals.STDIN, ENABLE_VIRTUAL_TERMINAL_INPUT);
    SetConsoleMode(globals.STDOUT, ENABLE_PROCESSED_OUTPUT|ENABLE_WRAP_AT_EOL_OUTPUT|ENABLE_VIRTUAL_TERMINAL_PROCESSING|DISABLE_NEWLINE_AUTO_RETURN);
#endif
}

#ifdef _WIN32
#else
static
void
sighandler(int sig){
    if(sig == SIGWINCH){
        // LOG("SIGWINCH\n");
        globals.needs_rescale = 1;
        return;
    }
    if(sig == SIGCONT){
        globals.needs_rescale = 1;
        return;
    }
}
#endif

static
int
get_input(int* pc, int* pcx, int* pcy, int* pmagnitude){
    char _c;
    char sequence[32] = {0};
    ssize_t nread = read_one_b(&_c);
    int c = (int)(unsigned char)_c;
    if(nread < 0)
        return -1;
    if(!nread) return 0;
    if(c > 127){ // utf-8 sequence
        sequence[0] = (char)c;
        int length;
        if((c & 0xe0) == 0xc0)
            length = 2;
        else if((c & 0xf0) == 0xe0)
            length = 3;
        else if((c & 0xf8) == 0xf0)
            length = 4;
        else {
            // if(0)LOG("invalid utf-8 starter? %#x\n", c);
            // invalid sequence
             return 0;
        }
        ssize_t e = 0;
        for(int i = 1; i < length; i++){
            e = read_one_nb(sequence+i);
            if(e == -1) return -1;
            int val = (int)(unsigned char)sequence[i];
            if(val <= 127){
                // if(0)LOG("val: %d\n", val);
                // if(0)LOG("invalid utf-8? '%.*s'\n", i+1, sequence);
                break;
            }
        }
        for(int i = 0; i < length; i++){
            // if(0)LOG("seq[%d] = %x\n", i, (int)(unsigned char)sequence[i]);
        }
        // if(0)LOG("utf-8: %.*s\n", length, sequence);
        return 0;
    }
    int cx = 0, cy = 0;
    int magnitude = 1;
    if(c == ESC){
        if(read_one_nb(sequence) == -1) return -1;
        if(read_one_nb(sequence+1) == -1) return -1;
        // if(0)LOG("ESC %d %d\n", sequence[0], sequence[1]);
        if(sequence[0] == '['){
            if(sequence[1] == '<'){
                // See https://invisible-island.net/xterm/ctlseqs/ctlseqs.html#h3-Extended-coordinates
                int i;
                int mb = 0;
                for(i = 2; i < sizeof sequence; i++){
                    if(read_one_nb(sequence+i) == -1) return -1;
                    if(sequence[i] == 0) return 0; // unexpected end of escape sequence
                    if(sequence[i] == ';') break;
                    if(sequence[i] < '0' || sequence[i] > '9') return 0; // out of range, should be decimal
                    mb *= 10;
                    mb += sequence[i] - '0';
                }
                int x = 0;
                for(; i < sizeof sequence; i++){
                    if(read_one_nb(sequence+i) == -1) return -1;
                    if(sequence[i] == 0) return 0; // unexpected end of escape sequence
                    if(sequence[i] == ';') break;
                    if(sequence[i] < '0' || sequence[i] > '9') return 0; // out of range, should be decimal
                    x *= 10;
                    x += sequence[i] - '0';
                }
                int y = 0;
                for(; i < sizeof sequence; i++){
                    if(read_one_nb(sequence+i) == -1) return -1;
                    if(sequence[i] == 0) return 0; // unexpected end of escape sequence
                    if(sequence[i] == 'm' || sequence[i] == 'M') break;
                    if(sequence[i] < '0' || sequence[i] > '9') return 0; // out of range, should be decimal
                    y *= 10;
                    y += sequence[i] - '0';
                }
                _Bool up = sequence[i] == 'm';
                if(0){
                // LOG("ESC [ <");
                // for(int s = 2; s <= i; s++)
                    // LOG(" %d", sequence[s]);
                // LOG("\n");
                // LOG("mb: %d\n", mb);
                // LOG("x: %d\n", x);
                // LOG("y: %d\n", y);
                // LOG("up: %s\n", up?"true":"false");
                }
                cx = x -1;
                cy = y -1;
                switch(mb){
                    case 0: c = up?LCLICK_UP:LCLICK_DOWN; break;
                    case 32: c = LDRAG; break;
                    case 64: c = UP; magnitude = 3; break;
                    case 65: c = DOWN; magnitude = 3; break;
                    default: break; // LOG("Unknown mb: %d\n", mb);
                }
            }
            else if(sequence[1] == 'M'){
                if(read_one_nb(sequence+2) == -1) return -1;
                if(read_one_nb(sequence+3) == -1) return -1;
                // LOG("click?\n");
                // if(0)LOG("ESC [ M %d %d %d\n", sequence[2], sequence[3], sequence[4]);
                cx = ((int)(unsigned char)sequence[3]) - 32-1;
                cy = ((int)(unsigned char)sequence[4]) - 32-1;
                // LOG("x, y: %d,%d\n", cx, cy);
                switch(sequence[2]){
                    case 32:
                        c = LCLICK_DOWN;
                        break;
                    case 35:
                        c = LCLICK_UP;
                        break;
                    case 96:
                        c = UP;
                        magnitude = 3;
                        break;
                    case 97:
                        c = DOWN;
                        magnitude = 3;
                        break;
                }
            }
            else if (sequence[1] >= '0' && sequence[1] <= '9'){
                // Extended escape, read additional byte.
                if (read_one_nb(sequence+2) == -1) return -1;
                if (sequence[2] == '~') {
                    switch(sequence[1]) {
                    case '1':
                        c = HOME;
                        break;
                    // 2 is INSERT, which idk what that would do.
                    case '3': /* Delete key. */
                        c = DELETE;
                        break;
                    case '4':
                        c = END;
                        break;
                    case '5':
                        c = PAGE_UP;
                        break;
                    case '6':
                        c = PAGE_DOWN;
                        break;
                    case '7':
                        c = HOME;
                        break;
                    case '8':
                        c = END;
                        break;
                    // Maybe we want to use f-keys later?
                    // 10 F0
                    // 11 F1
                    // 12 F2
                    // 13 F3
                    // 14 F4
                    // 15 F5
                    // 17 F6
                    // 18 F7
                    // 19 F8
                    // 20 F9
                    // 21 F10
                    // 23 F11
                    // 24 F12

                    }
                }
            }
            else {
                switch(sequence[1]) {
                case 'A': // Up
                    c = UP;
                    break;
                case 'B': // Down
                    c = DOWN;
                    break;
                case 'C': // Right
                    c = RIGHT;
                    break;
                case 'D': // Left
                    c = LEFT;
                    break;
                case 'H': // Home
                    c = HOME;
                    break;
                case 'F': // End
                    c = END;
                    break;
                case 'Z': // Shift-tab
                    c = SHIFT_TAB;
                    break;
                }
            }
        }
        else if(sequence[0] == 'O'){
            switch(sequence[1]){
                case 'H': // Home
                    c = HOME;
                    break;
                case 'F': // End
                    c = END;
                    break;
            }
        }
    }
    else
        if(0){/*LOG("c: %d\n", c);*/}
#if 0
    if(0)
    LOG("%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
            (int)(unsigned char)_c,
            (int)(unsigned char)sequence[0],
            (int)(unsigned char)sequence[1],
            (int)(unsigned char)sequence[2],
            (int)(unsigned char)sequence[3],
            (int)(unsigned char)sequence[4],
            (int)(unsigned char)sequence[5],
            (int)(unsigned char)sequence[6],
            (int)(unsigned char)sequence[7]);
#endif
    *pc = c;
    *pcx = cx;
    *pcy = cy;
    *pmagnitude = magnitude;
    return 1;
}

int 
main(int argc, const char* const* argv){
    #ifdef _WIN32
    globals.STDIN = GetStdHandle(STD_INPUT_HANDLE);
    globals.STDOUT = GetStdHandle(STD_OUTPUT_HANDLE);
    #else
    int pid = getpid();
    // LOG("pid: %d\n", pid);
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sighandler;
    sa.sa_flags = 0;
    sigaction(SIGWINCH, &sa, NULL);
    sigaction(SIGCONT, &sa, NULL);
    // signal(SIGWINCH, sighandler);
    #endif
    Args args = {argc?argc-1:0, argc?argv+1:NULL};
    LongString jsonpath = {0};
    ArgToParse pos_args[] = {
        [0] = {
            .name = SV("filepath"),
            .min_num = 1,
            .max_num = 1,
            .dest = ARGDEST(&jsonpath),
            .help = "Json file to parse",
        },
    };

    LongString outpath = {0};
    LongString queries[100];
    enum {QUERY_KWARG=1};
    _Bool braceless = 0;
    _Bool pretty = 0;
    _Bool interactive = 0;
    _Bool intern = 0;
    _Bool gc = 0;
    int indent = 0;
    ArgToParse kw_args[] = {
        {
            .name = SV("-o"),
            .altname1 = SV("--output"),
            .dest = ARGDEST(&outpath),
            .help = "Where to write the result",
        },
        [QUERY_KWARG] = {
            .name = SV("-q"),
            .altname1 = SV("--query"),
            .min_num=0,
            .max_num=arrlen(queries),
            .dest = ARGDEST(&queries[0]),
            .help = "A query to filter the data. Queries can be stacked",
        },
        {
            .name = SV("--braceless"),
            .dest = ARGDEST(&braceless),
            .help = "Don't require opening and closing braces around the document",
        },
        {
            .name = SV("-p"),
            .altname1 = SV("--pretty"),
            .dest = ARGDEST(&pretty),
            .help = "Pretty print the output",
        },
        {
            .name = SV("--indent"),
            .dest = ARGDEST(&indent),
            .help = "Number of leading spaces to print",
        },
        {
            .name = SV("-i"),
            .altname1 = SV("--interactive"),
            .help = "Enter a cli prompt",
            .dest = ARGDEST(&interactive),
        },
        {
            .name = SV("--intern-objects"),
            .altname1 = SV("--intern"),
            .help = "Reuse duplicate arrays and objects while parsing. Slower but can use less memory. Sometimes.",
            .dest = ARGDEST(&intern),
            .hidden = 1,
        },
        {
            .name = SV("--gc"),
            .help = "Run the gc on exit. This is for testing.",
            .dest = ARGDEST(&gc),
            .hidden = 1,
        },
    };
    enum {HELP=0, HIDDEN_HELP, VERSION, FISH};
    ArgToParse early_args[] = {
        [HELP] = {
            .name = SV("-h"),
            .altname1 = SV("--help"),
            .help = "Print this help and exit.",
        },
        [HIDDEN_HELP] = {
            .name = SV("-H"),
            .altname1 = SV("--hidden-help"),
            .help = "Print this help and exit.",
            .hidden = 1,
        },
        [VERSION] = {
            .name = SV("-v"),
            .altname1 = SV("--version"),
            .help = "Print the version and exit.",
        },
        [FISH] = {
            .name = SV("--fish-completions"),
            .help = "Print out commands for fish shell completions.",
            .hidden = true,
        },
    };
    ArgParser parser = {
        .name = argc?argv[0]:"drjson",
        .description = "CLI interface to drjson.",
        .positional.args = pos_args,
        .positional.count = arrlen(pos_args),
        .early_out.args = early_args,
        .early_out.count = arrlen(early_args),
        .keyword.args = kw_args,
        .keyword.count = arrlen(kw_args),
        .styling.plain = !isatty(fileno(stdout)),
    };
    int columns = get_terminal_size().columns;
    switch(check_for_early_out_args(&parser, &args)){
        case HELP:
            print_argparse_help(&parser, columns);
            return 0;
        case HIDDEN_HELP:
            print_argparse_hidden_help(&parser, columns);
            return 0;
        case VERSION:
            puts("drjson v" DRJSON_VERSION);
            return 0;
        case FISH:
            print_argparse_fish_completions(&parser);
            return 0;
        default:
            break;
    }
    enum ArgParseError error = parse_args(&parser, &args, ARGPARSE_FLAGS_NONE);
    if(error){
        print_argparse_error(&parser, error);
        return error;
    }
    begin_tui();
    atexit(end_tui);
    if(indent < 0)
        indent = 0;
    if(indent > 80)
        indent = 80;
    if(indent)
        pretty = 1;
    LongString jsonstr = {0};
    jsonstr = read_file(jsonpath.text);
    if(!jsonstr.text){
        fprintf(stderr, "Unable to read data from '%s': %s\n", jsonpath.text, strerror(errno));
        return 1;
    }
    DrJsonAllocator allocator = drjson_stdc_allocator();
    DrJsonContext* jctx = drjson_create_ctx(allocator);
    DrJsonParseContext ctx = {
        .ctx = jctx,
        .begin = jsonstr.text,
        .cursor = jsonstr.text,
        .end = jsonstr.text+jsonstr.length,
        .depth = 0,
    };
    unsigned flags = DRJSON_PARSE_FLAG_NONE;
    if(braceless) flags |= DRJSON_PARSE_FLAG_BRACELESS_OBJECT;
    if(intern) flags |= DRJSON_PARSE_FLAG_INTERN_OBJECTS;
    flags |= DRJSON_PARSE_FLAG_NO_COPY_STRINGS;
    DrJsonValue document = drjson_parse(&ctx, flags);
    if(document.kind == DRJSON_ERROR){
        size_t l, c;
        drjson_get_line_column(&ctx, &l, &c);
        drjson_print_error_fp(stderr,  jsonpath.text, jsonpath.length, l, c, document);
        return 1;
    }
    DrJsonValue this = document;
    DrJsonValue stack[1024] = {this};
    size_t top = 0;
    for(int i = 0; i < kw_args[QUERY_KWARG].num_parsed; i++){
        DrJsonValue v = drjson_query(jctx, this, queries[i].text, queries[i].length);
        if(v.kind == DRJSON_ERROR){
            fprintf(stderr, "Error when evaluating the %dth query ('%s'): ", i, queries[i].text);
            drjson_print_value_fp(jctx, stderr, v, 0, DRJSON_PRETTY_PRINT|DRJSON_APPEND_NEWLINE);
            return 1;
        }
        this = v;
        stack[++top] = this;
    }
    for(;;){
        if(globals.needs_rescale){
            TermSize sz = get_terminal_size();
            drt_update_terminal_size(&globals.drt, sz.columns, sz.rows);
            drt_update_drawable_area(&globals.drt, 0, 0, sz.columns, sz.rows);
            drt_invalidate(&globals.drt);
            drt_clear_screen(&globals.drt);
            if(globals.screenh != sz.rows || globals.screenw != sz.columns){
                globals.screenh = sz.rows;
                globals.screenw = sz.columns;
            }
            globals.needs_rescale = 0;
        }
        int c = 0, cx = 0, cy = 0, magnitude = 0;
        int r = get_input(&c, &cx, &cy, &magnitude);
        if(r == -1) goto finally;
        if(!r) continue;
        if(c == CTRL_Z){
            #ifdef _WIN32
            #else
            end_tui();
            raise(SIGTSTP);
            begin_tui();
            globals.needs_redisplay = 1;
            #endif
            continue;
        }
        if(globals.needs_rescale){
            TermSize sz = get_terminal_size();
            drt_update_terminal_size(&globals.drt, sz.columns, sz.rows);
            drt_update_drawable_area(&globals.drt, 0, 0, sz.columns, sz.rows);
            drt_invalidate(&globals.drt);
            drt_clear_screen(&globals.drt);
            if(globals.screenh != sz.rows || globals.screenw != sz.columns){
                globals.screenh = sz.rows;
                globals.screenw = sz.columns;
            }
            globals.needs_rescale = 0;
        }

        return 0;
    }
    finally:;
    return 0;
}

static inline
_Bool
SV_startswith(StringView hay, StringView needle){
    if(needle.length > hay.length) return 0;
    if(!needle.length) return 1;
    return memcmp(needle.text, hay.text, needle.length) == 0;
}

#include "drjson.c"
