#ifndef TUI_GET_INPUT_C
#define TUI_GET_INPUT_C
#include "tui_get_input.h"
#ifdef _WIN32
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#endif

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
read_one(TermState* TS, int* needs_rescale, char* buff, _Bool block){
#ifdef _WIN32
    for(;;){
        INPUT_RECORD record;
        DWORD num_read = 0;
        if(!block){
            DWORD timeout = 0;
            DWORD ev = WaitForSingleObject(TS->STDIN, timeout);
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
        BOOL b = ReadConsoleInput(TS->STDIN, &record, 1, &num_read);
        // LOG("DoneReadConsoleInput\n");
        if(!b) return -1;
        if(record.EventType == WINDOW_BUFFER_SIZE_EVENT){
            *needs_rescale = 1;
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
    (void)TS;
    if(block){
        ssize_t e;
        for(;;){
            e = read(STDIN_FILENO, buff, 1);
            if(e == -1 && errno == EINTR) {
                if(*needs_rescale){
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
read_one_nb(TermState* TS, int* needs_rescale, char* buff){
    return read_one(TS, needs_rescale, buff, /*block=*/0);
}

static inline
ssize_t
read_one_b(TermState* TS, int* needs_rescale, char* buff){
    return read_one(TS, needs_rescale, buff, /*block=*/1);
}

static
int
get_input(TermState* TS, int* needs_rescale, int* pc, int* pcx, int* pcy, int* pmagnitude){
    char _c;
    char sequence[32] = {0};
    ssize_t nread = read_one_b(TS, needs_rescale, &_c);
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
            // invalid sequence
             return 0;
        }
        ssize_t e = 0;
        for(int i = 1; i < length; i++){
            e = read_one_nb(TS, needs_rescale, sequence+i);
            if(e == -1) return -1;
            int val = (int)(unsigned char)sequence[i];
            if(val <= 127){
                break;
            }
        }
        for(int i = 0; i < length; i++){
        }
        return 0;
    }
    int cx = 0, cy = 0;
    int magnitude = 1;
    if(c == ESC){
        if(read_one_nb(TS, needs_rescale, sequence) == -1) return -1;
        if(read_one_nb(TS, needs_rescale, sequence+1) == -1) return -1;
        if(sequence[0] == '['){
            if(sequence[1] == '<'){
                // See https://invisible-island.net/xterm/ctlseqs/ctlseqs.html#h3-Extended-coordinates
                int i;
                int mb = 0;
                for(i = 2; i < (int)sizeof sequence; i++){
                    if(read_one_nb(TS, needs_rescale, sequence+i) == -1) return -1;
                    if(sequence[i] == 0) return 0; // unexpected end of escape sequence
                    if(sequence[i] == ';') break;
                    if(sequence[i] < '0' || sequence[i] > '9') return 0; // out of range, should be decimal
                    mb *= 10;
                    mb += sequence[i] - '0';
                }
                int x = 0;
                for(; i < (int)sizeof sequence; i++){
                    if(read_one_nb(TS, needs_rescale, sequence+i) == -1) return -1;
                    if(sequence[i] == 0) return 0; // unexpected end of escape sequence
                    if(sequence[i] == ';') break;
                    if(sequence[i] < '0' || sequence[i] > '9') return 0; // out of range, should be decimal
                    x *= 10;
                    x += sequence[i] - '0';
                }
                int y = 0;
                for(; i < (int)sizeof sequence; i++){
                    if(read_one_nb(TS, needs_rescale, sequence+i) == -1) return -1;
                    if(sequence[i] == 0) return 0; // unexpected end of escape sequence
                    if(sequence[i] == 'm' || sequence[i] == 'M') break;
                    if(sequence[i] < '0' || sequence[i] > '9') return 0; // out of range, should be decimal
                    y *= 10;
                    y += sequence[i] - '0';
                }
                _Bool up = sequence[i] == 'm';
                cx = x -1;
                cy = y -1;
                switch(mb){
                    case 0: c = up?LCLICK_UP:LCLICK_DOWN; break;
                    case 32: c = LDRAG; break;
                    case 64: c = UP; magnitude = 3; break;
                    case 65: c = DOWN; magnitude = 3; break;
                    default: break;
                }
            }
            else if(sequence[1] == 'M'){
                if(read_one_nb(TS, needs_rescale, sequence+2) == -1) return -1;
                if(read_one_nb(TS, needs_rescale, sequence+3) == -1) return -1;
                cx = ((int)(unsigned char)sequence[3]) - 32-1;
                cy = ((int)(unsigned char)sequence[4]) - 32-1;
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
                if (read_one_nb(TS, needs_rescale, sequence+2) == -1) return -1;
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
    *pc = c;
    *pcx = cx;
    *pcy = cy;
    *pmagnitude = magnitude;
    return 1;
}

#endif
