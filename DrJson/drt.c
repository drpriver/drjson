//
// Copyright © 2024, David Priver <david@davidpriver.com>
//
#ifndef DRT_LL_C
#define DRT_LL_C

#include "drt.h"
#include <stdarg.h>
#include <string.h>

#ifdef __clang__
#pragma clang assume_nonnull begin
#endif

enum {DRT_MAX_LINES=200, DRT_MAX_COLUMNS=400};

typedef struct DrtColor DrtColor;
struct DrtColor {
    union {
        struct {
            _Bool is_24bit:1;
            _Bool is_not_reset:1;
            union {
                unsigned char _8bit;
                unsigned char r;
            };
            unsigned char g;
            unsigned char b;
        };
        unsigned bits;
    };
};
_Static_assert(sizeof(DrtColor) == sizeof(unsigned), "");

typedef struct DrtState DrtState;
struct DrtState {
    unsigned style;
    DrtColor color;
    DrtColor bg_color;
    struct { 
        int x, y, w, h;
    } scissor;
};

typedef struct DrtCell DrtCell;
struct DrtCell {
    DrtColor color;
    DrtColor bg_color;
    unsigned char style:5;
    unsigned char rend_width:3;
    char txt[7];
};

struct Drt {
    DrtState state_stack[100];
    size_t state_cursor;
    int term_w, term_h;
    struct {
        int x, y, w, h;
    } draw_area;
    int x, y;
    DrtCell cells[2][DRT_MAX_LINES*DRT_MAX_COLUMNS];
    _Bool active_cells;
    _Bool dirty;
    _Bool force_paint;
    _Bool cursor_visible;
    char buff[32*DRT_MAX_LINES*DRT_MAX_COLUMNS];
    size_t buff_cursor;
    int cur_x, cur_y;
};

static inline
DrtCell*
drt_current_cell(Drt* drt){
    return &drt->cells[drt->active_cells][drt->x+drt->y*drt->draw_area.w];
}
static inline
DrtCell*
drt_old_cell(Drt* drt){
    return &drt->cells[!drt->active_cells][drt->x+drt->y*drt->draw_area.w];
}

static inline
DrtState*
drt_current_state(Drt* drt){
    return &drt->state_stack[drt->state_cursor];
}

static inline
void
drt_sprintf(Drt* drt, const char* fmt, ...){
    va_list va;
    va_start(va, fmt);
    char* buff = drt->buff+ drt->buff_cursor;
    size_t remainder = sizeof drt->buff - drt->buff_cursor;
    size_t n = vsnprintf(buff, remainder, fmt, va);
    if(n > remainder)
        n = remainder - 1;
    drt->buff_cursor += n;
    va_end(va);
}

static inline
void
drt_flush(Drt* drt){
    drt_sprintf(drt, "\x1b[%d;%dH", drt->cur_y+drt->draw_area.y+1, drt->cur_x+drt->draw_area.x+1);
    if(0){
        static FILE* fp;
        if(!fp) fp = fopen("drtlog.txt", "a");
        fprintf(fp, "\nflush\n-----\n\n");
        for(size_t i = 0; i < drt->buff_cursor; i++){
            char c = drt->buff[i];
            if((unsigned)(unsigned char)c < 0x20)
                fprintf(fp, "\\x%x", c);
            else
                fputc(c, fp);
        }
        fflush(fp);
    }
    fwrite(drt->buff, drt->buff_cursor, 1, stdout);
    fflush(stdout);
    drt->buff_cursor = 0;
    drt->force_paint = 0;
}

DRT_API
void
drt_init(Drt* drt){
    drt_sprintf(drt, "\x1b[?1049h");
}

DRT_API
void
drt_end(Drt* drt){
    drt_sprintf(drt, "\x1b[?25h");
    drt_sprintf(drt, "\x1b[?1049l");
    drt_sprintf(drt, "\n");
    drt_flush(drt);
}

typedef struct DrtPaint DrtPaint;
struct DrtPaint {
    DrtState state;
    int x, y;
};

static inline
void
drt_paint_update(Drt* drt, DrtPaint* p, int x, int y, DrtCell* new){
    if(x != p->x || y != p->y){
        // Goto coord
        int term_x = drt->draw_area.x + x+1;
        int term_y = drt->draw_area.y + y+1;
        drt_sprintf(drt, "\x1b[%d;%dH", term_y, term_x);
    }
    _Bool started = 0;
    if(p->state.style != new->style){
        // set style
        drt_sprintf(drt, "\x1b[0;");
        started = 1;
        p->state.color = (DrtColor){0};
        p->state.bg_color = (DrtColor){0};
        if(new->style & DRT_STYLE_BOLD){
            drt_sprintf(drt, "1;");
        }
        if(new->style & DRT_STYLE_ITALIC){
            drt_sprintf(drt, "3;");
        }
        if(new->style & DRT_STYLE_UNDERLINE){
            drt_sprintf(drt, "4;");
        }
        if(new->style & DRT_STYLE_STRIKETHROUGH){
            drt_sprintf(drt, "9;");
        }
        // drt_sprintf(drt, "m");
    }
    if(p->state.color.bits != new->color.bits){
        if(!started){
            started = 1;
            drt_sprintf(drt, "\x1b[");
        }
        // set color
        if(!new->color.is_not_reset){
            drt_sprintf(drt, "39;");
        }
        else if(new->color.is_24bit){
            drt_sprintf(drt, "38;2;%d;%d;%d;", new->color.r, new->color.g, new->color.b);
        }
        else {
            drt_sprintf(drt, "38;5;%d;", new->color._8bit);
        }
    }
    if(p->state.bg_color.bits != new->bg_color.bits){
        if(!started){
            started = 1;
            drt_sprintf(drt, "\x1b[");
        }
        if(!new->bg_color.is_not_reset){
            drt_sprintf(drt, "49;");
        }
        else if(new->bg_color.is_24bit){
            drt_sprintf(drt, "48;2;%d;%d;%d;", new->bg_color.r, new->bg_color.g, new->bg_color.b);
        }
        else {
            drt_sprintf(drt, "48;5;%d;", new->bg_color._8bit);
        }
    }
    if(started){
        drt->buff[drt->buff_cursor-1] = 'm';
    }
    // write char
    if((unsigned)(unsigned char)new->txt[0] <= 0x20u)
        drt_sprintf(drt, " ");
    else if((unsigned)(unsigned char)new->txt[0] == 0x7fu)
        drt_sprintf(drt, " ");
    else 
        drt_sprintf(drt, "%s", new->txt);
    p->x = x+(new->rend_width?new->rend_width:1);
    p->y = y;
    p->state.style = new->style;
    p->state.color = new->color;
    p->state.bg_color = new->bg_color;
}

DRT_API
int
drt_paint(Drt* drt){
    if(!drt->dirty && !drt->force_paint) return 0;
    drt_sprintf(drt, "\x1b[?25l");
    drt_sprintf(drt, "\x1b[?2026h");
    if(drt->force_paint){
        drt_sprintf(drt, "\x1b[%d;%dH\x1b[0J", drt->draw_area.y+1, drt->draw_area.x+1);
    }
    DrtPaint paint = {.x = -1, .y=-1};
    for(int y = 0; y < drt->draw_area.h; y++)
        for(int x = 0; x < drt->draw_area.w; x++){
            DrtCell* old = &drt->cells[!drt->active_cells][x+y*drt->draw_area.w];
            DrtCell* new = &drt->cells[drt->active_cells][x+y*drt->draw_area.w];
            if(!old->txt[0]) old->txt[0] = ' ';
            if(!new->txt[0]) new->txt[0] = ' ';
            if(!drt->force_paint){
                if(memcmp(old, new, sizeof *new) == 0) continue;
                // if((unsigned)(unsigned char)old->txt[0] <= 0x20 && (unsigned)(unsigned char)new->txt[0] <= 0x20) continue;
            }
            drt_paint_update(drt, &paint, x, y, new);
            *old = *new;
            if(new->rend_width > 1)
                x += new->rend_width-1;
        }
    if(drt->cursor_visible){
        drt_sprintf(drt, "\x1b[?25h");
    }
    else {
        // drt_sprintf(drt, "\x1b[?25l");
    }
    drt_sprintf(drt, "\x1b[0m");
    drt_sprintf(drt, "\x1b[?2026l");
    drt_flush(drt);
    drt->active_cells = !drt->active_cells;
    return 0;
}

DRT_API
void
drt_clear_screen(Drt* drt){
    memset(drt->cells[drt->active_cells], 0, sizeof drt->cells[drt->active_cells]);
}

DRT_API
void
drt_invalidate(Drt* drt){
    drt->force_paint = 1;
}

DRT_API
void
drt_move(Drt* drt, int x, int y){
    if(x > -1){
        if(x >= drt->draw_area.w)
            x = drt->draw_area.w-1;
        drt->x = x;
    }
    if(y > -1){
        if(y >= drt->draw_area.h)
            y = drt->draw_area.h-1;
        drt->y = y;
    }
}

DRT_API
void
drt_cursor(Drt* drt, int* x, int* y){
    *x = drt->x;
    *y = drt->y;
}

DRT_API
void
drt_update_drawable_area(Drt* drt, int x, int y, int w, int h){
    if(w < 0) w = 0;
    if(h < 0) h = 0;
    if(x+w>drt->term_w) w = drt->term_w - x;
    if(y+h>drt->term_h) h = drt->term_h - y;
    if(drt->draw_area.x == x && drt->draw_area.y == y && drt->draw_area.w == w && drt->draw_area.h == h)
        return;
    drt->force_paint = 1;
    drt->draw_area.x = x;
    drt->draw_area.y = y;
    drt->draw_area.w = w;
    drt->draw_area.h = h;
    if(drt->x >= w) drt->x = w?w-1:0;
    if(drt->y >= h) drt->y = h?h-1:0;

}

DRT_API
void
drt_update_terminal_size(Drt* drt, int w, int h){
    if(w > DRT_MAX_COLUMNS) w = DRT_MAX_COLUMNS;
    if(h > DRT_MAX_LINES) h = DRT_MAX_LINES;
    if(drt->term_w == w && drt->term_h == h)
        return;
    drt->force_paint = 1;
    drt->term_w = w;
    drt->term_h = h;
    if(drt->draw_area.x + drt->draw_area.w > w)
        drt_update_drawable_area(drt, drt->draw_area.x, drt->draw_area.y, w - drt->draw_area.x, h);
    if(drt->draw_area.y + drt->draw_area.h > h)
        drt_update_drawable_area(drt, drt->draw_area.x, drt->draw_area.y, drt->draw_area.w, h - drt->draw_area.y);
}

DRT_API
void
drt_push_state(Drt* drt){
    if(drt->state_cursor +1 >= sizeof drt->state_stack / sizeof drt->state_stack[0]){
        // __builtin_debugtrap();
        return;
    }
    drt->state_stack[drt->state_cursor+1] = drt->state_stack[drt->state_cursor];
    drt->state_cursor++;
}

DRT_API
void
drt_pop_state(Drt* drt){
    if(drt->state_cursor) --drt->state_cursor;
    // else __builtin_debugtrap();
}

DRT_API
void
drt_pop_all_states(Drt* drt){
    drt->state_cursor = 0;
    *drt_current_state(drt) = (DrtState){0};
}

DRT_API
void
drt_clear_state(Drt* drt){
    *drt_current_state(drt) = (DrtState){0};
}


DRT_API
void
drt_scissor(Drt* drt, int x, int y, int w, int h){
    DrtState* s = drt_current_state(drt);
    s->scissor.x = x;
    s->scissor.y = y;
    s->scissor.w = w;
    s->scissor.h = h;
}

DRT_API
void
drt_set_style(Drt* drt, unsigned style){
    // if(drt->state_cursor == 0) __builtin_debugtrap();
    drt_current_state(drt)->style = style & DRT_STYLE_ALL;
}

DRT_API
void
drt_set_8bit_color(Drt* drt, unsigned color){
    drt_current_state(drt)->color = (DrtColor){._8bit=color, .is_not_reset=1};
}

DRT_API
void
drt_clear_color(Drt* drt){
    drt_current_state(drt)->color = (DrtColor){0};
}


DRT_API
void
drt_set_24bit_color(Drt* drt, unsigned char r, unsigned char g, unsigned char b){
    drt_current_state(drt)->color = (DrtColor){.r=r, .g=g, .b=b, .is_24bit=1, .is_not_reset=1};
}

DRT_API
void
drt_bg_set_8bit_color(Drt* drt, unsigned color){
    drt_current_state(drt)->bg_color = (DrtColor){._8bit=color, .is_not_reset=1};
}

DRT_API
void
drt_bg_clear_color(Drt* drt){
    drt_current_state(drt)->bg_color = (DrtColor){0};
}


DRT_API
void
drt_bg_set_24bit_color(Drt* drt, unsigned char r, unsigned char g, unsigned char b){
    drt_current_state(drt)->bg_color = (DrtColor){.r=r, .g=g, .b=b, .is_24bit=1, .is_not_reset=1};
}

DRT_API
void
drt_setc(Drt* drt, char c){
    // if(drt->state_cursor == 0){
        // if(drt_current_state(drt)->style) __builtin_debugtrap();
    // }
    DrtState* state = drt_current_state(drt);
    DrtCell* cell = drt_current_cell(drt);
    memset(cell->txt, 0, sizeof cell->txt);
    cell->txt[0] = c;
    cell->color = state->color;
    cell->bg_color = state->bg_color;
    cell->style = state->style;
    cell->rend_width = 1;
    if(memcmp(cell, drt_old_cell(drt), sizeof *cell) != 0)
        drt->dirty = 1;
}

DRT_API
void
drt_setc_at(Drt* drt, int x, int y, char c){
    if(x >= drt->draw_area.w) return;
    if(y >= drt->draw_area.h) return;
    DrtState* state = drt_current_state(drt);
    DrtCell* cell = &drt->cells[drt->active_cells][x+y*drt->draw_area.w];
    memset(cell->txt, 0, sizeof cell->txt);
    cell->txt[0] = c;
    cell->color = state->color;
    cell->bg_color = state->bg_color;
    cell->style = state->style;
    cell->rend_width = 1;
    if(memcmp(cell, drt_old_cell(drt), sizeof *cell) != 0)
        drt->dirty = 1;
}

DRT_API
void
drt_putc(Drt* drt, char c){
    drt_setc(drt, c);
    drt_move(drt, drt->x+1, -1);
}

DRT_API
void
drt_setc_mb(Drt* drt, const char* c, size_t length, size_t rendwidth){
    DrtState* state = drt_current_state(drt);
    DrtCell* cell = drt_current_cell(drt);
    if(length > 7) return;
    memset(cell->txt, 0, sizeof cell->txt);
    memcpy(cell->txt, c, length);
    cell->color = state->color;
    cell->bg_color = state->bg_color;
    cell->style = state->style;
    cell->rend_width = rendwidth;
    if(memcmp(cell, drt_old_cell(drt), sizeof *cell) != 0)
        drt->dirty = 1;
}
DRT_API
void
drt_putc_mb(Drt* drt, const char* c, size_t length, size_t rend_width){
    drt_setc_mb(drt, c, length, rend_width);
    drt_move(drt, drt->x+rend_width, -1);
}

DRT_API
void
drt_puts(Drt* drt, const char* txt, size_t length){
    for(size_t i = 0; i < length; i++)
        drt_putc(drt, txt[i]);
}

DRT_API
void
drt_set_cursor_visible(Drt* drt, _Bool show){
    drt->cursor_visible = show;
}

DRT_API
void
drt_move_cursor(Drt* drt, int x, int y){
    drt->cur_x = x;
    drt->cur_y = y;
}

DRT_API
void
drt_printf(Drt* drt, const char* fmt, ...){
    char buff[1024];
    va_list va;
    va_start(va, fmt);
    int n = vsnprintf(buff, sizeof buff, fmt, va);
    va_end(va);
    drt_puts(drt, buff, n < sizeof buff? n : -1+sizeof buff);
}
DRT_API
void
drt_clear_to_end_of_row(Drt*drt){
    int w = drt->draw_area.w - drt->x;
    if(!w) return;
    memset(&drt->cells[drt->active_cells][drt->x+drt->y*drt->draw_area.w], 0, w * sizeof(DrtCell));
    drt->dirty = 1;
}

#ifdef __clang__
#pragma clang assume_nonnull end
#endif

#endif
