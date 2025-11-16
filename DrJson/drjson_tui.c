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
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <dlfcn.h>
#endif
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include "TUI/tui_get_input.h"
#include "TUI/tui_get_input.c"
#include "TUI/lineedit.h"
#include "TUI/cmd_parse.h"
#include "TUI/cmd_parse.c"
#define DRJSON_API static inline
#include "drjson.h"
// Access to private APIs
#include "drjson.c"
#define DRE_API static
#include "TUI/dre.c"
#include "argument_parsing.h"
#include "term_util.h"
#include "TUI/drt.h"
#include "TUI/drt.c"

#ifndef force_inline
#if defined(__GNUC__) || defined(__clang__)
#define force_inline static inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define force_inline static inline __forceinline
#else
#define force_inline static inline
#endif
#endif

#ifdef __clang__
#pragma clang assume_nonnull begin
#else
#ifndef _Nullable
#define _Nullable
#endif
#ifndef _Null_unspecified
#define _Null_unspecified
#endif
#ifndef _Nonnull
#define _Nonnull
#endif
#endif

static inline
void
strip_whitespace(const char*_Nonnull*_Nonnull ptext, size_t *pcount){
    size_t count = *pcount;
    const char* text = *ptext;
    while(count && text[0] == ' '){
        text++;
        count--;
    }
    while(count && text[count-1] == ' '){
        count--;
    }
    *ptext = text;
    *pcount = count;
}


static struct {
    TermState TS;
    int needs_recalc, needs_rescale, needs_redisplay;
    int screenw, screenh;
    _Bool intern;
    Drt drt;
} globals;

enum {ITEMS_PER_ROW=16};

static const char* LOGFILE = NULL;
static FILE* LOGFILE_FP = NULL;
static inline
void
__attribute__((format(printf,1, 2)))
LOG(const char* fmt, ...){
    if(!LOGFILE) return;
#ifdef _WIN32
    if(!LOGFILE_FP) LOGFILE_FP = fopen(LOGFILE, "w");
#else
    if(!LOGFILE_FP) LOGFILE_FP = fopen(LOGFILE, "wbe");
#endif
    if(!LOGFILE_FP) return;

    va_list va;
    va_start(va, fmt);
    vfprintf(LOGFILE_FP, fmt, va);
    va_end(va);
    fflush(LOGFILE_FP);
}

static inline
void
le_render(Drt* drt, LineEditor* buf){
    if(buf->length > 0)
        drt_puts(drt, buf->data, buf->length);
}

//------------------------------------------------------------
// Navigation Data Structures
//------------------------------------------------------------

// Search modes
enum SearchMode {
    SEARCH_INACTIVE = 0,
    SEARCH_RECURSIVE = 1,    // /pattern - global search
    SEARCH_QUERY = 2,        // /?path pattern - search by evaluating path
};

// Represents a single visible line in the TUI
typedef struct NavItem NavItem;
struct NavItem {
    DrJsonValue value;        // The JSON value at this position
    DrJsonAtom key;           // Key if this is an object member (0 if array element)
    int depth;                // Indentation depth (for rendering)
    _Bool is_flat_view;       // If true, this is a synthetic flat array view child
    int64_t index;            // Array/object index (if key.bits==0/!=0), or flat row index (if is_flat_view), -1 if neither
};

typedef struct BitSet BitSet;
struct BitSet {
    uint64_t* ids;
    size_t capacity;
};

// Main navigation state
typedef struct JsonNav JsonNav;
struct JsonNav {
    DrJsonContext* jctx;      // DrJson context
    DrJsonValue root;         // Root document value
    char filename[1024];       // Name of file being viewed
    DrJsonAllocator allocator; // Allocator for nav's dynamic memory

    // Flattened view (rebuilt when expansion state changes)
    NavItem* items;           // Dynamic array of visible items
    size_t item_count;        // Number of visible items
    size_t item_capacity;     // Allocated capacity

    // Expansion tracking
    BitSet expanded;    // Set of expanded container IDs

    // Cursor and viewport
    size_t cursor_pos;        // Current cursor position in items array
    size_t scroll_offset;     // First visible line (for scrolling)

    // State flags
    _Bool needs_rebuild;      // Items array needs regeneration
    _Bool show_help;
    const StringView* help_lines;
    size_t help_lines_count;
    int help_page;            // Current help page (0-based)
    _Bool command_mode;       // In command mode (:w, :save, etc.)
    _Bool was_opened_with_braceless;  // Whether this file was opened with --braceless

    // Message display
    char message[512];        // Message to display to user
    size_t message_length;    // Length of message

    // Command mode
    LineEditor command_buffer;      // Command input buffer
    LineEditorHistory command_history; // Command history
    int tab_count;                  // Number of consecutive tabs pressed
    char saved_command[256];        // Original command before tab completion
    size_t saved_command_len;       // Length of saved command
    size_t saved_prefix_len;

    // Completion menu
    _Bool in_completion_menu;       // Whether completion menu is showing
    char completion_matches[64][256]; // Array of completion strings
    int completion_count;           // Number of completions
    int completion_selected;        // Currently selected completion index
    int completion_scroll;          // Scroll offset for completion menu

    // Search state
    LineEditor search_buffer;       // Current search query
    LineEditorHistory search_history; // Search history
    enum SearchMode search_mode;    // Current search mode
    _Bool search_input_active;      // True when actively typing search

    // For SEARCH_QUERY mode: parsed /?path pattern
    DrJsonPath search_query_path;   // Parsed query path (e.g., "metadata.author.name")
    char search_pattern[256];       // Pattern part (e.g., "Smith" from "/?metadata.author.name Smith")
    size_t search_pattern_len;

    // Numeric search optimization
    struct {
        _Bool is_numeric;           // True if pattern parsed as number
        _Bool is_integer;           // True if it's an integer (not float)
        int64_t int_value;          // Parsed integer value
        double double_value;        // Parsed double value
    } search_numeric;

    // Inline edit mode
    _Bool edit_mode;                // In inline edit mode
    _Bool edit_key_mode;            // If true, editing key; if false, editing value

    // Insert mode (for both arrays and objects)
    enum {
        INSERT_NONE,
        INSERT_ARRAY,
        INSERT_OBJECT
    } insert_mode;
    size_t insert_container_pos;    // Position of container (array/object) to insert into
    size_t insert_index;            // Index to insert at (SIZE_MAX for append)
    size_t insert_visual_pos;       // Visual position where insert buffer should render
    DrJsonAtom insert_object_key;   // Key atom after first phase of object insertion (object-only)

    LineEditor edit_buffer;         // Buffer for editing value

    // Focus mode
    DrJsonValue* focus_stack;
    size_t focus_stack_count;
    size_t focus_stack_capacity;

    // Multi-key input state
    int pending_key;                // First key of a two-key sequence (z, c, d, y)
};

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

// Expand ~ to home directory in a path
// Returns 0 on success, -1 on error (buffer too small or HOME not set)
static
int
expand_tilde_to_buffer(const char* path, size_t path_len, char* buffer, size_t buffer_size){
    if(path_len == 0 || path[0] != '~'){
        // No tilde, just copy the path
        if(path_len + 1 > buffer_size) return -1;
        memcpy(buffer, path, path_len);
        buffer[path_len] = '\0';
        return 0;
    }

    const char* home = getenv("HOME");
    #ifdef _WIN32
    if(!home) home = getenv("USERPROFILE");
    #endif
    if(!home) return -1;

    size_t home_len = strlen(home);

    if(path_len == 1){
        // Just "~"
        if(home_len + 1 > buffer_size) return -1;
        memcpy(buffer, home, home_len);
        buffer[home_len] = '\0';
        return 0;
    }
    else if(path[1] == '/' || path[1] == '\\'){
        // "~/something" or "~\something"
        size_t total = home_len + (path_len - 1); // -1 because we skip '~'
        if(total + 1 > buffer_size) return -1;
        memcpy(buffer, home, home_len);
        memcpy(buffer + home_len, path + 1, path_len - 1);
        buffer[total] = '\0';
        return 0;
    }
    else {
        // "~username" - don't expand (would require getpwnam on Unix)
        if(path_len + 1 > buffer_size) return -1;
        memcpy(buffer, path, path_len);
        buffer[path_len] = '\0';
        return 0;
    }
}

static inline
DRJSON_WARN_UNUSED
int
read_file_streamed(FILE* fp, LongString* out){
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
                return -1;
            }
            buff = newbuff;
        }
        used += nread;
        if(nread != remainder){
            if(feof(fp)) break;
            else{
                free(buff);
                return -1;
            }
        }
    }
    buff = realloc(buff, used+1);
    buff[used] = 0;
    *out = (LongString){used, buff};
    return 0;
}

static inline
DRJSON_WARN_UNUSED
int
read_file(const char* filepath, LongString* out){
    int status = -1;
    FILE* fp = fopen(filepath, "rb");
    if(!fp) return -1;
    long long size = file_size_from_fp(fp);
    if(size <= 0){
        status = read_file_streamed(fp, out);
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
    *out = (LongString){nbytes, text};
    status = 0;
finally:
    fclose(fp);
    return status;
}

//------------------------------------------------------------
// Expansion Set Operations
//------------------------------------------------------------

static inline
void
bs_ensure_capacity(BitSet* set, size_t id, DrJsonAllocator* allocator){
    size_t idx = id / 64;
    if(idx >= set->capacity){
        size_t new_capacity = idx + 1;
        // Round up to next power of 2 for fewer reallocations
        new_capacity--;
        new_capacity |= new_capacity >> 1;
        new_capacity |= new_capacity >> 2;
        new_capacity |= new_capacity >> 4;
        new_capacity |= new_capacity >> 8;
        new_capacity |= new_capacity >> 16;
        new_capacity |= new_capacity >> 32;
        new_capacity++;

        size_t old_size = set->capacity * sizeof *set->ids;
        size_t new_size = new_capacity * sizeof *set->ids;
        uint64_t* new_ids = allocator->realloc(allocator->user_pointer, set->ids, old_size, new_size);
        if(!new_ids) __builtin_debugtrap();
        // Zero out the new portion
        memset(new_ids + set->capacity, 0, (new_capacity - set->capacity) * sizeof *new_ids);
        set->ids = new_ids;
        set->capacity = new_capacity;
    }
}

static inline
_Bool
bs_contains(const BitSet* set, size_t id){
    size_t bit = id & 63;
    size_t idx = id / 64;
    if(idx >= set->capacity) return 0;
    uint64_t val = set->ids[idx];
    return val & (1llu << bit)?1:0;
}

static inline
void
bs_add(BitSet* set, size_t id, DrJsonAllocator* allocator){
    size_t bit = id & 63;
    size_t idx = id / 64;
    bs_ensure_capacity(set, id, allocator);
    set->ids[idx] |= 1lu << bit;
}

static inline
void
bs_remove(BitSet* set, size_t id){
    size_t bit = id & 63;
    size_t idx = id / 64;
    if(idx >= set->capacity) return; // Nothing to remove
    set->ids[idx] &= ~(1llu << bit);
}

static inline
void
bs_toggle(BitSet* set, size_t id, DrJsonAllocator* allocator){
    size_t bit = id & 63;
    size_t idx = id / 64;
    bs_ensure_capacity(set, id, allocator);
    set->ids[idx] ^= 1lu << bit;
}

static inline
void
bs_clear(BitSet* set){
    memset(set->ids, 0, sizeof *set->ids * set->capacity);
}

static inline
void
bs_free(BitSet* set, DrJsonAllocator* allocator){
    if(set->ids)
        allocator->free(allocator->user_pointer, set->ids, set->capacity * sizeof *set->ids);
    set->ids = NULL;
    set->capacity = 0;
}

//------------------------------------------------------------
// Navigation Functions
//------------------------------------------------------------

static inline
size_t
nav_get_container_id(DrJsonValue val){
    // Pack the index with a bit to distinguish arrays from objects
    // Use bit 0: 0 for arrays, 1 for objects
    if(val.kind == DRJSON_ARRAY || val.kind == DRJSON_ARRAY_VIEW)
        return (val.array_idx << 1) | 0;
    if(val.kind == DRJSON_OBJECT || val.kind == DRJSON_OBJECT_KEYS ||
       val.kind == DRJSON_OBJECT_VALUES || val.kind == DRJSON_OBJECT_ITEMS)
        return (val.object_idx << 1) | 1;
    return 0;
}

static inline
_Bool
nav_is_container(DrJsonValue val){
    return val.kind == DRJSON_ARRAY || val.kind == DRJSON_OBJECT;
}

static inline
_Bool
nav_is_expanded(const JsonNav* nav, DrJsonValue val){
    if(!nav_is_container(val))
        return 0;
    return bs_contains(&nav->expanded, nav_get_container_id(val));
}

static inline
void
nav_append_item(JsonNav* nav, NavItem item){
    if(nav->item_count >= nav->item_capacity){
        size_t new_cap = nav->item_capacity ? nav->item_capacity * 2 : 256;
        size_t old_size = nav->item_capacity * sizeof *nav->items;
        size_t new_size = new_cap * sizeof *nav->items;
        NavItem* new_items = nav->allocator.realloc(nav->allocator.user_pointer, nav->items, old_size, new_size);
        if(!new_items) return; // allocation failed
        nav->items = new_items;
        nav->item_capacity = new_cap;
    }
    nav->items[nav->item_count++] = item;
}

static
size_t
nav_find_parent(JsonNav* nav, size_t pos){
    if(!pos) return SIZE_MAX;
    if(pos >= nav->item_count) return SIZE_MAX;
    NavItem* item = &nav->items[pos];
    int depth = item->depth;
    if(depth<=0) return SIZE_MAX;
    int parent_depth = depth-1;
    while(pos--){
        NavItem* p = &nav->items[pos];
        if(p->depth == parent_depth) return (size_t)(p - nav->items);
    }
    return SIZE_MAX;
}

static void nav_rebuild_recursive(JsonNav* nav, DrJsonValue val, int depth, DrJsonAtom key, int64_t index);

// Check if an array should be rendered as a flat wrapped list
// Returns true if all elements are numbers (or other simple primitives)
static
_Bool
nav_should_render_flat(JsonNav* nav, DrJsonValue val){
    if(val.kind != DRJSON_ARRAY) return 0;

    int64_t len = drjson_len(nav->jctx, val);
    if(len == 0) return 0; // Empty arrays render normally

    // Check if all elements are numbers
    for(int64_t i = 0; i < len; i++){
        DrJsonValue child = drjson_get_by_index(nav->jctx, val, i);
        if(child.kind != DRJSON_NUMBER &&
           child.kind != DRJSON_INTEGER &&
           child.kind != DRJSON_UINTEGER){
            return 0;
        }
    }
    return 1;
}

static
void
nav_rebuild(JsonNav* nav){
    nav->item_count = 0;
    nav_rebuild_recursive(nav, nav->root, 0, (DrJsonAtom){0}, -1);
    nav->needs_rebuild = 0;

    // Clamp cursor to valid range
    if(nav->item_count == 0)
        nav->cursor_pos = 0;
    else if(nav->cursor_pos >= nav->item_count)
        nav->cursor_pos = nav->item_count - 1;
}

static
void
nav_rebuild_recursive(JsonNav* nav, DrJsonValue val, int depth, DrJsonAtom key, int64_t index){
    // Check if this should be rendered flat
    _Bool render_flat = 0;
    if(val.kind == DRJSON_ARRAY && nav_is_expanded(nav, val)){
        render_flat = nav_should_render_flat(nav, val);
    }

    // Add current item
    NavItem item = {
        .value = val,
        .key = key,
        .depth = depth,
        .index = index,
        .is_flat_view = 0
    };
    nav_append_item(nav, item);

    // If it's a container and expanded, add children
    if(nav_is_container(val) && nav_is_expanded(nav, val)){
        if(render_flat){
            // Add multiple synthetic flat view children (one per row of 10 items)
            int64_t len = drjson_len(nav->jctx, val);
            int num_rows = (int)((len + ITEMS_PER_ROW - 1) / ITEMS_PER_ROW); // ceil division

            for(int row = 0; row < num_rows; row++){
                NavItem flat_item = {
                    .value = val,  // Same array value
                    .key = (DrJsonAtom){0},
                    .depth = depth + 1,
                    .index = row,
                    .is_flat_view = 1
                };
                nav_append_item(nav, flat_item);
            }
        }
        else {
            int64_t len = drjson_len(nav->jctx, val);

            if(val.kind == DRJSON_ARRAY){
                for(int64_t i = 0; i < len; i++){
                    DrJsonValue child = drjson_get_by_index(nav->jctx, val, i);
                    nav_rebuild_recursive(nav, child, depth + 1, (DrJsonAtom){0}, i);
                }
            }
            else { // DRJSON_OBJECT
                DrJsonValue items = drjson_object_items(val);
                int64_t items_len = drjson_len(nav->jctx, items);
                for(int64_t i = 0; i < items_len; i += 2){
                    DrJsonValue k = drjson_get_by_index(nav->jctx, items, i);
                    DrJsonValue v = drjson_get_by_index(nav->jctx, items, i + 1);
                    nav_rebuild_recursive(nav, v, depth + 1, k.atom, i / 2);
                }
            }
        }
    }
}

static inline
void
nav_init(JsonNav* nav, DrJsonContext* jctx, DrJsonValue root, const char* filename, DrJsonAllocator allocator){
    *nav = (JsonNav){
        .jctx = jctx,
        .root = root,
        .allocator = allocator,
        .needs_rebuild = 1,
    };
    // Copy filename
    if(filename){
        size_t len = strlen(filename);
        if(len >= sizeof nav->filename) len = sizeof nav->filename - 1;
        memcpy(nav->filename, filename, len);
        nav->filename[len] = '\0';
    }
    else {
        nav->filename[0] = '\0';
    }
    // Expand root document by default if it's a container
    if(nav_is_container(root)){
        bs_add(&nav->expanded, nav_get_container_id(root), &nav->allocator);
    }
    le_init(&nav->search_buffer, 256);
    le_history_init(&nav->search_history);
    nav->search_buffer.history = &nav->search_history;
    le_init(&nav->command_buffer, 512);
    le_history_init(&nav->command_history);
    nav->command_buffer.history = &nav->command_history;
    le_init(&nav->edit_buffer, 512);
    nav->focus_stack = NULL;
    nav->focus_stack_count = 0;
    nav->focus_stack_capacity = 0;
    nav_rebuild(nav);
}

static
void
nav_reinit(JsonNav* nav){
    // Reset navigation state but keep buffers
    nav->cursor_pos = 0;
    nav->scroll_offset = 0;
    nav->needs_rebuild = 1;
    nav->message_length = 0;
    nav->show_help = 0;
    nav->command_mode = 0;
    nav->pending_key = 0;

    // Clear line editors but keep their buffers
    if(nav->command_buffer.length > 0)
        nav->command_buffer.data[0] = 0;
    nav->command_buffer.length = 0;
    nav->command_buffer.cursor_pos = 0;
    if(nav->search_buffer.length > 0)
        nav->search_buffer.data[0] = 0;
    nav->search_buffer.length = 0;
    nav->search_buffer.cursor_pos = 0;

    // Clear search state but keep buffers
    nav->search_mode = SEARCH_INACTIVE;
    nav->search_input_active = 0;

    nav->in_completion_menu = 0;
    nav->tab_count = 0;

    // Clear expansion set - capacity stays allocated for reuse
    bs_clear(&nav->expanded);

    if(nav_is_container(nav->root)){
        bs_add(&nav->expanded, nav_get_container_id(nav->root), &nav->allocator);
    }
    nav_rebuild(nav);
}


static inline
void
nav_free(JsonNav* nav){
    if(nav->items)
        nav->allocator.free(nav->allocator.user_pointer, nav->items, nav->item_capacity * sizeof *nav->items);
    bs_free(&nav->expanded, &nav->allocator);
    le_free(&nav->search_buffer);
    le_history_free(&nav->search_history);
    le_free(&nav->command_buffer);
    if(nav->focus_stack)
        nav->allocator.free(nav->allocator.user_pointer, nav->focus_stack, nav->focus_stack_capacity * sizeof *nav->focus_stack);
    *nav = (JsonNav){0};
}

static inline
void
nav_toggle_expand_at_cursor(JsonNav* nav){
    if(nav->item_count == 0) return;

    NavItem* item = &nav->items[nav->cursor_pos];

    // If not a container, try to toggle parent instead
    if(!nav_is_container(item->value)){
        int current_depth = item->depth;
        if(current_depth == 0) return; // No parent to toggle

        // Find parent by searching backwards for item with smaller depth
        for(size_t i = nav->cursor_pos; i > 0; i--){
            if(nav->items[i - 1].depth < current_depth){
                size_t parent_idx = i - 1;
                NavItem* parent = &nav->items[parent_idx];
                if(nav_is_container(parent->value)){
                    // Don't allow collapsing the root
                    if(parent->depth == 0) return;

                    size_t id = nav_get_container_id(parent->value);
                    bs_toggle(&nav->expanded, id, &nav->allocator);
                    nav->needs_rebuild = 1;
                    nav_rebuild(nav);
                }
                return;
            }
        }
        return;
    }

    // Don't allow collapsing the root object/array
    if(item->depth == 0) return;

    size_t id = nav_get_container_id(item->value);
    bs_toggle(&nav->expanded, id, &nav->allocator);
    nav->needs_rebuild = 1;
    nav_rebuild(nav);
}

// Helper to recursively expand a value and all its descendants
static
void
nav_expand_recursive_helper(JsonNav* nav, DrJsonValue val){
    if(!nav_is_container(val))
        return;

    // Add this container to expansion set
    bs_add(&nav->expanded, nav_get_container_id(val), &nav->allocator);

    // Recursively expand children
    int64_t len = drjson_len(nav->jctx, val);

    if(val.kind == DRJSON_ARRAY || val.kind == DRJSON_ARRAY_VIEW){
        for(int64_t i = 0; i < len; i++){
            DrJsonValue child = drjson_get_by_index(nav->jctx, val, i);
            nav_expand_recursive_helper(nav, child);
        }
    }
    else if(val.kind == DRJSON_OBJECT || val.kind == DRJSON_OBJECT_KEYS ||
            val.kind == DRJSON_OBJECT_VALUES || val.kind == DRJSON_OBJECT_ITEMS){
        DrJsonValue items = drjson_object_items(val);
        int64_t items_len = drjson_len(nav->jctx, items);
        for(int64_t i = 0; i < items_len; i += 2){
            DrJsonValue v = drjson_get_by_index(nav->jctx, items, i + 1);
            nav_expand_recursive_helper(nav, v);
        }
    }
}

static inline
void
nav_expand_recursive(JsonNav* nav){
    if(nav->item_count == 0) return;

    NavItem* item = &nav->items[nav->cursor_pos];
    if(!nav_is_container(item->value))
        return;

    nav_expand_recursive_helper(nav, item->value);

    nav->needs_rebuild = 1;
    nav_rebuild(nav);
}

// Helper to recursively collapse a value and all its descendants
static
void
nav_collapse_recursive_helper(JsonNav* nav, DrJsonValue val){
    if(!nav_is_container(val))
        return;

    // Remove this container from expansion set
    bs_remove(&nav->expanded, nav_get_container_id(val));

    // Recursively collapse children
    int64_t len = drjson_len(nav->jctx, val);

    if(val.kind == DRJSON_ARRAY || val.kind == DRJSON_ARRAY_VIEW){
        for(int64_t i = 0; i < len; i++){
            DrJsonValue child = drjson_get_by_index(nav->jctx, val, i);
            nav_collapse_recursive_helper(nav, child);
        }
    }
    else if(val.kind == DRJSON_OBJECT || val.kind == DRJSON_OBJECT_KEYS ||
            val.kind == DRJSON_OBJECT_VALUES || val.kind == DRJSON_OBJECT_ITEMS){
        DrJsonValue items = drjson_object_items(val);
        int64_t items_len = drjson_len(nav->jctx, items);
        for(int64_t i = 0; i < items_len; i += 2){
            DrJsonValue v = drjson_get_by_index(nav->jctx, items, i + 1);
            nav_collapse_recursive_helper(nav, v);
        }
    }
}

static inline
void
nav_collapse_recursive(JsonNav* nav){
    if(nav->item_count == 0) return;

    NavItem* item = &nav->items[nav->cursor_pos];
    if(!nav_is_container(item->value))
        return;

    // Special handling for root: collapse children but not the root itself
    if(item->depth == 0){
        DrJsonValue val = item->value;
        int64_t len = drjson_len(nav->jctx, val);

        if(val.kind == DRJSON_ARRAY || val.kind == DRJSON_ARRAY_VIEW){
            for(int64_t i = 0; i < len; i++){
                DrJsonValue child = drjson_get_by_index(nav->jctx, val, i);
                nav_collapse_recursive_helper(nav, child);
            }
        }
        else if(val.kind == DRJSON_OBJECT || val.kind == DRJSON_OBJECT_KEYS ||
                val.kind == DRJSON_OBJECT_VALUES || val.kind == DRJSON_OBJECT_ITEMS){
            DrJsonValue items = drjson_object_items(val);
            int64_t items_len = drjson_len(nav->jctx, items);
            for(int64_t i = 0; i < items_len; i += 2){
                DrJsonValue v = drjson_get_by_index(nav->jctx, items, i + 1);
                nav_collapse_recursive_helper(nav, v);
            }
        }

        nav->needs_rebuild = 1;
        nav_rebuild(nav);
        return;
    }

    nav_collapse_recursive_helper(nav, item->value);

    nav->needs_rebuild = 1;
    nav_rebuild(nav);
}

// Calculate visual position for container insertion
static
size_t
nav_calc_insert_visual_pos(JsonNav* nav, size_t pos, size_t insert_index){
    if(nav->item_count == 0) return 0;

    NavItem* item = &nav->items[pos];
    int depth = item->depth;

    // If inserting at beginning (index 0 or O command)
    if(insert_index == 0){
        return pos + 1;
    }

    // If appending (index SIZE_MAX or o on container itself), find last child
    if(insert_index == SIZE_MAX){
        // Scan forward to find last child of this container
        for(size_t i = pos + 1; i < nav->item_count; i++){
            if(nav->items[i].depth <= depth){
                // Found item at same or lower depth - insert before it
                return i;
            }
        }
        // container is last item, append at end
        return nav->item_count;
    }

    // Inserting at specific index - find the item at insert_index
    // and return position after it
    for(size_t i = pos + 1; i < nav->item_count; i++){
        NavItem* item = &nav->items[i];
        if(item->depth <= depth){
            // Exited the array without finding the index
            return i;
        }
        if(item->depth == depth + 1 && item->index == (int64_t)insert_index){
            // Found the item we want to insert before
            return i;
        }
    }

    return nav->item_count;
}

static inline
void
nav_jump_to_parent(JsonNav* nav, _Bool collapse){
    if(nav->item_count == 0) return;
    if(nav->cursor_pos == 0) return; // Already at root

    int current_depth = nav->items[nav->cursor_pos].depth;
    if(current_depth == 0) return; // Already at root level

    // Search backwards for first item with smaller depth
    for(size_t i = nav->cursor_pos; i > 0; i--){
        if(nav->items[i - 1].depth < current_depth){
            nav->cursor_pos = i - 1;

            // Optionally collapse the parent we just jumped to
            if(collapse){
                NavItem* parent = &nav->items[nav->cursor_pos];
                // Don't collapse the root
                if(parent->depth > 0 && nav_is_container(parent->value) && nav_is_expanded(nav, parent->value)){
                    size_t id = nav_get_container_id(parent->value);
                    bs_remove(&nav->expanded, id);
                    nav->needs_rebuild = 1;
                    nav_rebuild(nav);
                }
            }
            return;
        }
    }
}

// Jump to the nth child of the current item (if it's a container)
// If current item is not an expanded container, jump to nth child of parent
// For flat view items, jump to the row containing item n
static inline
void
nav_jump_to_nth_child(JsonNav* nav, int n){
    if(nav->item_count == 0) return;

    NavItem* item = &nav->items[nav->cursor_pos];

    // If on a flat view item, jump to the row containing the nth element
    if(item->is_flat_view){
        int target_row = n / ITEMS_PER_ROW;

        // Find the parent array item (go backwards to find non-flat-view item)
        size_t parent_pos = nav->cursor_pos;
        for(size_t i = nav->cursor_pos; i > 0; i--){
            if(!nav->items[i - 1].is_flat_view && nav->items[i - 1].depth < item->depth){
                parent_pos = i - 1;
                break;
            }
        }

        // Now find the target row among the flat view children
        for(size_t i = parent_pos + 1; i < nav->item_count; i++){
            if(nav->items[i].is_flat_view && nav->items[i].index == target_row){
                nav->cursor_pos = i;
                return;
            }
            // Stop if we leave the flat view children
            if(!nav->items[i].is_flat_view && i > parent_pos + 1)
                break;
        }
        return;
    }

    // If current item is a container and expanded with children, jump to nth child
    if(nav_is_container(item->value) && nav_is_expanded(nav, item->value)){
        size_t start_pos = nav->cursor_pos + 1;
        int target_depth = item->depth + 1;

        // Check if the first child is a flat view item
        if(start_pos < nav->item_count && nav->items[start_pos].is_flat_view){
            // This is a flat array view, jump to the row containing item n
            int target_row = n / ITEMS_PER_ROW;

            for(size_t i = start_pos; i < nav->item_count; i++){
                if(nav->items[i].depth < target_depth)
                    break; // Left the container
                if(nav->items[i].is_flat_view && nav->items[i].index == target_row){
                    nav->cursor_pos = i;
                    return;
                }
            }
            // If target row not found, go to last flat view row
            for(size_t i = start_pos; i < nav->item_count; i++){
                if(nav->items[i].depth < target_depth)
                    break;
                if(nav->items[i].is_flat_view){
                    nav->cursor_pos = i;
                }
            }
            return;
        }

        // Normal case: jump to nth child
        int child_count = 0;
        for(size_t i = start_pos; i < nav->item_count; i++){
            if(nav->items[i].depth < target_depth)
                break; // Left the container
            if(nav->items[i].depth == target_depth){
                if(child_count == n){
                    nav->cursor_pos = i;
                    return;
                }
                child_count++;
            }
        }
        // If n is out of range, go to last child
        if(child_count > 0 && n >= child_count){
            for(size_t i = start_pos; i < nav->item_count; i++){
                if(nav->items[i].depth < target_depth)
                    break;
                if(nav->items[i].depth == target_depth){
                    nav->cursor_pos = i;
                }
            }
        }
    }
    else {
        // Jump to nth child of parent container
        int current_depth = item->depth;
        if(current_depth == 0) return; // At root, no parent

        // Find parent
        size_t parent_pos = nav->cursor_pos;
        for(size_t i = nav->cursor_pos; i > 0; i--){
            if(nav->items[i - 1].depth < current_depth){
                parent_pos = i - 1;
                break;
            }
        }

        // Now jump to nth child of parent
        if(parent_pos < nav->cursor_pos){
            size_t start_pos = parent_pos + 1;
            int target_depth = nav->items[parent_pos].depth + 1;

            // Check if parent's first child is a flat view item
            if(start_pos < nav->item_count && nav->items[start_pos].is_flat_view){
                // This is a flat array view, jump to the row containing item n
                int target_row = n / ITEMS_PER_ROW;

                for(size_t i = start_pos; i < nav->item_count; i++){
                    if(nav->items[i].depth < target_depth)
                        break; // Left the container
                    if(nav->items[i].is_flat_view && nav->items[i].index == target_row){
                        nav->cursor_pos = i;
                        return;
                    }
                }
                // If target row not found, go to last flat view row
                for(size_t i = start_pos; i < nav->item_count; i++){
                    if(nav->items[i].depth < target_depth)
                        break;
                    if(nav->items[i].is_flat_view){
                        nav->cursor_pos = i;
                    }
                }
                return;
            }

            // Normal case: jump to nth child of parent
            int child_count = 0;
            for(size_t i = start_pos; i < nav->item_count; i++){
                if(nav->items[i].depth < target_depth)
                    break; // Left parent's container
                if(nav->items[i].depth == target_depth){
                    if(child_count == n){
                        nav->cursor_pos = i;
                        return;
                    }
                    child_count++;
                }
            }
            // If n is out of range, go to last child of parent
            if(child_count > 0 && n >= child_count){
                for(size_t i = start_pos; i < nav->item_count; i++){
                    if(nav->items[i].depth < target_depth)
                        break;
                    if(nav->items[i].depth == target_depth){
                        nav->cursor_pos = i;
                    }
                }
            }
        }
    }
}

static inline
void
nav_jump_into_container(JsonNav* nav){
    if(nav->item_count == 0) return;

    NavItem* item = &nav->items[nav->cursor_pos];
    if(!nav_is_container(item->value))
        return; // Not a container, can't jump in

    // Expand if not already expanded
    if(!nav_is_expanded(nav, item->value)){
        size_t id = nav_get_container_id(item->value);
        bs_add(&nav->expanded, id, &nav->allocator);
        nav->needs_rebuild = 1;
        nav_rebuild(nav);
    }

    // Move to first child (next item in list)
    if(nav->cursor_pos + 1 < nav->item_count){
        nav->cursor_pos++;
    }
}

static inline
void
nav_jump_to_next_sibling(JsonNav* nav){
    if(nav->item_count == 0) return;
    if(nav->cursor_pos >= nav->item_count - 1) return; // Already at end

    int current_depth = nav->items[nav->cursor_pos].depth;

    // Search forward for next item at same or lesser depth
    for(size_t i = nav->cursor_pos + 1; i < nav->item_count; i++){
        if(nav->items[i].depth <= current_depth){
            nav->cursor_pos = i;
            return;
        }
    }
    // If not found, we're at the last item at this depth
}

static inline
void
nav_jump_to_prev_sibling(JsonNav* nav){
    if(nav->item_count == 0) return;
    if(nav->cursor_pos == 0) return; // Already at start

    int current_depth = nav->items[nav->cursor_pos].depth;

    // Search backward for previous item at same depth
    for(size_t i = nav->cursor_pos; i > 0; i--){
        if(nav->items[i - 1].depth == current_depth){
            nav->cursor_pos = i - 1;
            return;
        }
        // If we hit a parent (lesser depth), we're the first sibling
        if(nav->items[i - 1].depth < current_depth){
            return;
        }
    }
}

static inline
void
nav_collapse_all(JsonNav* nav){
    bs_clear(&nav->expanded);
    // Keep the root expanded
    if(nav_is_container(nav->root)){
        bs_add(&nav->expanded, nav_get_container_id(nav->root), &nav->allocator);
    }
    nav->cursor_pos = 0;
    nav->scroll_offset = 0;
    nav->needs_rebuild = 1;
    nav_rebuild(nav);
}

static inline
void
nav_expand_all(JsonNav* nav){
    // Expand the root and everything inside it recursively
    nav_expand_recursive_helper(nav, nav->root);
    nav->needs_rebuild = 1;
    nav_rebuild(nav);
}

static inline
void
nav_move_cursor(JsonNav* nav, int delta){
    if(nav->item_count == 0) return;

    int new_pos = (int)nav->cursor_pos + delta;
    if(new_pos < 0)
        new_pos = 0;
    if(new_pos >= (int)nav->item_count)
        new_pos = (int)nav->item_count - 1;
    nav->cursor_pos = (size_t)new_pos;
}

static inline
void
nav_ensure_cursor_visible(JsonNav* nav, int viewport_height){
    if(nav->item_count == 0) return;

    // Account for status line at top and breadcrumb at bottom
    int visible_rows = viewport_height - 2;
    if(visible_rows < 1) visible_rows = 1;

    // Cursor is above viewport
    if(nav->cursor_pos < nav->scroll_offset){
        nav->scroll_offset = nav->cursor_pos;
    }
    // Cursor is below viewport
    else if(nav->cursor_pos >= nav->scroll_offset + (size_t)visible_rows){
        nav->scroll_offset = nav->cursor_pos - (size_t)visible_rows + 1;
    }
}

// Case-insensitive character comparison
static inline
char
to_lower(char c){
    if(c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

// Non-backtracking glob pattern matching with * wildcard support (case-insensitive)
// Uses explicit state tracking instead of recursion
static
_Bool
glob_match(const char* str, size_t str_len, const char* pattern, size_t pattern_len){
    size_t s = 0;  // position in str
    size_t p = 0;  // position in pattern
    size_t last_star_in_pattern = (size_t)-1;  // position of last * in pattern
    size_t last_star_in_string = 0;  // position in str where we last matched a *

    while(s < str_len){
        if(p < pattern_len && pattern[p] == '*'){
            // Found a *, record its position and move past it
            last_star_in_pattern = p;
            last_star_in_string = s;
            p++;
        }
        else if(p < pattern_len && to_lower(pattern[p]) == to_lower(str[s])){
            // Characters match, advance both
            p++;
            s++;
        }
        else if(last_star_in_pattern != (size_t)-1){
            // No match, but we have a previous * - resume from after that *
            p = last_star_in_pattern + 1;
            last_star_in_string++;
            s = last_star_in_string;
        }
        else {
            // No match and no * to resume from
            return 0;
        }
    }

    // Skip any trailing * in pattern
    while(p < pattern_len && pattern[p] == '*'){
        p++;
    }

    // Match if we consumed entire pattern
    return p == pattern_len;
}

// Case-insensitive substring search
static
_Bool
substring_match(const char* str, size_t str_len, const char* query, size_t query_len){
    if(query_len == 0) return 0;

    for(size_t i = 0; i + query_len <= str_len; i++){
        _Bool match = 1;
        for(size_t j = 0; j < query_len; j++){
            if(to_lower(str[i + j]) != to_lower(query[j])){
                match = 0;
                break;
            }
        }
        if(match) return 1;
    }
    return 0;
}

// Helper to check if a string matches the query (uses regex matching)
static
_Bool
string_matches_query(const char* str, size_t str_len, const char* query, size_t query_len){
    DreContext ctx = {0};
    size_t match_start = 0;
    int result = dre_match(&ctx, query, query_len, str, str_len, &match_start);

    // On regex error, fall back to substring match
    if(ctx.error != RE_ERROR_NONE){
        return substring_match(str, str_len, query, query_len);
    }

    return result != 0;
}

// Forward declaration
static _Bool nav_value_matches_query(JsonNav* nav, DrJsonValue val, DrJsonAtom key, const char* query, size_t query_len);

// Helper to check if a value matches the search pattern directly (no path evaluation)
// Used when checking children in query search mode
static
_Bool
nav_value_matches_pattern(JsonNav* nav, DrJsonValue val){
    // Check numeric match
    if(nav->search_numeric.is_numeric){
        if(nav->search_numeric.is_integer){
            if(val.kind == DRJSON_INTEGER && val.integer == nav->search_numeric.int_value)
                return 1;
            if(val.kind == DRJSON_UINTEGER && (int64_t)val.uinteger == nav->search_numeric.int_value)
                return 1;
        }
        else {
            if(val.kind == DRJSON_NUMBER && val.number == nav->search_numeric.double_value)
                return 1;
        }
    }

    // Check string match
    if(val.kind == DRJSON_STRING){
        const char* str = NULL;
        size_t slen = 0;
        int err = drjson_get_str_and_len(nav->jctx, val, &str, &slen);
        if(!err && str && string_matches_query(str, slen, nav->search_pattern, nav->search_pattern_len)){
            return 1;
        }
    }

    return 0;
}

// Check if a NavItem matches the search query (uses regex matching)
static
_Bool
nav_item_matches_query(JsonNav* nav, NavItem* item, const char* query, size_t query_len){
    if(query_len == 0) return 0;

    // For flat view items, check all elements in the row
    if(item->is_flat_view && item->value.kind == DRJSON_ARRAY){
        int64_t len = drjson_len(nav->jctx, item->value);
        size_t WRAP_WIDTH = 10;
        int64_t row_start = (int64_t)item->index * (int64_t)WRAP_WIDTH;
        int64_t row_end = row_start + (int64_t)WRAP_WIDTH;
        if(row_end > len) row_end = len;

        // In SEARCH_QUERY mode, flat view items represent array elements
        // We should check if any element in the row directly matches the pattern
        if(nav->search_mode == SEARCH_QUERY){
            // Check each element directly against the numeric pattern
            for(int64_t i = row_start; i < row_end; i++){
                DrJsonValue elem = drjson_get_by_index(nav->jctx, item->value, i);

                // Check numeric match
                if(nav->search_numeric.is_numeric){
                    if(nav->search_numeric.is_integer){
                        if(elem.kind == DRJSON_INTEGER && elem.integer == nav->search_numeric.int_value)
                            return 1;
                        if(elem.kind == DRJSON_UINTEGER && (int64_t)elem.uinteger == nav->search_numeric.int_value)
                            return 1;
                    }
                    else {
                        if(elem.kind == DRJSON_NUMBER && elem.number == nav->search_numeric.double_value)
                            return 1;
                    }
                }

                // Check string match
                if(elem.kind == DRJSON_STRING){
                    const char* str = NULL;
                    size_t slen = 0;
                    int err = drjson_get_str_and_len(nav->jctx, elem, &str, &slen);
                    if(!err && str && string_matches_query(str, slen, nav->search_pattern, nav->search_pattern_len)){
                        return 1;
                    }
                }
            }
            return 0;
        }
        else {
            // SEARCH_RECURSIVE mode: check each element in this row
            for(int64_t i = row_start; i < row_end; i++){
                DrJsonValue elem = drjson_get_by_index(nav->jctx, item->value, i);
                if(nav_value_matches_query(nav, elem, (DrJsonAtom){0}, query, query_len)){
                    return 1;
                }
            }
            return 0;
        }
    }

    // Delegate to nav_value_matches_query which handles all search modes
    return nav_value_matches_query(nav, item->value, item->key, query, query_len);
}

// Helper to check if a DrJsonValue matches the query (uses regex matching)
static
_Bool
nav_value_matches_query(JsonNav* nav, DrJsonValue val, DrJsonAtom key, const char* query, size_t query_len){
    // For SEARCH_QUERY mode: evaluate path and match result
    if(nav->search_mode == SEARCH_QUERY){
        // Evaluate the query path from the current value
        DrJsonValue result = drjson_evaluate_path(nav->jctx, val, &nav->search_query_path);

        // If path evaluation failed, no match
        if(result.kind == DRJSON_ERROR)
            return 0;

        // If no pattern is provided, a successful path evaluation is a match
        if(nav->search_pattern_len == 0)
            return 1;

        // Fast path: numeric comparison
        if(nav->search_numeric.is_numeric){
            if(nav->search_numeric.is_integer){
                // Integer comparison
                if(result.kind == DRJSON_INTEGER && result.integer == nav->search_numeric.int_value)
                    return 1;
                if(result.kind == DRJSON_UINTEGER && (int64_t)result.uinteger == nav->search_numeric.int_value)
                    return 1;
            }
            else {
                // Floating point comparison
                if(result.kind == DRJSON_NUMBER && result.number == nav->search_numeric.double_value)
                    return 1;
            }
        }

        // Check if result matches the pattern
        if(result.kind == DRJSON_STRING){
            const char* str = NULL;
            size_t len = 0;
            int err = drjson_get_str_and_len(nav->jctx, result, &str, &len);
            if(!err && str && string_matches_query(str, len, nav->search_pattern, nav->search_pattern_len))
                return 1;
        }
        // Also match in arrays: check if any element matches
        else if(result.kind == DRJSON_ARRAY || result.kind == DRJSON_ARRAY_VIEW){
            int64_t len = drjson_len(nav->jctx, result);
            for(int64_t i = 0; i < len; i++){
                DrJsonValue elem = drjson_get_by_index(nav->jctx, result, i);

                // Fast path: numeric comparison for array elements
                if(nav->search_numeric.is_numeric){
                    if(nav->search_numeric.is_integer){
                        if(elem.kind == DRJSON_INTEGER && elem.integer == nav->search_numeric.int_value)
                            return 1;
                        if(elem.kind == DRJSON_UINTEGER && (int64_t)elem.uinteger == nav->search_numeric.int_value)
                            return 1;
                    }
                    else {
                        if(elem.kind == DRJSON_NUMBER && elem.number == nav->search_numeric.double_value)
                            return 1;
                    }
                }

                // String matching for array elements
                if(elem.kind == DRJSON_STRING){
                    const char* str = NULL;
                    size_t slen = 0;
                    int err = drjson_get_str_and_len(nav->jctx, elem, &str, &slen);
                    if(!err && str && string_matches_query(str, slen, nav->search_pattern, nav->search_pattern_len)){
                        return 1;
                    }
                }
            }
        }

        return 0;
    }

    // For SEARCH_RECURSIVE mode: match key OR value (original behavior)
    // Check key if present
    if(key.bits != 0){
        const char* key_str = NULL;
        size_t key_len = 0;
        DrJsonValue key_val = drjson_atom_to_value(key);
        int err = drjson_get_str_and_len(nav->jctx, key_val, &key_str, &key_len);
        if(!err && key_str && string_matches_query(key_str, key_len, query, query_len)){
            return 1;
        }
    }

    // Check value for numbers if query is numeric
    if(nav->search_numeric.is_numeric){
        if(nav->search_numeric.is_integer){
            // Integer comparison
            if(val.kind == DRJSON_INTEGER && val.integer == nav->search_numeric.int_value)
                return 1;
            if(val.kind == DRJSON_UINTEGER && (int64_t)val.uinteger == nav->search_numeric.int_value)
                return 1;
        }
        else {
            // Floating point comparison
            if(val.kind == DRJSON_NUMBER && val.number == nav->search_numeric.double_value)
                return 1;
        }
    }

    // Check value for strings
    if(val.kind == DRJSON_STRING){
        const char* str = NULL;
        size_t len = 0;
        int err = drjson_get_str_and_len(nav->jctx, val, &str, &len);
        if(!err && str && string_matches_query(str, len, query, query_len)){
            return 1;
        }
    }

    return 0;
}

// Check if a value or any of its descendants match the query (read-only, doesn't modify expanded set)
static
_Bool
nav_contains_match(JsonNav* nav, DrJsonValue val, DrJsonAtom key, const char* query, size_t query_len){
    // Check if this value matches
    if(nav_value_matches_query(nav, val, key, query, query_len)){
        return 1;
    }

    // Recursively check children
    if(nav_is_container(val)){
        int64_t len = drjson_len(nav->jctx, val);

        if(val.kind == DRJSON_ARRAY || val.kind == DRJSON_ARRAY_VIEW){
            for(int64_t i = 0; i < len; i++){
                DrJsonValue child = drjson_get_by_index(nav->jctx, val, i);
                if(nav_contains_match(nav, child, (DrJsonAtom){0}, query, query_len)){
                    return 1;
                }
            }
        }
        else {
            DrJsonValue items = drjson_object_items(val);
            int64_t items_len = drjson_len(nav->jctx, items);
            for(int64_t i = 0; i < items_len; i += 2){
                DrJsonValue k = drjson_get_by_index(nav->jctx, items, i);
                DrJsonValue v = drjson_get_by_index(nav->jctx, items, i + 1);
                if(nav_contains_match(nav, v, k.atom, query, query_len)){
                    return 1;
                }
            }
        }
    }

    return 0;
}

// Recursive helper for recursive search
// Returns true if this value or any descendant matches the query
static
_Bool
nav_search_recursive_helper(JsonNav* nav, DrJsonValue val, DrJsonAtom key, const char* query, size_t query_len){
    _Bool found_match = 0;

    // Check if this value matches
    if(nav_value_matches_query(nav, val, key, query, query_len)){
        found_match = 1;
        // Expand this container if it's a container
        if(nav_is_container(val)){
            bs_add(&nav->expanded, nav_get_container_id(val), &nav->allocator);
        }
    }

    // Recursively search children
    if(nav_is_container(val)){
        int64_t len = drjson_len(nav->jctx, val);

        if(val.kind == DRJSON_ARRAY || val.kind == DRJSON_ARRAY_VIEW){
            for(int64_t i = 0; i < len; i++){
                DrJsonValue child = drjson_get_by_index(nav->jctx, val, i);
                // Recursively search child - if it or its descendants match, expand this container
                if(nav_search_recursive_helper(nav, child, (DrJsonAtom){0}, query, query_len)){
                    found_match = 1;
                    bs_add(&nav->expanded, nav_get_container_id(val), &nav->allocator);
                }
            }
        }
        else {
            DrJsonValue items = drjson_object_items(val);
            int64_t items_len = drjson_len(nav->jctx, items);
            for(int64_t i = 0; i < items_len; i += 2){
                DrJsonValue k = drjson_get_by_index(nav->jctx, items, i);
                DrJsonValue v = drjson_get_by_index(nav->jctx, items, i + 1);
                // Recursively search child - if it or its descendants match, expand this container
                if(nav_search_recursive_helper(nav, v, k.atom, query, query_len)){
                    found_match = 1;
                    bs_add(&nav->expanded, nav_get_container_id(val), &nav->allocator);
                }
            }
        }
    }

    return found_match;
}

// Navigate from a container item to a field specified by path
// Returns the index of the item at the path, or the original index if navigation fails
static
size_t
nav_navigate_to_path(JsonNav* nav, size_t container_idx, const DrJsonPath* path){
    if(path->count == 0) return container_idx;

    size_t current_idx = container_idx;
    DrJsonValue current_val = nav->items[current_idx].value;

    // Expand the container if needed
    if(nav_is_container(current_val) && !bs_contains(&nav->expanded, nav_get_container_id(current_val))){
        bs_add(&nav->expanded, nav_get_container_id(current_val), &nav->allocator);
        nav->needs_rebuild = 1;
        nav_rebuild(nav);
    }

    // Navigate through each path segment
    for(size_t seg_idx = 0; seg_idx < path->count; seg_idx++){
        DrJsonPathSegment segment = path->segments[seg_idx];

        // Find the child item that matches this segment
        _Bool found = 0;
        int child_depth = nav->items[current_idx].depth + 1;

        // Look through children (items with greater depth immediately following)
        for(size_t i = current_idx + 1; i < nav->item_count; i++){
            NavItem* child = &nav->items[i];

            // Stop if we've left this container's children
            if(child->depth < child_depth) break;

            // Skip if not a direct child
            if(child->depth != child_depth) continue;

            // Check if this child matches the segment
            _Bool matches = 0;
            if(segment.kind == DRJSON_PATH_KEY && child->key.bits == segment.key.bits){
                matches = 1;
            }
            else if(segment.kind == DRJSON_PATH_INDEX && child->index == segment.index){
                matches = 1;
            }

            if(matches){
                current_idx = i;
                current_val = child->value;
                found = 1;

                // If there are more segments and this is a container, expand it
                if(seg_idx + 1 < path->count && nav_is_container(current_val)){
                    if(!bs_contains(&nav->expanded, nav_get_container_id(current_val))){
                        bs_add(&nav->expanded, nav_get_container_id(current_val), &nav->allocator);
                        nav->needs_rebuild = 1;
                        nav_rebuild(nav);
                        // Restart navigation from the beginning since indices changed
                        return nav_navigate_to_path(nav, container_idx, path);
                    }
                }
                break;
            }
        }

        if(!found){
            // Path navigation failed, return original index
            return container_idx;
        }
    }

    return current_idx;
}

// Unified search function - searches forward or backward with optional container expansion
// direction: 1 = forward, -1 = backward
static
void
nav_search_internal(JsonNav* nav, int direction){
    if(nav->search_buffer.length == 0) return;
    if(nav->item_count == 0) return;

    if(direction > 0){
        // Search forward from current position + 1
        for(size_t i = nav->cursor_pos + 1; i < nav->item_count; i++){
            NavItem* item = &nav->items[i];

            // Check if this visible item matches
            if(nav_item_matches_query(nav, item, nav->search_buffer.data, nav->search_buffer.length)){
                // For SEARCH_QUERY mode, navigate to the actual field
                if(nav->search_mode == SEARCH_QUERY){
                    size_t path_idx = nav_navigate_to_path(nav, i, &nav->search_query_path);
                    // If the path result is an array/object and there's a pattern, find the first matching child element
                    if(path_idx < nav->item_count && nav_is_container(nav->items[path_idx].value) &&
                       (nav->search_pattern_len > 0 || nav->search_numeric.is_numeric)){
                        DrJsonValue container = nav->items[path_idx].value;
                        // Expand the container if not already expanded
                        if(!bs_contains(&nav->expanded, nav_get_container_id(container))){
                            bs_add(&nav->expanded, nav_get_container_id(container), &nav->allocator);
                            nav->needs_rebuild = 1;
                            nav_rebuild(nav);
                        }
                        // Now look for matching children (check value directly, not via path evaluation)
                        int child_depth = nav->items[path_idx].depth + 1;
                        for(size_t j = path_idx + 1; j < nav->item_count; j++){
                            if(nav->items[j].depth < child_depth) break; // Left container
                            if(nav->items[j].depth != child_depth) continue; // Not direct child

                            // For flat view items, check elements in the row
                            if(nav->items[j].is_flat_view && nav->items[j].value.kind == DRJSON_ARRAY){
                                int64_t len = drjson_len(nav->jctx, nav->items[j].value);
                                size_t WRAP_WIDTH = 10;
                                int64_t row_start = (int64_t)nav->items[j].index * (int64_t)WRAP_WIDTH;
                                int64_t row_end = row_start + (int64_t)WRAP_WIDTH;
                                if(row_end > len) row_end = len;

                                _Bool found_in_flat = 0;
                                for(int64_t k = row_start; k < row_end; k++){
                                    DrJsonValue elem = drjson_get_by_index(nav->jctx, nav->items[j].value, k);
                                    if(nav_value_matches_pattern(nav, elem)){
                                        path_idx = j;
                                        found_in_flat = 1;
                                        break;
                                    }
                                }
                                if(found_in_flat) break;
                            }
                            // Check if this child's value matches the pattern directly
                            else if(nav_value_matches_pattern(nav, nav->items[j].value)){
                                path_idx = j;
                                break;
                            }
                        }
                    }
                    nav->cursor_pos = path_idx;
                }
                else {
                    nav->cursor_pos = i;
                }
                return;
            }

            // If expanding and it's a collapsed container, check inside it
            if(nav_is_container(item->value) && !bs_contains(&nav->expanded, nav_get_container_id(item->value))){
                if(nav_contains_match(nav, item->value, item->key, nav->search_buffer.data, nav->search_buffer.length)){
                    // Found a match inside! Expand ONLY this container (not recursively)
                    bs_add(&nav->expanded, nav_get_container_id(item->value), &nav->allocator);
                    nav->needs_rebuild = 1;
                    nav_rebuild(nav);

                    // Continue searching from this position - the children are now visible
                    // Restart the search from position i by recursively calling ourselves
                    // This allows us to incrementally expand nested containers
                    nav->cursor_pos = i;
                    nav_search_internal(nav, direction);
                    return;
                }
            }
        }

        // No match found forward - wrap to beginning
        for(size_t i = 0; i <= nav->cursor_pos && i < nav->item_count; i++){
            NavItem* item = &nav->items[i];

            // Check if this visible item matches
            if(nav_item_matches_query(nav, item, nav->search_buffer.data, nav->search_buffer.length)){
                // For SEARCH_QUERY mode, navigate to the actual field
                if(nav->search_mode == SEARCH_QUERY){
                    size_t path_idx = nav_navigate_to_path(nav, i, &nav->search_query_path);
                    // If the path result is an array/object and there's a pattern, find the first matching child element
                    if(path_idx < nav->item_count && nav_is_container(nav->items[path_idx].value) &&
                       (nav->search_pattern_len > 0 || nav->search_numeric.is_numeric)){
                        DrJsonValue container = nav->items[path_idx].value;
                        // Expand the container if not already expanded
                        if(!bs_contains(&nav->expanded, nav_get_container_id(container))){
                            bs_add(&nav->expanded, nav_get_container_id(container), &nav->allocator);
                            nav->needs_rebuild = 1;
                            nav_rebuild(nav);
                        }
                        // Now look for matching children (check value directly, not via path evaluation)
                        int child_depth = nav->items[path_idx].depth + 1;
                        for(size_t j = path_idx + 1; j < nav->item_count; j++){
                            if(nav->items[j].depth < child_depth) break; // Left container
                            if(nav->items[j].depth != child_depth) continue; // Not direct child

                            // For flat view items, check elements in the row
                            if(nav->items[j].is_flat_view && nav->items[j].value.kind == DRJSON_ARRAY){
                                int64_t len = drjson_len(nav->jctx, nav->items[j].value);
                                size_t WRAP_WIDTH = 10;
                                int64_t row_start = (int64_t)nav->items[j].index * (int64_t)WRAP_WIDTH;
                                int64_t row_end = row_start + (int64_t)WRAP_WIDTH;
                                if(row_end > len) row_end = len;

                                _Bool found_in_flat = 0;
                                for(int64_t k = row_start; k < row_end; k++){
                                    DrJsonValue elem = drjson_get_by_index(nav->jctx, nav->items[j].value, k);
                                    if(nav_value_matches_pattern(nav, elem)){
                                        path_idx = j;
                                        found_in_flat = 1;
                                        break;
                                    }
                                }
                                if(found_in_flat) break;
                            }
                            // Check if this child's value matches the pattern directly
                            else if(nav_value_matches_pattern(nav, nav->items[j].value)){
                                path_idx = j;
                                break;
                            }
                        }
                    }
                    nav->cursor_pos = path_idx;
                }
                else {
                    nav->cursor_pos = i;
                }
                return;
            }

            // If expanding and it's a collapsed container, check inside it
            if(nav_is_container(item->value) && !bs_contains(&nav->expanded, nav_get_container_id(item->value))){
                if(nav_contains_match(nav, item->value, item->key, nav->search_buffer.data, nav->search_buffer.length)){
                    // Found a match inside! Expand ONLY this container (not recursively)
                    bs_add(&nav->expanded, nav_get_container_id(item->value), &nav->allocator);
                    nav->needs_rebuild = 1;
                    nav_rebuild(nav);

                    // Continue searching from this position - the children are now visible
                    // Restart the search from position i by recursively calling ourselves
                    // This allows us to incrementally expand nested containers
                    nav->cursor_pos = i;
                    nav_search_internal(nav, direction);
                    return;
                }
            }
        }
    }
    else {
        // Search backward from current position - 1
        if(nav->cursor_pos > 0){
            for(size_t i = nav->cursor_pos; i > 0; i--){
                size_t idx = i - 1;
                NavItem* item = &nav->items[idx];

                // Check if this visible item matches
                if(nav_item_matches_query(nav, item, nav->search_buffer.data, nav->search_buffer.length)){
                    // For SEARCH_QUERY mode, navigate to the actual field
                    if(nav->search_mode == SEARCH_QUERY){
                        size_t path_idx = nav_navigate_to_path(nav, idx, &nav->search_query_path);
                        // If the path result is an array/object, expand it and find the first matching child element
                        if(path_idx < nav->item_count && nav_is_container(nav->items[path_idx].value)){
                            DrJsonValue container = nav->items[path_idx].value;
                            // Expand the container if not already expanded
                            if(!bs_contains(&nav->expanded, nav_get_container_id(container))){
                                bs_add(&nav->expanded, nav_get_container_id(container), &nav->allocator);
                                nav->needs_rebuild = 1;
                                nav_rebuild(nav);
                            }
                            // Now look for matching children
                            int child_depth = nav->items[path_idx].depth + 1;
                            for(size_t j = path_idx + 1; j < nav->item_count; j++){
                                if(nav->items[j].depth < child_depth) break; // Left container
                                if(nav->items[j].depth != child_depth) continue; // Not direct child
                                // Check if this child matches the pattern
                                if(nav_item_matches_query(nav, &nav->items[j], nav->search_buffer.data, nav->search_buffer.length)){
                                    path_idx = j;
                                    break;
                                }
                            }
                        }
                        nav->cursor_pos = path_idx;
                    }
                    else {
                        nav->cursor_pos = idx;
                    }
                    return;
                }

                // If expanding and it's a collapsed container, check inside it
                if(nav_is_container(item->value) && !bs_contains(&nav->expanded, nav_get_container_id(item->value))){
                    if(nav_contains_match(nav, item->value, item->key, nav->search_buffer.data, nav->search_buffer.length)){
                        // Found a match inside! Expand ONLY this container (not recursively)
                        bs_add(&nav->expanded, nav_get_container_id(item->value), &nav->allocator);
                        nav->needs_rebuild = 1;
                        nav_rebuild(nav);

                        // Continue searching backward from this position
                        // Restart the search from position idx by recursively calling ourselves
                        nav->cursor_pos = idx;
                        nav_search_internal(nav, direction);
                        return;
                    }
                }
            }
        }

        // No match found backward - wrap to end
        for(size_t i = nav->item_count; i > nav->cursor_pos && i > 0; i--){
            size_t idx = i - 1;
            NavItem* item = &nav->items[idx];

            // Check if this visible item matches
            if(nav_item_matches_query(nav, item, nav->search_buffer.data, nav->search_buffer.length)){
                nav->cursor_pos = idx;
                return;
            }

            // If expanding and it's a collapsed container, check inside it
            if(nav_is_container(item->value) && !bs_contains(&nav->expanded, nav_get_container_id(item->value))){
                if(nav_contains_match(nav, item->value, item->key, nav->search_buffer.data, nav->search_buffer.length)){
                    // Found a match inside! Expand and search for last match
                    nav_search_recursive_helper(nav, item->value, item->key, nav->search_buffer.data, nav->search_buffer.length);
                    nav->needs_rebuild = 1;
                    nav_rebuild(nav);

                    // Search from this container forward to find last match inside it
                    size_t last_match = idx;
                    for(size_t j = idx; j < nav->item_count; j++){
                        if(nav_item_matches_query(nav, &nav->items[j], nav->search_buffer.data, nav->search_buffer.length)){
                            last_match = j;
                        }
                        // Stop when we exit this container's children
                        if(j > idx && nav->items[j].depth <= item->depth){
                            break;
                        }
                    }
                    nav->cursor_pos = last_match;
                    return;
                }
            }
        }
    }

    // No match found - stay at current position
}

// Perform recursive search - find first match (expanding containers as needed)
static inline
void
nav_search_recursive(JsonNav* nav){
    nav_search_internal(nav, 1);
}

// Jump to next search match
static inline
void
nav_search_next(JsonNav* nav){
    nav_search_internal(nav, 1);
}

// Jump to previous search match
static inline
void
nav_search_prev(JsonNav* nav){
    nav_search_internal(nav, -1);
}

// Set up search from a string and mode
// This helper consolidates all the search setup logic in one place
// Returns 0 on success, non-zero if search couldn't be set up
static
int
nav_setup_search(JsonNav* nav, const char* search_str, size_t search_len, enum SearchMode mode){
    if(search_len == 0) return -1;
    if(search_len >= nav->search_buffer.capacity) return -1;

    // Copy search string to buffer
    memcpy(nav->search_buffer.data, search_str, search_len);
    nav->search_buffer.data[search_len] = '\0';
    nav->search_buffer.length = search_len;
    nav->search_buffer.cursor_pos = search_len;

    // Reset numeric search state
    nav->search_numeric.is_numeric = 0;
    nav->search_numeric.is_integer = 0;
    nav->search_numeric.int_value = 0;
    nav->search_numeric.double_value = 0;

    if(mode == SEARCH_QUERY){
        // Query-based search: path pattern
        // Use drjson_path_parse_greedy to parse the query part
        const char* remainder = NULL;
        DrJsonPath path = {0};
        int parse_result = drjson_path_parse_greedy(nav->jctx, search_str, search_len, &path, &remainder);

        if(parse_result == 0 && path.count > 0 && remainder != NULL){
            // Store the parsed path
            nav->search_query_path = path;

            // Parse pattern from remainder
            // Skip leading whitespace
            while(remainder < search_str + search_len && (*remainder == ' ' || *remainder == '\t')){
                remainder++;
            }
            // Optionally allow ':' after the query
            if(remainder < search_str + search_len && *remainder == ':'){
                remainder++;
                // Skip whitespace after ':'
                while(remainder < search_str + search_len && (*remainder == ' ' || *remainder == '\t')){
                    remainder++;
                }
            }

            // Rest is the pattern
            nav->search_pattern_len = (search_str + search_len) - remainder;
            if(nav->search_pattern_len > sizeof nav->search_pattern - 1){
                nav->search_pattern_len = sizeof nav->search_pattern - 1;
            }
            if(nav->search_pattern_len > 0){
                memcpy(nav->search_pattern, remainder, nav->search_pattern_len);
                nav->search_pattern[nav->search_pattern_len] = '\0';

                // Try to parse pattern as number for fast numeric comparison
                // Try int64 first
                Int64Result int_res = parse_int64(nav->search_pattern, nav->search_pattern_len);
                if(int_res.errored == PARSENUMBER_NO_ERROR){
                    nav->search_numeric.is_numeric = 1;
                    nav->search_numeric.is_integer = 1;
                    nav->search_numeric.int_value = int_res.result;
                }
                else {
                    // Try uint64
                    Uint64Result uint_res = parse_uint64(nav->search_pattern, nav->search_pattern_len);
                    if(uint_res.errored == PARSENUMBER_NO_ERROR){
                        nav->search_numeric.is_numeric = 1;
                        nav->search_numeric.is_integer = 1;
                        nav->search_numeric.int_value = (int64_t)uint_res.result;
                    }
                    else {
                        // Try double
                        DoubleResult double_res = parse_double(nav->search_pattern, nav->search_pattern_len);
                        if(double_res.errored == PARSENUMBER_NO_ERROR){
                            nav->search_numeric.is_numeric = 1;
                            nav->search_numeric.is_integer = 0;
                            nav->search_numeric.double_value = double_res.result;
                        }
                    }
                }
            }
            else {
                nav->search_pattern_len = 0;
                nav->search_pattern[0] = '\0';
            }
            // Successfully set up query search
            nav->search_mode = SEARCH_QUERY;
            return 0;
        }
        else {
            // Parse failed, can't do query search
            return -1;
        }
    }
    else {
        // Regular recursive search
        nav->search_mode = SEARCH_RECURSIVE;

        // Try to parse search buffer as number for numeric comparison
        // Try int64 first
        Int64Result int_res = parse_int64(search_str, search_len);
        if(int_res.errored == PARSENUMBER_NO_ERROR){
            nav->search_numeric.is_numeric = 1;
            nav->search_numeric.is_integer = 1;
            nav->search_numeric.int_value = int_res.result;
        }
        else {
            // Try uint64
            Uint64Result uint_res = parse_uint64(search_str, search_len);
            if(uint_res.errored == PARSENUMBER_NO_ERROR){
                nav->search_numeric.is_numeric = 1;
                nav->search_numeric.is_integer = 1;
                nav->search_numeric.int_value = (int64_t)uint_res.result;
            }
            else {
                // Try double
                DoubleResult double_res = parse_double(search_str, search_len);
                if(double_res.errored == PARSENUMBER_NO_ERROR){
                    nav->search_numeric.is_numeric = 1;
                    nav->search_numeric.is_integer = 0;
                    nav->search_numeric.double_value = double_res.result;
                }
            }
        }

        return 0;
    }
}

static inline
void
nav_center_cursor(JsonNav* nav, int viewport_height){
    if(nav->item_count == 0) return;

    // Account for status line at top and breadcrumb at bottom
    int visible_rows = viewport_height - 2;
    if(visible_rows < 1) visible_rows = 1;

    // Center cursor in viewport
    int half_screen = visible_rows / 2;
    if(nav->cursor_pos >= (size_t)half_screen){
        nav->scroll_offset = nav->cursor_pos - (size_t)half_screen;
    }
    else {
        nav->scroll_offset = 0;
    }

    // Don't scroll past the end
    if(nav->scroll_offset + (size_t)visible_rows > nav->item_count){
        if(nav->item_count > (size_t)visible_rows){
            nav->scroll_offset = nav->item_count - (size_t)visible_rows;
        }
        else {
            nav->scroll_offset = 0;
        }
    }
}

static inline
DrJsonValue
nav_get_current_value(const JsonNav* nav){
    if(nav->item_count == 0)
        return drjson_make_error(DRJSON_ERROR_INDEX_ERROR, "no items");
    return nav->items[nav->cursor_pos].value;
}

//------------------------------------------------------------
// Message Display
//------------------------------------------------------------

// Set a message to display to the user
static
void
nav_set_messagef(JsonNav* nav, const char* fmt, ...){
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(nav->message, sizeof nav->message, fmt, args);
    va_end(args);
    if(len < 0){
        nav->message_length = 0;
    }
    else if((size_t)len >= sizeof nav->message){
        nav->message_length = sizeof nav->message - 1;
    }
    else{
        nav->message_length = (size_t)len;
    }
}

// Clear the current message
static inline
void
nav_clear_message(JsonNav* nav){
    nav->message_length = 0;
}

DRJSON_WARN_UNUSED
static int parse_as_string(DrJsonContext* jctx, const char* txt, size_t len, DrJsonAtom* outatom);
DRJSON_WARN_UNUSED
static int parse_as_value(DrJsonContext* jctx, const char* txt, size_t len, DrJsonValue* outvalue);

//------------------------------------------------------------
// Command Mode
//------------------------------------------------------------

// Command handler function type
// Returns: 0 = success, -1 = error, 1 = quit requested
typedef int (CommandHandler)(JsonNav* nav, CmdArgs* args);

typedef struct Command Command;
struct Command {
    StringView name;
    StringView signature;
    StringView short_help;
    CommandHandler* handler;
};
static CommandHandler cmd_help, cmd_write, cmd_quit, cmd_open, cmd_pwd, cmd_cd, cmd_yank, cmd_paste, cmd_query, cmd_path, cmd_focus, cmd_unfocus, cmd_wq, cmd_reload, cmd_sort, cmd_filter, cmd_move;

static size_t nav_build_json_path(JsonNav* nav, char* buf, size_t buf_size);

static const Command commands[] = {
    {SV("help"),  SV(":help"), SV("  Show help"),         cmd_help},
    {SV("h"),     SV(":h"), SV("  Show help"),         cmd_help},
    {SV("open"),  SV(":open [--braceless] <file>"), SV("  Open JSON at <file>"), cmd_open},
    {SV("o"),     SV(":o [--braceless] <file>"), SV("  Open JSON at <file>"), cmd_open},
    {SV("edit"),  SV(":edit [--braceless] <file>"), SV("  Open JSON at <file>"), cmd_open},
    {SV("e"),     SV(":e [--braceless] <file>"), SV("  Open JSON at <file>"), cmd_open},
    {SV("reload"), SV(":reload"), SV("  Reload file from disk (preserves braceless)"), cmd_reload},
    {SV("e!"),    SV(":e!"), SV("  Reload file from disk (preserves braceless)"), cmd_reload},
    {SV("save"),  SV(":save [--braceless|--no-braceless] <file>"), SV("  Save JSON to <file>"), cmd_write},
    {SV("w"),     SV(":w [--braceless|--no-braceless] <file>"), SV("  Save JSON to <file>"), cmd_write},
    {SV("quit"),  SV(":quit"), SV("  Quit"),              cmd_quit},
    {SV("q"),     SV(":q"), SV("  Quit"),              cmd_quit},
    {SV("exit"),  SV(":exit"), SV("  Quit"),              cmd_quit},
    {SV("wq"),    SV(":wq"), SV("  Write and quit"),      cmd_wq},
    {SV("pwd"),   SV(":pwd"), SV("  Print working directory"), cmd_pwd},
    {SV("cd"),    SV(":cd <dir>"), SV("  Change directory"), cmd_cd},
    {SV("yank"),  SV(":yank"), SV("  Yank (copy) current value to clipboard"), cmd_yank},
    {SV("y"),     SV(":y"), SV("  Yank (copy) current value to clipboard"), cmd_yank},
    {SV("paste"), SV(":paste"), SV("  Paste from clipboard"), cmd_paste},
    {SV("p"),     SV(":p"), SV("  Paste from clipboard"), cmd_paste},
    {SV("query"), SV(":query <path>"), SV("  Navigate to path (e.g., foo.bar[0].baz)"), cmd_query},
    {SV("path"), SV(":path"), SV("  Yank (copy) current item's JSON path to clipboard"), cmd_path},
    {SV("focus"), SV(":focus"), SV("  Focus on the current array or object"), cmd_focus},
    {SV("unfocus"), SV(":unfocus"), SV("  Return to the previous (less focused) view"), cmd_unfocus},
    {SV("sort"), SV(":sort [<query>] [keys|values] [asc|desc]"), SV("Sort array or object. Can sort by query."), cmd_sort},
    {SV("filter"), SV(":filter <query>"), SV("  Filter array/object based on a query"), cmd_filter},
    {SV("f"), SV(":f <query>"), SV("  Alias for :filter"), cmd_filter},
    {SV("move"), SV(":move <index>"), SV("  Move current item to <index>"), cmd_move},
    {SV("m"), SV(":m <index>"), SV("  Move current item to <index>"), cmd_move},
};

static
const Command* _Nullable
cmd_by_name(StringView name){
    for(size_t i = 0; i < sizeof commands / sizeof commands[0]; i++){
        if(SV_equals(commands[i].name, name))
            return &commands[i];
    }
    return NULL;
}

static void build_command_helps(void);
static const StringView* cmd_helps;
static size_t cmd_helps_count;

enum {
    CMD_ERROR = -1,
    CMD_OK = 0,
    CMD_QUIT = 1,
};

// Command handlers

static
int
nav_load_file(JsonNav* nav, const char* filepath, _Bool use_braceless){
    LongString file_content = {0};
    if(read_file(filepath, &file_content) != 0){
        nav_set_messagef(nav, "Error: Could not read file '%s'", filepath);
        return CMD_ERROR;
    }

    DrJsonParseContext pctx = {
        .ctx = nav->jctx,
        .begin = file_content.text,
        .cursor = file_content.text,
        .end = file_content.text + file_content.length,
        .depth = 0,
    };
    unsigned parse_flags = DRJSON_PARSE_FLAG_ERROR_ON_TRAILING;
    if(globals.intern) parse_flags |= DRJSON_PARSE_FLAG_INTERN_OBJECTS;
    if(use_braceless) parse_flags |= DRJSON_PARSE_FLAG_BRACELESS_OBJECT;
    DrJsonValue new_root = drjson_parse(&pctx, parse_flags);

    if(new_root.kind == DRJSON_ERROR){
        size_t line=0, col=0;
        drjson_get_line_column(&pctx, &line, &col);
        nav_set_messagef(nav, "Error parsing '%s': %s at line %zu col %zu", filepath, new_root.err_mess, line, col);
        free((void*)file_content.text);
        drjson_gc(nav->jctx, &nav->root, 1);
        return CMD_ERROR;
    }

    free((void*)file_content.text); // done with this now
    nav->root = new_root;
    nav->was_opened_with_braceless = use_braceless;
    nav_reinit(nav);
    nav->focus_stack_count = 0;
    drjson_gc(nav->jctx, &nav->root, 1); // Free the old root and other garbage

    return CMD_OK;
}

static
int
cmd_open(JsonNav* nav, CmdArgs* args){
    // Get optional --braceless flag
    _Bool use_braceless = 0;
    int err = cmd_get_arg_bool(args, SV("--braceless"), &use_braceless);
    if(err != CMD_ARG_ERROR_NONE && err != CMD_ARG_ERROR_MISSING_BUT_OPTIONAL){
        nav_set_messagef(nav, "Error parsing --braceless flag");
        return CMD_ERROR;
    }

    // Get required file argument
    StringView filepath_sv = {0};
    err = cmd_get_arg_string(args, SV("file"), &filepath_sv);
    if(err == CMD_ARG_ERROR_MISSING || err == CMD_ARG_ERROR_MISSING_BUT_OPTIONAL){
        nav_set_messagef(nav, "Error: No filename provided");
        return CMD_ERROR;
    }
    if(err != CMD_ARG_ERROR_NONE){
        nav_set_messagef(nav, "Error parsing filename");
        return CMD_ERROR;
    }

    // Expand ~ and copy filename to null-terminated buffer
    char filepath[1024];
    if(expand_tilde_to_buffer(filepath_sv.text, filepath_sv.length, filepath, sizeof filepath) != 0){
        nav_set_messagef(nav, "Error: Could not expand path");
        return CMD_ERROR;
    }

    if(nav_load_file(nav, filepath, use_braceless) != CMD_OK){
        return CMD_ERROR; // nav_load_file already set the message
    }

    // Update filename on successful load
    size_t expanded_len = strlen(filepath);
    if(expanded_len >= sizeof nav->filename) expanded_len = sizeof nav->filename - 1;
    memcpy(nav->filename, filepath, expanded_len);
    nav->filename[expanded_len] = '\0';

    nav_set_messagef(nav, "Opened '%s'%s", filepath, use_braceless ? " (braceless)" : "");
    return CMD_OK;
}

static
int
cmd_write(JsonNav* nav, CmdArgs* args){
    // Check for --braceless or --no-braceless flags (they're alternatives)
    _Bool use_braceless = nav->was_opened_with_braceless; // Default to current setting
    _Bool braceless_specified = 0;

    _Bool flag_braceless = 0;
    int err = cmd_get_arg_bool(args, SV("--braceless"), &flag_braceless);
    if(err == CMD_ARG_ERROR_NONE && flag_braceless){
        use_braceless = 1;
        braceless_specified = 1;
    }

    _Bool flag_no_braceless = 0;
    err = cmd_get_arg_bool(args, SV("--no-braceless"), &flag_no_braceless);
    if(err == CMD_ARG_ERROR_NONE && flag_no_braceless){
        use_braceless = 0;
        braceless_specified = 1;
    }

    // Get required file argument
    StringView filepath_sv = {0};
    err = cmd_get_arg_string(args, SV("file"), &filepath_sv);
    if(err == CMD_ARG_ERROR_MISSING || err == CMD_ARG_ERROR_MISSING_BUT_OPTIONAL){
        nav_set_messagef(nav, "Error: No filename provided");
        return CMD_ERROR;
    }
    if(err != CMD_ARG_ERROR_NONE){
        nav_set_messagef(nav, "Error parsing filename");
        return CMD_ERROR;
    }

    // Expand ~ and copy filename to null-terminated buffer
    char filepath[1024];
    if(expand_tilde_to_buffer(filepath_sv.text, filepath_sv.length, filepath, sizeof filepath) != 0){
        nav_set_messagef(nav, "Error: Could not expand path");
        return CMD_ERROR;
    }

    // Write JSON to file
    FILE* fp = fopen(filepath, "wb");
    if(!fp){
        nav_set_messagef(nav, "Error: Could not open file '%s' for writing", filepath);
        return CMD_ERROR;
    }

    int print_err = drjson_print_value_fp(nav->jctx, fp, nav->root, 0, DRJSON_PRETTY_PRINT | (use_braceless ? DRJSON_PRINT_BRACELESS : 0));
    int close_err = fclose(fp);

    if(print_err || close_err){
        nav_set_messagef(nav, "Error: Failed to write to '%s'", filepath);
        return CMD_ERROR;
    }

    nav_set_messagef(nav, "Wrote to '%s'%s", filepath, braceless_specified ? (use_braceless ? " (braceless)" : " (with braces)") : "");
    return CMD_OK;
}

static
int
cmd_quit(JsonNav* nav, CmdArgs* args){
    (void)nav;
    (void)args;
    return CMD_QUIT; // Signal quit
}

static
int
cmd_help(JsonNav* nav, CmdArgs* args){
    (void)args;
    build_command_helps();
    if(cmd_helps){
        nav->show_help = 1;
        nav->help_lines = cmd_helps;
        nav->help_lines_count = cmd_helps_count;
        nav->help_page = 0;
    }
    return CMD_OK;
}

static
int
cmd_pwd(JsonNav* nav, CmdArgs* args){
    (void)args;

    char cwd[1024];
    #ifdef _WIN32
    DWORD len = GetCurrentDirectoryA(sizeof cwd, cwd);
    if(len == 0 || len >= sizeof cwd){
        nav_set_messagef(nav, "Error: Could not get current directory");
        return CMD_ERROR;
    }
    #else
    if(getcwd(cwd, sizeof cwd) == NULL){
        nav_set_messagef(nav, "Error: Could not get current directory: %s", strerror(errno));
        return CMD_ERROR;
    }
    #endif

    nav_set_messagef(nav, "%s", cwd);
    return CMD_OK;
}

static
int
cmd_cd(JsonNav* nav, CmdArgs* args){
    char dirpath[1024];

    // Get dir argument (defaults to "~" if not provided)
    StringView dir_sv = {0};
    int err = cmd_get_arg_string(args, SV("dir"), &dir_sv);
    if(err == CMD_ARG_ERROR_MISSING || err == CMD_ARG_ERROR_MISSING_BUT_OPTIONAL){
        // No argument - change to home directory
        dir_sv = SV("~");
    }
    else if(err != CMD_ARG_ERROR_NONE){
        nav_set_messagef(nav, "Error parsing directory");
        return CMD_ERROR;
    }

    // Expand ~ and copy directory path to null-terminated buffer
    if(expand_tilde_to_buffer(dir_sv.text, dir_sv.length, dirpath, sizeof dirpath) != 0){
        nav_set_messagef(nav, "Error: Could not expand path");
        return CMD_ERROR;
    }

    #ifdef _WIN32
    if(!SetCurrentDirectoryA(dirpath)){
        nav_set_messagef(nav, "Error: Could not change directory to '%s'", dirpath);
        return CMD_ERROR;
    }
    #else
    if(chdir(dirpath) != 0){
        nav_set_messagef(nav, "Error: Could not change directory to '%s': %s", dirpath, strerror(errno));
        return CMD_ERROR;
    }
    #endif

    nav_set_messagef(nav, "Changed to %s", dirpath);
    return CMD_OK;
}

#ifdef _WIN32
// Copy text to system clipboard (Windows only)
// Returns 0 on success, -1 on failure
static
int
copy_to_clipboard(const char* text, size_t len){
    if(!OpenClipboard(NULL)){
        return -1;
    }

    EmptyClipboard();

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len + 1);
    if(!hMem){
        CloseClipboard();
        return -1;
    }

    char* pMem = (char*)GlobalLock(hMem);
    if(!pMem){
        GlobalFree(hMem);
        CloseClipboard();
        return -1;
    }

    memcpy(pMem, text, len);
    pMem[len] = '\0';
    GlobalUnlock(hMem);

    if(!SetClipboardData(CF_TEXT, hMem)){
        GlobalFree(hMem);
        CloseClipboard();
        return -1;
    }

    CloseClipboard();
    return 0;
}
#endif

#ifdef __APPLE__
// Cached Objective-C runtime state for clipboard operations
typedef void*_Null_unspecified (*objc_getClass_t)(const char*);
typedef void*_Null_unspecified (*sel_registerName_t)(const char*);
typedef void*_Null_unspecified (*objc_msgSend_t)(void*, void*, ...);

typedef struct ObjCClipboard ObjCClipboard;
struct ObjCClipboard {
    // Libraries
    void* objc_lib;
    void* appkit;

    // Runtime functions
    objc_getClass_t objc_getClass_fn;
    sel_registerName_t sel_registerName_fn;
    objc_msgSend_t objc_msgSend_fn;

    // Classes
    void* NSPasteboard;
    void* NSString;
    void* NSAutoreleasePool;

    // Common selectors
    void* sel_generalPasteboard;
    void* sel_alloc;
    void* sel_init;
    void* sel_drain;
    void* sel_retain;

    // Write-specific selectors
    void* sel_clearContents;
    void* sel_setString;
    void* sel_stringWithUTF8String;

    // Read-specific selectors
    void* sel_stringForType;
    void* sel_UTF8String;

    // Cached instances
    void* pasteboard;
    void* pasteboardType;
};

static
ObjCClipboard* _Nullable
get_objc_clipboard(void){
    static ObjCClipboard cached = {0};
    static _Bool initialized = 0;

    if(initialized) return &cached;

    // Load Objective-C runtime
    cached.objc_lib = dlopen("/usr/lib/libobjc.dylib", RTLD_LAZY);
    if(!cached.objc_lib){
        LOG("Couldn't open objc_lib\n");
        return NULL;
    }

    // Load AppKit framework
    cached.appkit = dlopen("/System/Library/Frameworks/AppKit.framework/AppKit", RTLD_LAZY);
    if(!cached.appkit){
        LOG("Couldn't open appkit\n");
        return NULL;
    }

    // Get Objective-C runtime functions
    cached.objc_getClass_fn = (objc_getClass_t)dlsym(cached.objc_lib, "objc_getClass");
    cached.sel_registerName_fn = (sel_registerName_t)dlsym(cached.objc_lib, "sel_registerName");
    cached.objc_msgSend_fn = (objc_msgSend_t)dlsym(cached.objc_lib, "objc_msgSend");

    if(!cached.objc_getClass_fn || !cached.sel_registerName_fn || !cached.objc_msgSend_fn){
        if(!cached.objc_getClass_fn) LOG("Couldn't get objc_getClass\n");
        if(!cached.sel_registerName_fn) LOG("Couldn't get sel_registerName\n");
        if(!cached.objc_msgSend_fn) LOG("Couldn't get objc_msgSend\n");
        return NULL;
    }

    // Get classes
    cached.NSPasteboard = cached.objc_getClass_fn("NSPasteboard");
    cached.NSString = cached.objc_getClass_fn("NSString");
    cached.NSAutoreleasePool = cached.objc_getClass_fn("NSAutoreleasePool");

    if(!cached.NSPasteboard || !cached.NSString || !cached.NSAutoreleasePool){
        if(!cached.NSPasteboard) LOG("Couldn't get NSPasteboard\n");
        if(!cached.NSString) LOG("Couldn't get NSString\n");
        if(!cached.NSAutoreleasePool) LOG("Couldn't get NSAutoreleasePool\n");
        return NULL;
    }

    // Get common selectors
    cached.sel_generalPasteboard = cached.sel_registerName_fn("generalPasteboard");
    cached.sel_alloc = cached.sel_registerName_fn("alloc");
    cached.sel_init = cached.sel_registerName_fn("init");
    cached.sel_drain = cached.sel_registerName_fn("drain");
    cached.sel_retain = cached.sel_registerName_fn("retain");

    // Get write-specific selectors
    cached.sel_clearContents = cached.sel_registerName_fn("clearContents");
    cached.sel_setString = cached.sel_registerName_fn("setString:forType:");
    cached.sel_stringWithUTF8String = cached.sel_registerName_fn("stringWithUTF8String:");

    // Get read-specific selectors
    cached.sel_stringForType = cached.sel_registerName_fn("stringForType:");
    cached.sel_UTF8String = cached.sel_registerName_fn("UTF8String");

    if(!cached.sel_generalPasteboard || !cached.sel_alloc || !cached.sel_init || !cached.sel_drain){
        return NULL;
    }

    // Get general pasteboard instance
    cached.pasteboard = ((void*(*)(void*, void*))cached.objc_msgSend_fn)(cached.NSPasteboard, cached.sel_generalPasteboard);
    if(!cached.pasteboard){
        LOG("couldn't get generalPasteboard\n");
        return NULL;
    }

    // Try to get NSPasteboardTypeString constant
    void** NSPasteboardTypeString_ptr = (void**)dlsym(cached.appkit, "NSPasteboardTypeString");
    if(NSPasteboardTypeString_ptr && *NSPasteboardTypeString_ptr){
        cached.pasteboardType = *NSPasteboardTypeString_ptr;
    }
    else {
        // Fallback: try old API
        void** NSStringPboardType_ptr = (void**)dlsym(cached.appkit, "NSStringPboardType");
        if(NSStringPboardType_ptr && *NSStringPboardType_ptr){
            cached.pasteboardType = *NSStringPboardType_ptr;
        }
        else {
            // Last resort: create string with UTI
            cached.pasteboardType = ((void*(*)(void*, void*, void*))cached.objc_msgSend_fn)(
                cached.NSString, cached.sel_stringWithUTF8String, "public.utf8-plain-text"
            );
            cached.pasteboardType = ((void*(*)(void*, void*))cached.objc_msgSend_fn)(cached.pasteboardType, cached.sel_retain);
        }
    }

    if(!cached.pasteboardType){
        LOG("Couldn't get pasteboardType\n");
        return NULL;
    }

    initialized = 1;
    return &cached;
}

// macOS clipboard using Objective-C runtime via dlopen
// Returns 0 on success, -1 on failure
static
int
macos_copy_to_clipboard(const char* text, size_t len){
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
    // LOG("Copying '%s' to clipboard\n", text);
    (void)len; // Unused parameter

    ObjCClipboard* objc = get_objc_clipboard();
    if(!objc) return -1;

    int result = -1;
    void* pool = ((void*(*)(void*, void*))objc->objc_msgSend_fn)(objc->NSAutoreleasePool, objc->sel_alloc);
    pool = ((void*(*)(void*, void*))objc->objc_msgSend_fn)(pool, objc->sel_init);
    if(!pool){
        LOG("couldn't allocate a pool\n");
        return -1;
    }

    // Create NSString from C string
    void* nsstring = ((void*(*)(void*, void*, const void*))objc->objc_msgSend_fn)(objc->NSString, objc->sel_stringWithUTF8String, text);
    if(!nsstring){
        LOG("couldn't make an nsstring from '%s'\n", text);
        goto finally;
    }

    // Clear pasteboard
    ((long(*)(void*, void*))objc->objc_msgSend_fn)(objc->pasteboard, objc->sel_clearContents);

    // Set string on pasteboard
    _Bool ok = ((_Bool(*)(void*, void*, void*, void*))objc->objc_msgSend_fn)(objc->pasteboard, objc->sel_setString, nsstring, objc->pasteboardType);

    if(!ok){
        LOG("Failed to setstring the pasteboard\n");
        goto finally;
    }

    result = 0;
    finally:;
    ((void(*)(void*, void*))objc->objc_msgSend_fn)(pool, objc->sel_drain);
#pragma clang diagnostic pop
    LOG("copied to clipboard?: result=%d\n", result);
    return result;
}
#endif

#if defined(_WIN32) || defined(__APPLE__)
// In-memory buffer for Windows and macOS clipboard
typedef struct {
    char* data;
    size_t size;
    size_t capacity;
} MemBuffer;

static
int
membuf_write(void* user_data, const void* data, size_t len){
    MemBuffer* buf = (MemBuffer*)user_data;

    // Ensure capacity
    size_t needed = buf->size + len;
    if(needed > buf->capacity){
        size_t new_cap = buf->capacity * 2;
        if(new_cap < needed) new_cap = needed;
        if(new_cap < 1024) new_cap = 1024;

        char* new_data = realloc(buf->data, new_cap);
        if(!new_data) return -1;

        buf->data = new_data;
        buf->capacity = new_cap;
    }

    memcpy(buf->data + buf->size, data, len);
    buf->size += len;
    return 0;
}
#endif

// Read text from system clipboard into LongString
// Returns 0 on success, -1 on failure
#ifdef _WIN32
static
DRJSON_WARN_UNUSED
int
read_from_clipboard(LongString* out){
    if(!OpenClipboard(NULL)){
        return -1;
    }

    HANDLE hData = GetClipboardData(CF_TEXT);
    if(!hData){
        CloseClipboard();
        return -1;
    }

    char* clipboard_data = (char*)GlobalLock(hData);
    if(!clipboard_data){
        CloseClipboard();
        return -1;
    }

    size_t len = strlen(clipboard_data);
    char* result = malloc(len + 1);
    if(!result){
        GlobalUnlock(hData);
        CloseClipboard();
        return -1;
    }

    memcpy(result, clipboard_data, len + 1);

    GlobalUnlock(hData);
    CloseClipboard();

    *out = (LongString){len, result};
    return 0;
}
#elif defined(__APPLE__)
static
DRJSON_WARN_UNUSED
int
read_from_clipboard(LongString* out){
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
    ObjCClipboard* objc = get_objc_clipboard();
    if(!objc) return -1;

    int result = -1;
    void* pool = ((void*(*)(void*, void*))objc->objc_msgSend_fn)(objc->NSAutoreleasePool, objc->sel_alloc);
    pool = ((void*(*)(void*, void*))objc->objc_msgSend_fn)(pool, objc->sel_init);
    if(!pool){
        return -1;
    }

    // Get string from pasteboard
    void* nsstring = ((void*(*)(void*, void*, void*))objc->objc_msgSend_fn)(objc->pasteboard, objc->sel_stringForType, objc->pasteboardType);

    if(!nsstring){
        // No string on clipboard
        goto finally;
    }

    // Convert NSString to C string
    const char* utf8_str = ((const char*(*)(void*, void*))objc->objc_msgSend_fn)(nsstring, objc->sel_UTF8String);
    if(!utf8_str){
        goto finally;
    }

    // Copy to LongString
    size_t len = strlen(utf8_str);
    char* copy = malloc(len + 1);
    if(!copy){
        goto finally;
    }

    memcpy(copy, utf8_str, len + 1);
    *out = (LongString){len, copy};
    result = 0;

    finally:;
    ((void(*)(void*, void*))objc->objc_msgSend_fn)(pool, objc->sel_drain);
    return result;
#pragma clang diagnostic pop
}
#else
static
DRJSON_WARN_UNUSED
int
read_from_clipboard(LongString* out){
    // Linux - try tmux, then xclip, then xsel
    FILE* pipe = NULL;

    // Try tmux
    if(getenv("TMUX")){
        pipe = popen("tmux show-buffer 2>/dev/null", "r");
        if(pipe){
            // Check if command succeeded
            int c = fgetc(pipe);
            if(c == EOF){
                pclose(pipe);
                pipe = NULL;
            }
            else {
                ungetc(c, pipe);
            }
        }
    }

    // Try xclip
    if(!pipe){
        pipe = popen("xclip -selection clipboard -o 2>/dev/null", "r");
        if(pipe){
            int c = fgetc(pipe);
            if(c == EOF){
                pclose(pipe);
                pipe = NULL;
            }
            else {
                ungetc(c, pipe);
            }
        }
    }

    // Try xsel
    if(!pipe){
        pipe = popen("xsel --clipboard --output 2>/dev/null", "r");
    }

    if(!pipe) return -1;

    int status = read_file_streamed(pipe, out);
    pclose(pipe);

    return status;
}
#endif

static
int
cmd_yank(JsonNav* nav, CmdArgs* args){
    (void)args;

    if(nav->item_count == 0){
        nav_set_messagef(nav, "Error: Nothing to yank");
        return CMD_ERROR;
    }

    NavItem* item = &nav->items[nav->cursor_pos];

    // Determine what to yank: if item has a key, yank {key: value}, else just value
    DrJsonValue yank_value = item->value;
    unsigned print_flags = 0;

    if(item->key.bits != 0){
        // Create a temporary object with {key: value} and use braceless printing
        DrJsonValue temp_obj = drjson_make_object(nav->jctx);
        drjson_object_set_item_atom(nav->jctx, temp_obj, item->key, item->value);
        yank_value = temp_obj;
        print_flags = DRJSON_PRINT_BRACELESS;
    }

    #ifdef _WIN32
    // Windows: use in-memory buffer
    MemBuffer buf = {0};
    DrJsonTextWriter writer = {
        .up = &buf,
        .write = membuf_write,
    };

    int print_err = drjson_print_value(nav->jctx, &writer, yank_value, 0, print_flags);

    if(print_err){
        free(buf.data);
        nav_set_messagef(nav, "Error: Could not serialize value");
        return CMD_ERROR;
    }

    if(buf.size > 10*1024*1024){  // 10MB limit
        free(buf.data);
        nav_set_messagef(nav, "Error: Value too large to yank");
        return CMD_ERROR;
    }

    int result = copy_to_clipboard(buf.data, buf.size);
    free(buf.data);

    if(result != 0){
        nav_set_messagef(nav, "Error: Could not copy to clipboard");
        return CMD_ERROR;
    }

    #elif defined(__APPLE__)
    // macOS: use in-memory buffer and native clipboard API
    MemBuffer buf = {0};
    DrJsonTextWriter writer = {
        .up = &buf,
        .write = membuf_write,
    };

    int print_err = drjson_print_value(nav->jctx, &writer, yank_value, 0, print_flags | DRJSON_APPEND_ZERO);

    if(print_err){
        free(buf.data);
        nav_set_messagef(nav, "Error: Could not serialize value");
        return CMD_ERROR;
    }

    if(0)
    if(buf.size > 10*1024*1024){  // 10MB limit
        free(buf.data);
        nav_set_messagef(nav, "Error: Value too large to yank");
        return CMD_ERROR;
    }

    int result = macos_copy_to_clipboard(buf.data, buf.size);
    free(buf.data);

    if(result != 0){
        nav_set_messagef(nav, "Error: Could not copy to clipboard");
        return CMD_ERROR;
    }

    #else
    // Linux: try tmux first (works in SSH), then fall back to X11 tools
    FILE* pipe = NULL;

    // Try tmux clipboard (works without X11)
    if(getenv("TMUX")){
        pipe = popen("tmux load-buffer - 2>/dev/null", "w");
    }

    // Fall back to X11 clipboard tools
    if(!pipe){
        pipe = popen("xclip -selection clipboard 2>/dev/null", "w");
    }
    if(!pipe){
        pipe = popen("xsel --clipboard --input 2>/dev/null", "w");
    }

    if(!pipe){
        nav_set_messagef(nav, "Error: Could not open clipboard command (tried tmux, xclip, xsel)");
        return CMD_ERROR;
    }

    int print_err = drjson_print_value_fp(nav->jctx, pipe, yank_value, 0, print_flags);
    int status = pclose(pipe);

    if(print_err || status != 0){
        nav_set_messagef(nav, "Error: Could not copy to clipboard");
        return CMD_ERROR;
    }
    #endif

    nav_set_messagef(nav, "Yanked to clipboard");
    return CMD_OK;
}

static
int
do_paste(JsonNav* nav, size_t cursor_pos, _Bool after){
    LongString clipboard_text = {0};
    // Read from clipboard
    if(read_from_clipboard(&clipboard_text) != 0){
        nav_set_messagef(nav, "Error: Could not read from clipboard");
        return CMD_ERROR;
    }

    if(clipboard_text.length == 0){
        free((void*)clipboard_text.text);
        nav_set_messagef(nav, "Error: Clipboard is empty");
        return CMD_ERROR;
    }

    LOG("Read %zu bytes from clipboard\n", clipboard_text.length);

    NavItem* parent = NULL;
    size_t insert_idx = 0;
    {
        NavItem* item = &nav->items[cursor_pos];
        // If cursor is on an expanded container, insert into it
        if(nav_is_expanded(nav, item->value)){
            parent = item;
            insert_idx = after?drjson_len(nav->jctx, parent->value):0;
        }
        else {
            // Find parent object
            for(size_t i = cursor_pos; i > 0; i--){
                if(nav->items[i-1].depth == item->depth)
                    insert_idx++;
                if(nav->items[i - 1].depth < item->depth){
                    parent = &nav->items[i - 1];
                    break;
                }
            }
            if(!parent){
                free((void*)clipboard_text.text);
                nav_set_messagef(nav, "Error: can't find parent");
                return CMD_ERROR;
            }
            if(after) insert_idx++;
        }
    }

    // Parse clipboard text with appropriate flags based on parent type
    DrJsonValue paste_value;
    if(parent->value.kind == DRJSON_OBJECT){
        // Pasting into object - check if we need braceless parsing
        const char* txt = clipboard_text.text;
        size_t len = clipboard_text.length;

        // Skip leading whitespace
        while(len > 0 && (*txt == ' ' || *txt == '\t' || *txt == '\n' || *txt == '\r')){
            txt++;
            len--;
        }

        // If doesn't start with '{', use braceless parsing
        unsigned parse_flags = (len > 0 && *txt != '{') ? DRJSON_PARSE_FLAG_BRACELESS_OBJECT : 0;
        parse_flags |= DRJSON_PARSE_FLAG_ERROR_ON_TRAILING;
        paste_value = drjson_parse_string(nav->jctx, txt, len, parse_flags);
        free((void*)clipboard_text.text);

        if(paste_value.kind != DRJSON_OBJECT){
            nav_set_messagef(nav, "Error: can only paste objects into objects");
            return CMD_ERROR;
        }

        // Insert all key-value pairs from pasted object
        size_t pair_count = drjson_len(nav->jctx, paste_value);
        for(size_t i = 0; i < pair_count; i++){
            DrJsonValue key = drjson_get_by_index(nav->jctx, drjson_object_keys(paste_value), i);
            DrJsonValue value = drjson_get_by_index(nav->jctx, drjson_object_values(paste_value), i);
            int err = drjson_object_insert_item_at_index(nav->jctx, parent->value, key.atom, value, insert_idx);
            if(err){
                nav_set_messagef(nav, "Error: failed to insert key");
            }
            else {
                insert_idx++;
            }
        }
    }
    else if(parent->value.kind == DRJSON_ARRAY){
        // Pasting into array - normal parsing
        int err = parse_as_value(nav->jctx, clipboard_text.text, clipboard_text.length, &paste_value);
        free((void*)clipboard_text.text);

        if(err || paste_value.kind == DRJSON_ERROR){
            nav_set_messagef(nav, "Error: Clipboard does not contain valid JSON");
            return CMD_ERROR;
        }

        err = drjson_array_insert_item(nav->jctx, parent->value, insert_idx, paste_value);
        if(err){
            nav_set_messagef(nav, "Error: couldn't insert into array at index %zu", insert_idx);
            return CMD_ERROR;
        }
    }
    else {
        free((void*)clipboard_text.text);
        nav_set_messagef(nav, "Error: Invalid parent type");
        return CMD_ERROR;
    }
    nav->needs_rebuild = 1;
    nav_rebuild(nav);
    return CMD_OK;
}

static
int
cmd_paste(JsonNav* nav, CmdArgs* args){
    (void)args;

    if(nav->item_count == 0){
        nav_set_messagef(nav, "Error: Nothing to paste into");
        return CMD_ERROR;
    }
    return do_paste(nav, nav->cursor_pos, 0);
}

static
int
cmd_query(JsonNav* nav, CmdArgs* args){
    // Get required path argument
    StringView path_sv = {0};
    int err = cmd_get_arg_string(args, SV("path"), &path_sv);
    if(err == CMD_ARG_ERROR_MISSING || err == CMD_ARG_ERROR_MISSING_BUT_OPTIONAL){
        nav_set_messagef(nav, "Error: No query path provided");
        return CMD_ERROR;
    }
    if(err != CMD_ARG_ERROR_NONE){
        nav_set_messagef(nav, "Error parsing query path");
        return CMD_ERROR;
    }

    if(nav->item_count == 0){
        nav_set_messagef(nav, "Error: No JSON loaded");
        return CMD_ERROR;
    }

    // Parse the path into components
    DrJsonPath path;
    int parse_err = drjson_path_parse(nav->jctx, path_sv.text, path_sv.length, &path);
    if(parse_err){
        nav_set_messagef(nav, "Error: Invalid path syntax: %.*s", (int)path_sv.length, path_sv.text);
        return CMD_ERROR;
    }

    // Get current value as starting point
    DrJsonValue current = nav->items[nav->cursor_pos].value;

    // Navigate through the path using the JSON API (not the visible items)
    // This allows us to navigate through collapsed containers
    for(size_t seg_idx = 0; seg_idx < path.count; seg_idx++){
        DrJsonPathSegment* seg = &path.segments[seg_idx];

        if(seg->kind == DRJSON_PATH_KEY){
            // Navigate by key (object member)
            if(current.kind != DRJSON_OBJECT){
                nav_set_messagef(nav, "Error: Cannot index non-object with key at segment %zu", seg_idx);
                return CMD_ERROR;
            }

            DrJsonValue next = drjson_object_get_item_atom(nav->jctx, current, seg->key);
            if(next.kind == DRJSON_ERROR){
                const char* key_str;
                size_t key_len;
                int err = drjson_get_atom_str_and_length(nav->jctx, seg->key, &key_str, &key_len);
                if(!err)
                    nav_set_messagef(nav, "Error: Key '%.*s' not found", (int)key_len, key_str);
                else
                    nav_set_messagef(nav, "Error: Key not found");
                return CMD_ERROR;
            }

            // Expand the current container if needed
            if(nav_is_container(current)){
                bs_add(&nav->expanded, nav_get_container_id(current), &nav->allocator);
            }

            current = next;
        }
        else if(seg->kind == DRJSON_PATH_INDEX){
            // Navigate by index (array element)
            if(current.kind != DRJSON_ARRAY){
                nav_set_messagef(nav, "Error: Cannot index non-array with [%lld] at segment %zu", (long long)seg->index, seg_idx);
                return CMD_ERROR;
            }

            DrJsonValue next = drjson_get_by_index(nav->jctx, current, seg->index);
            if(next.kind == DRJSON_ERROR){
                nav_set_messagef(nav, "Error: Index [%lld] out of bounds", (long long)seg->index);
                return CMD_ERROR;
            }

            // Expand the current container if needed
            if(nav_is_container(current)){
                bs_add(&nav->expanded, nav_get_container_id(current), &nav->allocator);
            }

            current = next;
        }
    }

    // Rebuild the items array to show newly expanded containers
    nav->needs_rebuild = 1;
    nav_rebuild(nav);

    // Now find the target value in the rebuilt items array
    for(size_t i = 0; i < nav->item_count; i++){
        if(drjson_eq(nav->items[i].value, current)){
            nav->cursor_pos = i;
            nav_set_messagef(nav, "Navigated to: %.*s", (int)path_sv.length, path_sv.text);
            return CMD_OK;
        }
    }

    // This shouldn't happen, but handle it just in case
    nav_set_messagef(nav, "Error: Found value but couldn't locate it in view");
    return CMD_ERROR;
}

static
void
nav_focus_stack_push(JsonNav* nav, DrJsonValue val){
    if(nav->focus_stack_count >= nav->focus_stack_capacity){
        size_t old_size = nav->focus_stack_capacity * sizeof *nav->focus_stack;
        size_t new_cap = nav->focus_stack_capacity ? nav->focus_stack_capacity * 2 : 8;
        size_t new_size = new_cap * sizeof *nav->focus_stack;
        DrJsonValue* new_stack = nav->allocator.realloc(nav->allocator.user_pointer, nav->focus_stack, old_size, new_size);
        if(!new_stack) return; // allocation failure
        nav->focus_stack = new_stack;
        nav->focus_stack_capacity = new_cap;
    }
    nav->focus_stack[nav->focus_stack_count++] = val;
}

static
DrJsonValue
nav_focus_stack_pop(JsonNav* nav){
    if(nav->focus_stack_count == 0){
        return drjson_make_error(DRJSON_ERROR_INDEX_ERROR, "focus stack empty");
    }
    return nav->focus_stack[--nav->focus_stack_count];
}

static
int
cmd_focus(JsonNav* nav, CmdArgs* args){
    (void)args;

    if(nav->item_count == 0){
        nav_set_messagef(nav, "Error: Nothing to focus on");
        return CMD_ERROR;
    }

    NavItem* item = &nav->items[nav->cursor_pos];
    if(!nav_is_container(item->value)){
        nav_set_messagef(nav, "Error: Can only focus on arrays or objects");
        return CMD_ERROR;
    }
    if(memcmp(&item->value, &nav->root, sizeof nav->root) == 0){
        nav_set_messagef(nav, "Error: Already the root");
        return CMD_ERROR;
    }

    nav_focus_stack_push(nav, nav->root);
    nav->root = item->value;
    nav_reinit(nav);

    nav_set_messagef(nav, "Focused on new root. Use :unfocus or 'F' to go back.");
    if(0) LOG("nav->focus_stack_count: %zu\n", nav->focus_stack_count);
    return CMD_OK;
}

static
int
cmd_unfocus(JsonNav* nav, CmdArgs* args){
    (void)args;
    if(0) LOG("nav->focus_stack_count: %zu\n", nav->focus_stack_count);

    if(nav->focus_stack_count == 0){
        nav_set_messagef(nav, "Error: Already at the top-level view");
        return CMD_ERROR;
    }

    DrJsonValue prev_root = nav_focus_stack_pop(nav);
    if(prev_root.kind == DRJSON_ERROR){
        // This shouldn't happen with the count check, but just in case.
        nav_set_messagef(nav, "Error: Invalid focus stack state");
        return CMD_ERROR;
    }

    nav->root = prev_root;
    nav_reinit(nav);

    nav_set_messagef(nav, "Unfocused, returned to previous view.");
    return CMD_OK;
}

static
int
cmd_wq(JsonNav* nav, CmdArgs* args){
    // First, try to write the file
    int write_result = cmd_write(nav, args);
    if(write_result != CMD_OK){
        // If write failed, don't quit, just report the error
        return write_result;
    }
    // If write succeeded, then quit
    return cmd_quit(nav, args);
}

static
int
cmd_reload(JsonNav* nav, CmdArgs* args){
    (void)args;

    if(nav->filename[0] == '\0'){
        nav_set_messagef(nav, "Error: No file is currently open to reload.");
        return CMD_ERROR;
    }
    // Remember the braceless flag so reload preserves it
    _Bool was_braceless = nav->was_opened_with_braceless;
    int err = nav_load_file(nav, nav->filename, was_braceless);
    if(err != CMD_OK)
        return CMD_ERROR;
    return CMD_OK;
}

//------------------------------------------------------------
// Sorting Helpers
//------------------------------------------------------------

static int compare_values(DrJsonValue a, DrJsonValue b, DrJsonContext* jctx);

// Struct to pass context to qsort
typedef struct {
    DrJsonContext* jctx;
    int direction; // 1 for asc, -1 for desc
    const char* sort_query;
    size_t sort_query_len;
} SortContext;

static SortContext g_sort_context;

// qsort compatible comparison function
static
int
qsort_compare_values_wrapper(const void* a, const void* b){
    DrJsonValue val_a = *(const DrJsonValue*)a;
    DrJsonValue val_b = *(const DrJsonValue*)b;
    return compare_values(val_a, val_b, g_sort_context.jctx) * g_sort_context.direction;
}

// Struct for sorting key-value pairs
typedef struct KeyValuePair KeyValuePair;
struct KeyValuePair {
    DrJsonAtom key;
    DrJsonValue value;
};

// qsort compatible comparison function for sorting pairs by value
static
int
qsort_compare_pairs_by_value(const void* a, const void* b){
    const KeyValuePair* pair_a = (const KeyValuePair*)a;
    const KeyValuePair* pair_b = (const KeyValuePair*)b;
    return compare_values(pair_a->value, pair_b->value, g_sort_context.jctx) * g_sort_context.direction;
}

// qsort wrapper for sorting an array of objects by a query on each object
static
int
qsort_compare_array_by_query(const void* a, const void* b){
    DrJsonValue obj_a = *(const DrJsonValue*)a;
    DrJsonValue obj_b = *(const DrJsonValue*)b;

    DrJsonValue val_a = drjson_query(g_sort_context.jctx, obj_a, g_sort_context.sort_query, g_sort_context.sort_query_len);
    DrJsonValue val_b = drjson_query(g_sort_context.jctx, obj_b, g_sort_context.sort_query, g_sort_context.sort_query_len);

    if(val_a.kind == DRJSON_ERROR) val_a = drjson_make_null();
    if(val_b.kind == DRJSON_ERROR) val_b = drjson_make_null();

    return compare_values(val_a, val_b, g_sort_context.jctx) * g_sort_context.direction;
}

// qsort wrapper for sorting an object's KeyValuePairs by a query on the value
static
int
qsort_compare_pairs_by_query(const void* a, const void* b){
    const KeyValuePair* pair_a = (const KeyValuePair*)a;
    const KeyValuePair* pair_b = (const KeyValuePair*)b;

    DrJsonValue val_a = drjson_query(g_sort_context.jctx, pair_a->value, g_sort_context.sort_query, g_sort_context.sort_query_len);
    DrJsonValue val_b = drjson_query(g_sort_context.jctx, pair_b->value, g_sort_context.sort_query, g_sort_context.sort_query_len);

    if(val_a.kind == DRJSON_ERROR) val_a = drjson_make_null();
    if(val_b.kind == DRJSON_ERROR) val_b = drjson_make_null();

    return compare_values(val_a, val_b, g_sort_context.jctx) * g_sort_context.direction;
}


static
int
cmd_sort(JsonNav* nav, CmdArgs* args){
    if(nav->item_count == 0){
        nav_set_messagef(nav, "Error: Nothing to sort.");
        return CMD_ERROR;
    }

    // --- Argument Parsing ---
    int direction = 1;
    _Bool sort_by_values = 0;
    const char* query_str = NULL;
    size_t query_len = 0;

    // Get optional query
    StringView query_sv = {0};
    int err = cmd_get_arg_string(args, SV("query"), &query_sv);
    if(err == CMD_ARG_ERROR_NONE){
        query_str = query_sv.text;
        query_len = query_sv.length;
    }

    // Check keys|values alternatives
    _Bool flag_keys = 0;
    err = cmd_get_arg_bool(args, SV("keys"), &flag_keys);
    if(err == CMD_ARG_ERROR_NONE && flag_keys){
        sort_by_values = 0;
    }

    _Bool flag_values = 0;
    err = cmd_get_arg_bool(args, SV("values"), &flag_values);
    if(err == CMD_ARG_ERROR_NONE && flag_values){
        sort_by_values = 1;
    }

    // Check asc|desc alternatives
    _Bool flag_asc = 0;
    err = cmd_get_arg_bool(args, SV("asc"), &flag_asc);
    if(err == CMD_ARG_ERROR_NONE && flag_asc){
        direction = 1;
    }

    _Bool flag_desc = 0;
    err = cmd_get_arg_bool(args, SV("desc"), &flag_desc);
    if(err == CMD_ARG_ERROR_NONE && flag_desc){
        direction = -1;
    }

    NavItem* item = &nav->items[nav->cursor_pos];
    DrJsonValue val = item->value;

    if(val.kind == DRJSON_ARRAY){
        int64_t len = drjson_len(nav->jctx, val);
        if(len <= 1){
            nav_set_messagef(nav, "Array has %lld elements, no sorting needed.", (long long)len);
            return CMD_OK;
        }

        DrJsonArray* arr = &nav->jctx->arrays.data[val.array_idx];

        if(query_str){
            // --- Sort array by query ---
            for(int64_t i = 0; i < len; i++){
                DrJsonValue elem = arr->array_items[i];
                DrJsonValue sort_val = drjson_query(nav->jctx, elem, query_str, query_len);
                if(sort_val.kind == DRJSON_ERROR){
                    nav_set_messagef(nav, "Error: Query '%.*s' failed on element at index %lld: %s", (int)query_len, query_str, (long long)i, sort_val.err_mess);
                    return CMD_ERROR;
                }
            }

            g_sort_context.jctx = nav->jctx;
            g_sort_context.sort_query = query_str;
            g_sort_context.sort_query_len = query_len;
            g_sort_context.direction = direction;
            qsort(arr->array_items, len, sizeof *arr->array_items, qsort_compare_array_by_query);
            nav_set_messagef(nav, "Array sorted by query '%.*s'.", (int)query_len, query_str);
        }
        else {
            // --- Sort simple array by value ---
            g_sort_context.jctx = nav->jctx;
            g_sort_context.direction = direction;
            qsort(arr->array_items, len, sizeof *arr->array_items, qsort_compare_values_wrapper);
            nav_set_messagef(nav, "Array sorted successfully.");
        }
    }
    else if(val.kind == DRJSON_OBJECT){
        int64_t len = drjson_len(nav->jctx, val);
        if(len <= 1){
            nav_set_messagef(nav, "Object has %lld members, no sorting needed.", (long long)len);
            return CMD_OK;
        }

        DrJsonValue new_obj = drjson_make_object(nav->jctx);

        if(sort_by_values){
            KeyValuePair* pairs = nav->allocator.alloc(nav->allocator.user_pointer, len * sizeof *pairs);
            if(!pairs){
                nav_set_messagef(nav, "Error: Failed to allocate memory for sorting.");
                return CMD_ERROR;
            }

            DrJsonValue keys_view = drjson_object_keys(val);
            for(int64_t i = 0; i < len; i++){
                pairs[i].key = drjson_get_by_index(nav->jctx, keys_view, i).atom;
                pairs[i].value = drjson_object_get_item_atom(nav->jctx, val, pairs[i].key);
            }

            if(query_str){
                // --- Sort object by value with query ---
                for(int64_t i = 0; i < len; i++){
                    DrJsonValue sort_val = drjson_query(nav->jctx, pairs[i].value, query_str, query_len);
                    if(sort_val.kind == DRJSON_ERROR){
                        const char* key_str; size_t key_len;
                        int err = drjson_get_atom_str_and_length(nav->jctx, pairs[i].key, &key_str, &key_len);
                        if(!err)
                            nav_set_messagef(nav, "Error: Query '%.*s' failed on value for key '%.*s': %s", (int)query_len, query_str, (int)key_len, key_str, sort_val.err_mess);
                        else
                            nav_set_messagef(nav, "Error: Query '%.*s' failed: %s", (int)query_len, query_str, sort_val.err_mess);
                        free(pairs);
                        return CMD_ERROR;
                    }
                }
                g_sort_context.jctx = nav->jctx;
                g_sort_context.sort_query = query_str;
                g_sort_context.sort_query_len = query_len;
                g_sort_context.direction = direction;
                qsort(pairs, len, sizeof *pairs, qsort_compare_pairs_by_query);
                nav_set_messagef(nav, "Object sorted by query '%.*s'.", (int)query_len, query_str);
            }
            else {
                // --- Sort object by value ---
                g_sort_context.jctx = nav->jctx;
                g_sort_context.direction = direction;
                qsort(pairs, len, sizeof *pairs, qsort_compare_pairs_by_value);
                nav_set_messagef(nav, "Object sorted by value.");
            }

            for(int64_t i = 0; i < len; i++){
                drjson_object_set_item_atom(nav->jctx, new_obj, pairs[i].key, pairs[i].value);
            }
            nav->allocator.free(nav->allocator.user_pointer, pairs, len * sizeof *pairs);
        }
        else {
            // --- Sort by keys ---
            if(query_str){
                nav_set_messagef(nav, "Error: Query cannot be used when sorting object by key.");
                return CMD_ERROR;
            }
            DrJsonValue keys_view = drjson_object_keys(val);
            DrJsonValue keys_copy = drjson_make_array(nav->jctx);
            for(int64_t i = 0; i < len; i++){
                drjson_array_push_item(nav->jctx, keys_copy, drjson_get_by_index(nav->jctx, keys_view, i));
            }

            DrJsonArray* keys_arr = &nav->jctx->arrays.data[keys_copy.array_idx];
            g_sort_context.jctx = nav->jctx;
            g_sort_context.direction = direction;
            qsort(keys_arr->array_items, len, sizeof *keys_arr->array_items, qsort_compare_values_wrapper);

            for(int64_t i = 0; i < len; i++){
                DrJsonValue key_val = keys_arr->array_items[i];
                DrJsonValue value = drjson_object_get_item_atom(nav->jctx, val, key_val.atom);
                drjson_object_set_item_atom(nav->jctx, new_obj, key_val.atom, value);
            }
            nav_set_messagef(nav, "Object sorted by key.");
        }

        // Replace the old object with the new one
        size_t parent_idx = nav_find_parent(nav, nav->cursor_pos);
        if(parent_idx == SIZE_MAX){ // It's the root
            nav->root = new_obj;
        }
        else {
            NavItem* parent_item = &nav->items[parent_idx];
            if(parent_item->value.kind == DRJSON_OBJECT){
                drjson_object_set_item_atom(nav->jctx, parent_item->value, item->key, new_obj);
            }
            else if(parent_item->value.kind == DRJSON_ARRAY){
                drjson_array_set_by_index(nav->jctx, parent_item->value, item->index, new_obj);
            }
        }
        item->value = new_obj;
    }
    else {
        nav_set_messagef(nav, "Error: Can only sort arrays or objects.");
        return CMD_ERROR;
    }

    nav->needs_rebuild = 1;
    nav_rebuild(nav);
    return CMD_OK;
}

// Defines the sort order for different JSON types.
static
int
get_type_rank(DrJsonValue v){
    switch(v.kind){
        case DRJSON_NULL: return 0;
        case DRJSON_BOOL: return 1;
        case DRJSON_NUMBER:
        case DRJSON_INTEGER:
        case DRJSON_UINTEGER: return 2;
        case DRJSON_STRING: return 3;
        case DRJSON_ARRAY: return 4;
        case DRJSON_OBJECT: return 5;
        default: return 6; // Errors and others
    }
}

// Comparison function for two DrJsonValues.
static
double
drj_to_double_for_sort(DrJsonValue val){
    switch(val.kind){
        case DRJSON_NUMBER: return val.number;
        case DRJSON_INTEGER: return (double)val.integer;
        case DRJSON_UINTEGER: return (double)val.uinteger;
        default: return 0.0;
    }
}

static
int
compare_values(DrJsonValue a, DrJsonValue b, DrJsonContext* jctx){
    int rank_a = get_type_rank(a);
    int rank_b = get_type_rank(b);
    if(rank_a != rank_b) return rank_a - rank_b;

    // Types are the same, compare by value
    switch(a.kind){
        case DRJSON_BOOL:
            return (int)a.boolean - (int)b.boolean;

        case DRJSON_NUMBER:
        case DRJSON_INTEGER:
        case DRJSON_UINTEGER: {
            double val_a = drj_to_double_for_sort(a);
            double val_b = drj_to_double_for_sort(b);
            if(val_a < val_b) return -1;
            if(val_a > val_b) return 1;
            return 0;
        }

        case DRJSON_STRING: {
            StringView sv1, sv2;
            int err1 = drjson_get_str_and_len(jctx, a, &sv1.text, &sv1.length);
            int err2 = drjson_get_str_and_len(jctx, b, &sv2.text, &sv2.length);
            if(err1 || err2 || !sv1.text || !sv2.text) return 0; // Should not happen
            return StringView_cmp(&sv1, &sv2);
        }

        case DRJSON_ARRAY:
        case DRJSON_OBJECT: {
            int64_t len_a = drjson_len(jctx, a);
            int64_t len_b = drjson_len(jctx, b);
            if(len_a < len_b) return -1;
            if(len_a > len_b) return 1;
            return 0;
        }

        default:
            return 0; // NULLs, Errors, etc.
    }
}

enum Operator {
    OP_INVALID, OP_EQ, OP_NEQ, OP_GT, OP_GTE, OP_LT, OP_LTE
};
typedef enum Operator Operator;

static
const char* _Nullable
parse_operator(const char* p, const char* end, Operator* op){
    while (p < end && *p == ' ') p++; // skip whitespace
    if(p >= end) return NULL;

    if(p + 1 < end){
        if(p[0] == '=' && p[1] == '='){ *op = OP_EQ; return p + 2; }
        if(p[0] == '!' && p[1] == '='){ *op = OP_NEQ; return p + 2; }
        if(p[0] == '>' && p[1] == '='){ *op = OP_GTE; return p + 2; }
        if(p[0] == '<' && p[1] == '='){ *op = OP_LTE; return p + 2; }
    }
    if(p[0] == '>'){ *op = OP_GT; return p + 1; }
    if(p[0] == '<'){ *op = OP_LT; return p + 1; }

    *op = OP_INVALID;
    return NULL;
}

static
const char* _Nullable
parse_literal(DrJsonContext* ctx, const char* p, const char* end, DrJsonValue* val){
    while (p < end && *p == ' ') p++; // skip whitespace
    if(p >= end) return NULL;

    DrJsonParseContext pctx = {
        .ctx = (DrJsonContext*)ctx,
        .begin = p,
        .cursor = p,
        .end = end,
        .depth = 0,
    };
    *val = drjson_parse(&pctx, DRJSON_PARSE_FLAG_NO_COPY_STRINGS|DRJSON_PARSE_FLAG_ERROR_ON_TRAILING);
    if(val->kind == DRJSON_ERROR){
        return NULL;
    }
    return pctx.cursor;
}

// Check if a DrJsonValue is considered "truthy" for filtering
static
_Bool
is_truthy(DrJsonValue val, DrJsonContext* jctx){
    switch(val.kind){
        case DRJSON_NULL:
        case DRJSON_ERROR:
            return 0;
        case DRJSON_BOOL:
            return val.boolean;
        case DRJSON_NUMBER:
            return val.number != 0.0;
        case DRJSON_INTEGER:
            return val.integer != 0;
        case DRJSON_UINTEGER:
            return val.uinteger != 0;
        case DRJSON_STRING:
        case DRJSON_ARRAY:
        case DRJSON_OBJECT:
            return drjson_len(jctx, val) > 0;
        default:
            return 0;
    }
}

typedef struct {
    DrJsonPath path;
    Operator op;
    _Bool rhs_is_path;
    union {
        DrJsonPath rhs_path;
        DrJsonValue rhs_literal;
    };
} TuiParsedExpression;

// Parses the expression string into a structured representation.
static
DRJSON_WARN_UNUSED
int
tui_parse_expression(JsonNav* nav, const char* expression, size_t expression_length, TuiParsedExpression* out_expr){
    const char* p = expression;
    const char* end = expression + expression_length;

    // 1. Greedily parse the path from the beginning of the string.
    const char* remainder = NULL;
    int err = drjson_path_parse_greedy(nav->jctx, p, end - p, &out_expr->path, &remainder);
    if(err){
        return 1; // The start of the expression is not a valid path.
    }

    // 2. Check if there's anything left to parse after the path.
    const char* op_start = remainder;
    while (op_start < end && *op_start == ' ') op_start++; // skip whitespace

    if(op_start == end){
        // Nothing left after the path, so it's a truthy check.
        out_expr->op = OP_INVALID;
        return 0; // Success
    }

    // 3. Something exists after the path, so parse it as an operator and a RHS.
    const char* rhs_start = parse_operator(op_start, end, &out_expr->op);
    if(out_expr->op == OP_INVALID || !rhs_start){
        return 1; // Invalid operator
    }

    while (rhs_start < end && *rhs_start == ' ') rhs_start++; // skip whitespace

    if(rhs_start < end && (*rhs_start == '.' || *rhs_start == '[' || *rhs_start == '$')){
        // RHS is a path
        out_expr->rhs_is_path = 1;
        const char* rhs_remainder = NULL;
        err = drjson_path_parse_greedy(nav->jctx, rhs_start, end - rhs_start, &out_expr->rhs_path, &rhs_remainder);
        if(err) return 1; // RHS path failed to parse
        // Ensure the entire rest of the string was consumed by the path parser
        while(rhs_remainder < end && *rhs_remainder == ' ') rhs_remainder++;
        if(rhs_remainder != end) return 1;
    }
    else {
        // RHS is a literal
        out_expr->rhs_is_path = 0;
        const char* literal_end = parse_literal(nav->jctx, rhs_start, end, &out_expr->rhs_literal);
        if(!literal_end){
            return 1; // RHS literal failed to parse
        }
    }

    return 0; // Success
}

// Evaluates a pre-parsed expression against a given value.
static
DrJsonValue
tui_eval_expression(JsonNav* nav, DrJsonValue v, const TuiParsedExpression* expr){
    // 1. Evaluate the path to get the LHS value
    DrJsonValue lhs = drjson_evaluate_path(nav->jctx, v, &expr->path);
    if(lhs.kind == DRJSON_ERROR) return lhs;

    // 2. If in truthy mode, check truthiness of the path result
    if(expr->op == OP_INVALID)
        return drjson_make_bool(is_truthy(lhs, nav->jctx));

    // 3. Get the RHS value
    DrJsonValue rhs;
    if(expr->rhs_is_path){
        rhs = drjson_evaluate_path(nav->jctx, v, &expr->rhs_path);
        if(rhs.kind == DRJSON_ERROR) return rhs;
    }
    else {
        rhs = expr->rhs_literal;
    }

    // 4. Perform the comparison
    int cmp = compare_values(lhs, rhs, nav->jctx);
    _Bool result = 0;
    switch(expr->op){
        case OP_EQ:  result = (cmp == 0); break;
        case OP_NEQ: result = (cmp != 0); break;
        case OP_GT:  result = (cmp > 0);  break;
        case OP_GTE: result = (cmp >= 0); break;
        case OP_LT:  result = (cmp < 0);  break;
        case OP_LTE: result = (cmp <= 0); break;
        case OP_INVALID: return drjson_make_error(DRJSON_ERROR_INVALID_VALUE, "Invalid operator");
    }

    return drjson_make_bool(result);
}

static
int
cmd_filter(JsonNav* nav, CmdArgs* args){
    // Get required query argument
    StringView query_sv = {0};
    int err = cmd_get_arg_string(args, SV("query"), &query_sv);
    if(err == CMD_ARG_ERROR_MISSING || err == CMD_ARG_ERROR_MISSING_BUT_OPTIONAL){
        nav_set_messagef(nav, "Error: :filter requires a query.");
        return CMD_ERROR;
    }
    if(err != CMD_ARG_ERROR_NONE){
        nav_set_messagef(nav, "Error parsing query");
        return CMD_ERROR;
    }

    if(nav->item_count == 0){
        nav_set_messagef(nav, "Error: Nothing to filter.");
        return CMD_ERROR;
    }

    // Parse the expression once before the loop
    TuiParsedExpression expr;
    if(tui_parse_expression(nav, query_sv.text, query_sv.length, &expr) != 0){
        nav_set_messagef(nav, "Error: Invalid filter expression.");
        return CMD_ERROR;
    }

    NavItem* item = &nav->items[nav->cursor_pos];
    DrJsonValue val = item->value;

    if(val.kind != DRJSON_ARRAY && val.kind != DRJSON_OBJECT){
        nav_set_messagef(nav, "Error: Can only filter arrays or objects.");
        return CMD_ERROR;
    }

    int64_t original_len = drjson_len(nav->jctx, val);
    int64_t filtered_count = 0;

    if(val.kind == DRJSON_ARRAY){
        DrJsonValue new_array = drjson_make_array(nav->jctx);
        for(int64_t i = 0; i < original_len; i++){
            DrJsonValue elem = drjson_get_by_index(nav->jctx, val, i);
            DrJsonValue query_result = tui_eval_expression(nav, elem, &expr);

            if(query_result.kind == DRJSON_BOOL && query_result.boolean){
                drjson_array_push_item(nav->jctx, new_array, elem);
                filtered_count++;
            }
        }
        nav_focus_stack_push(nav, nav->root);
        nav->root = new_array;
    }
    else { // DRJSON_OBJECT
        DrJsonValue new_obj = drjson_make_object(nav->jctx);
        DrJsonValue keys = drjson_object_keys(val);
        for(int64_t i = 0; i < original_len; i++){
            DrJsonValue key_val = drjson_get_by_index(nav->jctx, keys, i);
            DrJsonValue value = drjson_object_get_item_atom(nav->jctx, val, key_val.atom);

            DrJsonValue query_result = tui_eval_expression(nav, value, &expr);

            if(query_result.kind == DRJSON_BOOL && query_result.boolean){
                drjson_object_set_item_atom(nav->jctx, new_obj, key_val.atom, value);
                filtered_count++;
            }
        }
        nav_focus_stack_push(nav, nav->root);
        nav->root = new_obj;
    }

    nav_reinit(nav);
    nav_set_messagef(nav, "Filtered to %lld items.", (long long)filtered_count);
    return CMD_OK;
}

// Helper function to move the current item to a target index within its parent container
// Returns CMD_OK on success, CMD_ERROR on failure
static
int
nav_move_item_to_index(JsonNav* nav, int64_t target_idx){
    if(nav->item_count == 0){
        nav_set_messagef(nav, "Error: Nothing to move.");
        return CMD_ERROR;
    }

    NavItem* item = &nav->items[nav->cursor_pos];

    // Cannot move flat view items (synthetic row items)
    if(item->is_flat_view){
        nav_set_messagef(nav, "Error: Cannot move flat view items.");
        return CMD_ERROR;
    }

    // Find parent container
    size_t parent_idx = nav_find_parent(nav, nav->cursor_pos);
    if(parent_idx == SIZE_MAX){
        nav_set_messagef(nav, "Error: Cannot move root value.");
        return CMD_ERROR;
    }

    NavItem* parent = &nav->items[parent_idx];

    int64_t parent_len = drjson_len(nav->jctx, parent->value);

    // Handle negative index (from end)
    if(target_idx < 0){
        target_idx = parent_len + target_idx;
    }

    if(target_idx < 0 || target_idx >= parent_len){
        nav_set_messagef(nav, "Error: Index %lld out of range (0-%lld).",
                        (long long)target_idx, (long long)(parent_len - 1));
        return CMD_ERROR;
    }

    size_t to_idx = (size_t)target_idx;
    size_t from_idx = (size_t)item->index;

    int err;
    if(parent->value.kind == DRJSON_ARRAY){
        err = drjson_array_move_item(nav->jctx, parent->value, from_idx, to_idx);
    }
    else if(parent->value.kind == DRJSON_OBJECT){
        err = drjson_object_move_item(nav->jctx, parent->value, from_idx, to_idx);
    }
    else {
        nav_set_messagef(nav, "Error: Parent is not a container.");
        return CMD_ERROR;
    }

    if(err){
        nav_set_messagef(nav, "Error: Could not move item.");
        return CMD_ERROR;
    }

    nav->needs_rebuild = 1;
    nav_rebuild(nav);

    // Try to keep cursor on the moved item by finding it at its new position
    for(size_t i = 0; i < nav->item_count; i++){
        if(nav->items[i].index == (int64_t)to_idx &&
           nav_find_parent(nav, i) == parent_idx){
            nav->cursor_pos = i;
            break;
        }
    }
    nav_ensure_cursor_visible(nav, globals.screenh);

    return CMD_OK;
}

static
int
nav_move_item_relative(JsonNav* nav, int64_t delta){
    if(nav->item_count == 0) return CMD_ERROR;
    NavItem* item = &nav->items[nav->cursor_pos];
    int64_t from_idx = item->index;
    if(from_idx < 0){
        nav_set_messagef(nav, "Cannot move root value");
        return CMD_ERROR;
    }
    int64_t to_idx = from_idx + delta;

    // For relative moves, we need to check bounds ourselves
    // because nav_move_item_to_index interprets negative indices as "from end"
    // which doesn't make sense for relative movement
    size_t parent_idx = nav_find_parent(nav, nav->cursor_pos);
    if(parent_idx == SIZE_MAX){
        nav_set_messagef(nav, "Cannot move root value");
        return CMD_ERROR;
    }
    NavItem* parent = &nav->items[parent_idx];
    int64_t parent_len = drjson_len(nav->jctx, parent->value);

    if(to_idx < 0 || to_idx >= parent_len)
        return CMD_ERROR;

    return nav_move_item_to_index(nav, to_idx);
}

static
int
cmd_move(JsonNav* nav, CmdArgs* args){
    // Get required index argument
    StringView index_sv = {0};
    int err = cmd_get_arg_string(args, SV("index"), &index_sv);
    if(err == CMD_ARG_ERROR_MISSING || err == CMD_ARG_ERROR_MISSING_BUT_OPTIONAL){
        nav_set_messagef(nav, "Error: :move requires an index.");
        return CMD_ERROR;
    }
    if(err != CMD_ARG_ERROR_NONE){
        nav_set_messagef(nav, "Error parsing index");
        return CMD_ERROR;
    }

    // Parse the target index
    Int64Result parse_result = parse_int64(index_sv.text, index_sv.length);
    if(parse_result.errored){
        nav_set_messagef(nav, "Error: Invalid index.");
        return CMD_ERROR;
    }

    int result = nav_move_item_to_index(nav, parse_result.result);
    if(result == CMD_OK){
        nav_set_messagef(nav, "Moved to index %lld.", (long long)parse_result.result);
    }
    return result;
}

static
int
cmd_path(JsonNav* nav, CmdArgs* args){
    (void)args;

    if(nav->item_count == 0){
        nav_set_messagef(nav, "Error: Nothing selected");
        return CMD_ERROR;
    }

    char path_buf[1024];
    size_t path_len = nav_build_json_path(nav, path_buf, sizeof path_buf);

    if(path_len == 0){
        nav_set_messagef(nav, "Error: Could not generate path");
        return CMD_ERROR;
    }

#ifdef _WIN32
    if(copy_to_clipboard(path_buf, path_len) != 0){
        nav_set_messagef(nav, "Error: Could not copy path to clipboard");
        return CMD_ERROR;
    }
#elif defined(__APPLE__)
    if(macos_copy_to_clipboard(path_buf, path_len) != 0){
        nav_set_messagef(nav, "Error: Could not copy path to clipboard");
        return CMD_ERROR;
    }
#else
    // Linux clipboard logic
    FILE* pipe = NULL;
    if(getenv("TMUX"))
        pipe = popen("tmux load-buffer - 2>/dev/null", "w");
    if(!pipe)
        pipe = popen("xclip -selection clipboard 2>/dev/null", "w");
    if(!pipe)
        pipe = popen("xsel --clipboard --input 2>/dev/null", "w");

    if(!pipe){
        nav_set_messagef(nav, "Error: Could not open clipboard command (tried tmux, xclip, xsel)");
        return CMD_ERROR;
    }
    fwrite(path_buf, 1, path_len, pipe);
    if(pclose(pipe) != 0){
        nav_set_messagef(nav, "Error: Failed to copy to clipboard");
        return CMD_ERROR;
    }
#endif

    nav_set_messagef(nav, "Yanked path to clipboard");
    return CMD_OK;
}

static
void
nav_completion_add(JsonNav* nav, StringView name){
    if(nav->completion_count >= 64) return;
    // Store the full command name
    size_t copy_len = name.length < 255 ? name.length : 255;
    memcpy(nav->completion_matches[nav->completion_count], name.text, copy_len);
    nav->completion_matches[nav->completion_count][copy_len] = '\0';
    nav->completion_count++;
}

static void nav_completion_add_path_completion(JsonNav* nav, StringView prefix);

// Tab completion for command mode
// Opens completion menu with all matches
// Returns: 0 = no completion, 1 = menu shown
static
int
nav_complete_command(JsonNav* nav){
    LineEditor* le = &nav->command_buffer;

    // Save original command on first tab
    if(!nav->in_completion_menu){
        nav->saved_command_len = le->length < sizeof nav->saved_command ? le->length : sizeof nav->saved_command - 1;
        memcpy(nav->saved_command, le->data, nav->saved_command_len);
        nav->saved_command[nav->saved_command_len] = '\0';
    }

    // Use saved command for determining context
    StringView source = {nav->saved_command_len, nav->saved_command};

    // Find where we are in the command
    // If cursor is at start or only whitespace before cursor, complete command names
    StringView cmd_name = {.text = source.text};
    _Bool completing_command = 1;
    for(; cmd_name.length < source.length; cmd_name.length++){
        if(source.text[cmd_name.length] == ' '){
            completing_command = 0;
            break;
        }
    }

    nav->completion_count = 0;

    if(completing_command){
        // Complete command names using command table

        // Find matching commands
        for(size_t i = 0; i < sizeof commands / sizeof commands[0]; i++){
            StringView name = commands[i].name;
            if(SV_starts_with(name, cmd_name)){
                nav_completion_add(nav, name);
            }
        }
        if(nav->completion_count)
            nav->saved_prefix_len = 0;
    }
    else {
        // Complete commands according to the current possible completions based on cmd signature.
        const Command* cmd = cmd_by_name(cmd_name);
        if(!cmd) return 0;

        CmdParams params, completion_params;
        int err;
        err = cmd_param_parse_signature(cmd->signature, &params);
        if(err) return 0;
        StringView completion_token;
        err = cmd_get_completion_params(source, &params, &completion_params, &completion_token);
        if(err) return 0;

        // do flags first
        for(size_t i = 0; i < completion_params.count; i++){
            CmdParam* p = &completion_params.params[i];
            if(p->kind == CMD_PARAM_FLAG){
                if(p->names[0].length)
                    nav_completion_add(nav, p->names[0]);
                if(p->names[1].length)
                    nav_completion_add(nav, p->names[1]);
            }
        }

        // paths last
        for(size_t i = 0; i < completion_params.count; i++){
            CmdParam* p = &completion_params.params[i];
            if(p->kind == CMD_PARAM_PATH){
                nav_completion_add_path_completion(nav, completion_token);
                break;
            }
        }
        if(nav->completion_count){
            nav->saved_prefix_len = completion_token.text - source.text;
        }
    }
    if(!nav->completion_count) return 0;

    // Show completion menu
    nav->in_completion_menu = 1;
    nav->completion_selected = 0;
    nav->completion_scroll = 0;

    // Update buffer with first completion
    const char* first = nav->completion_matches[0];
    size_t first_len = strlen(first);
    size_t total_len = nav->saved_prefix_len + first_len;
    if(total_len < le->capacity){
        memcpy(le->data, nav->saved_command, nav->saved_prefix_len);
        memcpy(le->data+nav->saved_prefix_len, first, first_len);
        le->length = total_len;
        le->data[total_len] = 0;
        le->cursor_pos = total_len;
    }
    return 1;
}

static
void
nav_completion_add_path_completion(JsonNav* nav, StringView prefix){
    char path_prefix[1024];
    // Expand ~ in the path prefix
    int err = expand_tilde_to_buffer(prefix.text, prefix.length, path_prefix, sizeof path_prefix);
    if(err != 0) return;
    size_t path_len = strlen(path_prefix);
    if(0)LOG("path_prefix: '%s'\n", path_prefix);

    // Split into directory and filename parts
    char dir_path[1024] = ".";
    char file_prefix[256] = "";
    size_t file_prefix_len = 0;

    // Find last '/' to split directory and filename
    for(size_t i = path_len; i > 0; i--){
        _Bool is_slash;
        #ifdef _WIN32
        is_slash = path_prefix[i-1] == '\\' || path_prefix[i-1] == '/';
        #else
        is_slash = path_prefix[i-1] == '/';
        #endif
        if(is_slash){
            // Copy directory part
            size_t dir_len = i;
            if(dir_len >= sizeof dir_path) return;
            memcpy(dir_path, path_prefix, dir_len);
            dir_path[dir_len] = '\0';

            // Copy filename prefix
            file_prefix_len = path_len - i;
            if(file_prefix_len >= sizeof file_prefix) return;
            memcpy(file_prefix, path_prefix + i, file_prefix_len);
            file_prefix[file_prefix_len] = '\0';
            break;
        }
    }
    if(0)LOG("file_prefix_len: %zu\n", file_prefix_len);

    // If no '/' found, use current directory and full path as prefix
    if(dir_path[0] != '.' || dir_path[1] != '\0'){
        // We found a directory part
        if(0)LOG("found directory part\n");
    }
    else {
        // No directory separator found, use whole path as filename prefix
        file_prefix_len = path_len;
        if(file_prefix_len >= sizeof file_prefix) return;
        memcpy(file_prefix, path_prefix, file_prefix_len);
        file_prefix[file_prefix_len] = '\0';
        if(0)LOG("no directory separator\n");
    }

    // List matching files
    #ifndef _WIN32
    DIR* d = opendir(dir_path);
    if(!d) return;

    // Collect matching entries into completion menu
    for(struct dirent* entry = readdir(d); entry; entry = readdir(d)){
        // Skip . and ..
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){
            continue;
        }
        if(0)LOG("entry->d_name: '%s'\n", entry->d_name);

        // Check if it matches the prefix
        size_t entry_len = strlen(entry->d_name);
        if(entry_len >= file_prefix_len && memcmp(entry->d_name, file_prefix, file_prefix_len) == 0){

            // Build the full completed path for this match
            char completed[256];
            size_t completed_len = 0;
            if(prefix.length >= sizeof completed) return;
            memcpy(completed, prefix.text, prefix.length);
            completed_len = prefix.length;
            if(entry_len < file_prefix_len){
                if(0)LOG("entry_len < file_prefix_len: %zu < %zu\n", entry_len, file_prefix_len);
                continue;
            }
            size_t entry_diff = entry_len - file_prefix_len;
            if(entry_diff + completed_len >= sizeof completed){
                if(0)LOG("entry_diff + completed_len: %zu + %zu = %zu\n", entry_diff, completed_len, entry_diff+completed_len);
                continue;
            }
            memcpy(completed+completed_len, entry->d_name+file_prefix_len, entry_diff);
            completed_len += entry_diff;
            if(0)LOG("completed: '%.*s'\n", (int)completed_len, completed);
            StringView comp = {completed_len, completed};
            nav_completion_add(nav, comp);
        }
    }
    closedir(d);
    #else
    // Windows file completion using FindFirstFile/FindNextFile
    // TODO
    #endif
}

// Accept the currently selected completion
static
void
nav_accept_completion(JsonNav* nav){
    if(!nav->in_completion_menu || nav->completion_count == 0)
        return;

    LineEditor* le = &nav->command_buffer;
    const char* completion = nav->completion_matches[nav->completion_selected];
    size_t completion_len = strlen(completion);
    size_t total_len = completion_len + nav->saved_prefix_len;

    if(total_len < le->capacity){
        memcpy(le->data, nav->saved_command, nav->saved_prefix_len);
        memcpy(le->data+nav->saved_prefix_len, completion, completion_len);
        le->data[total_len] = '\0';
        le->length = total_len;
        le->cursor_pos = total_len;
    }

    // Exit completion menu
    nav->in_completion_menu = 0;
}

// Exit the completion menu without changing the buffer
static
void
nav_exit_completion(JsonNav* nav){
    nav->in_completion_menu = 0;
}

// Cancel the completion menu and restore original text
static
void
nav_cancel_completion(JsonNav* nav){
    if(!nav->in_completion_menu)
        return;

    LineEditor* le = &nav->command_buffer;
    memcpy(le->data, nav->saved_command, nav->saved_command_len);
    le->data[nav->saved_command_len] = '\0';
    le->length = nav->saved_command_len;
    le->cursor_pos = nav->saved_command_len;

    nav->in_completion_menu = 0;
}

// Move selection in completion menu
static
void
nav_completion_move(JsonNav* nav, int delta){
    if(!nav->in_completion_menu || nav->completion_count == 0)
        return;

    nav->completion_selected += delta;

    // Wrap around
    if(nav->completion_selected < 0){
        nav->completion_selected = nav->completion_count - 1;
    }
    else if(nav->completion_selected >= nav->completion_count){
        nav->completion_selected = 0;
    }

    // Update the line editor buffer with the selected completion
    const char* selected = nav->completion_matches[nav->completion_selected];
    size_t selected_len = strlen(selected);

    size_t total_len = selected_len + nav->saved_prefix_len;
    LineEditor* le = &nav->command_buffer;
    if(total_len < le->capacity){
        memcpy(le->data, nav->saved_command, nav->saved_prefix_len);
        memcpy(le->data+nav->saved_prefix_len, selected, selected_len);
        le->data[total_len] = '\0';
        le->length = total_len;
        le->cursor_pos = total_len;
    }

    // Adjust scroll to keep selection visible
    // Show up to 10 items at a time
    int visible_items = 10;
    if(nav->completion_selected < nav->completion_scroll){
        nav->completion_scroll = nav->completion_selected;
    }
    else if(nav->completion_selected >= nav->completion_scroll + visible_items){
        nav->completion_scroll = nav->completion_selected - visible_items + 1;
    }
}

// Execute a command from command mode
// Returns: 0 = success, -1 = error, 1 = quit requested
static
int
nav_execute_command(JsonNav* nav, const char* command, size_t command_len){
    if(command_len == 0) return CMD_OK;

    strip_whitespace(&command, &command_len);
    if(command_len == 0) return CMD_OK;

    // Parse command and arguments
    const char* arg_start = NULL;
    size_t cmd_len = 0;
    size_t arg_len = 0;

    // Find first space to separate command from arguments
    for(size_t i = 0; i < command_len; i++){
        if(command[i] == ' '){
            cmd_len = i;
            // Skip spaces
            while(i < command_len && command[i] == ' ') i++;
            if(i < command_len){
                arg_start = command + i;
                arg_len = command_len - i;
            }
            break;
        }
    }
    if(cmd_len == 0) cmd_len = command_len;

    // Look up command in command table
    StringView cmd_sv = {.text = command, .length = cmd_len};
    const Command* cmd = cmd_by_name(cmd_sv);
    if(!cmd){
        // Unknown command
        nav_set_messagef(nav, "Unknown command: %.*s", (int)cmd_len, command);
        return CMD_ERROR;
    }
    // Found matching command, call handler
    const char* args = arg_start ? arg_start : "";
    size_t args_len = arg_start ? arg_len : 0;
    strip_whitespace(&args, &args_len);

    // Parse command signature
    CmdParams params = {0};
    int err = cmd_param_parse_signature(cmd->signature, &params);
    if(err){
        nav_set_messagef(nav, "Internal error: invalid command signature");
        return CMD_ERROR;
    }

    // Parse command line arguments
    CmdArgs cmdargs = {0};
    err = cmd_param_parse_args((StringView){.length = args_len, .text = args}, &params, &cmdargs);
    if(err){
        nav_set_messagef(nav, "Error: Invalid arguments for command");
        return CMD_ERROR;
    }

    // Call command handler
    return cmd->handler(nav, &cmdargs);
}

//------------------------------------------------------------
// Rendering
//------------------------------------------------------------

static
void
nav_render_value_summary(Drt* drt, DrJsonContext* jctx, DrJsonValue val, int max_width){
    // Render a one-line summary of a value
    switch(val.kind){
        case DRJSON_NULL:
            drt_puts(drt, "null", 4);
            break;
        case DRJSON_BOOL:
            if(val.boolean)
                drt_puts(drt, "true", 4);
            else
                drt_puts(drt, "false", 5);
            break;
        case DRJSON_NUMBER:
            drt_printf(drt, "%g", val.number);
            break;
        case DRJSON_INTEGER:
            drt_printf(drt, "%lld", (long long)val.integer);
            break;
        case DRJSON_UINTEGER:
            drt_printf(drt, "%llu", (unsigned long long)val.uinteger);
            break;
        case DRJSON_STRING: {
            const char* str = NULL;
            size_t len = 0;
            int err = drjson_get_str_and_len(jctx, val, &str, &len);
            if(!err && str){
                drt_putc(drt, '"');
                // Truncate if too long
                size_t to_print = len;
                if(to_print > (size_t)max_width - 3)
                    to_print = (size_t)max_width - 6; // room for "..." and quotes
                drt_puts(drt, str, to_print);
                if(to_print < len)
                    drt_puts(drt, "...", 3);
                drt_putc(drt, '"');
            }
            break;
        }
        case DRJSON_ARRAY: {
            int64_t len = drjson_len(jctx, val);
            if(len == 0){
                drt_puts(drt, "[]", 2);
            }
            else {
                drt_putc(drt, '[');
                int shown = 0;
                int complex_shown = 0; // Count of objects/arrays shown
                int budget = max_width - 20; // Reserve space for brackets, ", ... N more]"

                for(int64_t i = 0; i < len && budget > 5; i++){
                    DrJsonValue item = drjson_get_by_index(jctx, val, i);

                    // Stop after showing 1 complex type (object/array)
                    if(complex_shown >= 1 && (item.kind == DRJSON_OBJECT || item.kind == DRJSON_ARRAY))
                        break;

                    if(i > 0){
                        drt_puts(drt, ", ", 2);
                        budget -= 2;
                    }

                    // Render based on type
                    switch(item.kind){
                        case DRJSON_NULL:
                            if(budget >= 4){
                                drt_puts(drt, "null", 4);
                                budget -= 4;
                                shown++;
                            }
                            break;
                        case DRJSON_BOOL:
                            if(item.boolean){
                                if(budget >= 4){
                                    drt_puts(drt, "true", 4);
                                    budget -= 4;
                                    shown++;
                                }
                            }
                            else {
                                if(budget >= 5){
                                    drt_puts(drt, "false", 5);
                                    budget -= 5;
                                    shown++;
                                }
                            }
                            break;
                        case DRJSON_NUMBER:
                        case DRJSON_INTEGER:
                        case DRJSON_UINTEGER: {
                            char numbuf[32];
                            int nlen = 0;
                            if(item.kind == DRJSON_NUMBER)
                                nlen = snprintf(numbuf, sizeof numbuf, "%g", item.number);
                            else if(item.kind == DRJSON_INTEGER)
                                nlen = snprintf(numbuf, sizeof numbuf, "%lld", (long long)item.integer);
                            else
                                nlen = snprintf(numbuf, sizeof numbuf, "%llu", (unsigned long long)item.uinteger);

                            if(nlen > 0 && nlen < budget){
                                drt_puts(drt, numbuf, nlen);
                                budget -= nlen;
                                shown++;
                            }
                            else {
                                goto budget_exceeded;
                            }
                            break;
                        }
                        case DRJSON_STRING: {
                            const char* str = NULL;
                            size_t slen = 0;
                            int err = drjson_get_str_and_len(jctx, item, &str, &slen);
                            if(!err && str && budget >= 4){ // At least room for ""
                                drt_putc(drt, '"');
                                budget--;
                                size_t to_print = slen;
                                if((int)to_print > budget - 1)
                                    to_print = budget - 1;
                                drt_puts(drt, str, to_print);
                                budget -= (int)to_print;
                                drt_putc(drt, '"');
                                budget--;
                                shown++;
                            }
                            else {
                                goto budget_exceeded;
                            }
                            break;
                        }
                        case DRJSON_ARRAY: {
                            // Show array preview - try to show values for small arrays with basic types
                            int64_t arr_len = drjson_len(jctx, item);

                            if(budget < 5){
                                goto budget_exceeded;
                            }

                            drt_putc(drt, '[');
                            budget--;

                            // Try to show actual values for small arrays
                            int arr_items_shown = 0;
                            int arr_budget = budget - 10; // Reserve for closing bracket and ellipsis
                            int show_values = (arr_len <= 5 && arr_len > 0); // Only for small arrays

                            if(show_values){
                                for(int64_t ai = 0; ai < arr_len && arr_budget > 5; ai++){
                                    DrJsonValue arr_item = drjson_get_by_index(jctx, item, ai);

                                    // Only show basic types
                                    if(arr_item.kind != DRJSON_NULL &&
                                       arr_item.kind != DRJSON_BOOL &&
                                       arr_item.kind != DRJSON_NUMBER &&
                                       arr_item.kind != DRJSON_INTEGER &&
                                       arr_item.kind != DRJSON_UINTEGER &&
                                       arr_item.kind != DRJSON_STRING){
                                        show_values = 0;
                                        break;
                                    }

                                    if(ai > 0){
                                        drt_puts(drt, ", ", 2);
                                        arr_budget -= 2;
                                    }

                                    // Render the value
                                    int consumed = 0;
                                    switch(arr_item.kind){
                                        case DRJSON_NULL:
                                            if(arr_budget >= 4){
                                                drt_puts(drt, "null", 4);
                                                consumed = 4;
                                            }
                                            break;
                                        case DRJSON_BOOL:
                                            if(arr_item.boolean){
                                                if(arr_budget >= 4){
                                                    drt_puts(drt, "true", 4);
                                                    consumed = 4;
                                                }
                                            }
                                            else {
                                                if(arr_budget >= 5){
                                                    drt_puts(drt, "false", 5);
                                                    consumed = 5;
                                                }
                                            }
                                            break;
                                        case DRJSON_NUMBER:
                                        case DRJSON_INTEGER:
                                        case DRJSON_UINTEGER: {
                                            char numbuf[32];
                                            int nlen = 0;
                                            if(arr_item.kind == DRJSON_NUMBER)
                                                nlen = snprintf(numbuf, sizeof numbuf, "%g", arr_item.number);
                                            else if(arr_item.kind == DRJSON_INTEGER)
                                                nlen = snprintf(numbuf, sizeof numbuf, "%lld", (long long)arr_item.integer);
                                            else
                                                nlen = snprintf(numbuf, sizeof numbuf, "%llu", (unsigned long long)arr_item.uinteger);

                                            if(nlen > 0 && nlen < arr_budget){
                                                drt_puts(drt, numbuf, nlen);
                                                consumed = nlen;
                                            }
                                            break;
                                        }
                                        case DRJSON_STRING: {
                                            const char* str = NULL;
                                            size_t slen = 0;
                                            int err = drjson_get_str_and_len(jctx, arr_item, &str, &slen);
                                            if(!err && str && arr_budget >= 4){
                                                drt_putc(drt, '"');
                                                consumed = 1;
                                                size_t to_print = slen;
                                                if((int)to_print > arr_budget - 2)
                                                    to_print = arr_budget - 2;
                                                drt_puts(drt, str, to_print);
                                                consumed += (int)to_print;
                                                drt_putc(drt, '"');
                                                consumed += 1;
                                            }
                                            break;
                                        }
                                        default:
                                            break;
                                    }

                                    if(consumed == 0){
                                        show_values = 0;
                                        break;
                                    }

                                    arr_budget -= consumed;
                                    arr_items_shown++;
                                }
                            }

                            if(show_values && arr_items_shown == arr_len){
                                // Successfully showed all items
                                budget = arr_budget;
                            }
                            else {
                                // Fall back to just showing ellipsis
                                if(arr_len > 0){
                                    drt_puts(drt, "...", 3);
                                    budget -= 3;
                                }
                            }

                            drt_putc(drt, ']');
                            budget--;
                            shown++;
                            complex_shown++;
                            break;
                        }
                        case DRJSON_OBJECT: {
                            // Show object preview - try to show key:value pairs for small objects with basic types
                            DrJsonValue obj_keys = drjson_object_keys(item);
                            int64_t obj_keys_len = drjson_len(jctx, obj_keys);

                            if(budget < 5){
                                goto budget_exceeded;
                            }

                            drt_putc(drt, '{');
                            budget--;

                            int obj_shown = 0;
                            int obj_budget = budget - 10; // Reserve for closing brace and ellipsis
                            int show_values = (obj_keys_len <= 3 && obj_keys_len > 0); // Only for small objects

                            if(show_values){
                                for(int64_t ki = 0; ki < obj_keys_len && obj_budget > 10; ki++){
                                    DrJsonValue okey = drjson_get_by_index(jctx, obj_keys, ki);
                                    const char* okey_str = NULL;
                                    size_t okey_len = 0;
                                    int err = drjson_get_str_and_len(jctx, okey, &okey_str, &okey_len);

                                    if(err || !okey_str){
                                        show_values = 0;
                                        break;
                                    }

                                    DrJsonValue oval = drjson_object_get_item(jctx, item, okey_str, okey_len);

                                    // Only show basic types
                                    if(oval.kind != DRJSON_NULL &&
                                       oval.kind != DRJSON_BOOL &&
                                       oval.kind != DRJSON_NUMBER &&
                                       oval.kind != DRJSON_INTEGER &&
                                       oval.kind != DRJSON_UINTEGER &&
                                       oval.kind != DRJSON_STRING){
                                        show_values = 0;
                                        break;
                                    }

                                    if(ki > 0){
                                        drt_puts(drt, ", ", 2);
                                        obj_budget -= 2;
                                    }

                                    // Show key
                                    size_t to_print = okey_len;
                                    if((int)to_print > obj_budget - 5)
                                        to_print = obj_budget - 5;

                                    if(to_print > 0){
                                        drt_puts(drt, okey_str, to_print);
                                        obj_budget -= (int)to_print;
                                    }
                                    else {
                                        show_values = 0;
                                        break;
                                    }

                                    // Show ": "
                                    if(obj_budget >= 2){
                                        drt_puts(drt, ": ", 2);
                                        obj_budget -= 2;
                                    }
                                    else {
                                        show_values = 0;
                                        break;
                                    }

                                    // Show value
                                    int consumed = 0;
                                    switch(oval.kind){
                                        case DRJSON_NULL:
                                            if(obj_budget >= 4){
                                                drt_puts(drt, "null", 4);
                                                consumed = 4;
                                            }
                                            break;
                                        case DRJSON_BOOL:
                                            if(oval.boolean){
                                                if(obj_budget >= 4){
                                                    drt_puts(drt, "true", 4);
                                                    consumed = 4;
                                                }
                                            }
                                            else {
                                                if(obj_budget >= 5){
                                                    drt_puts(drt, "false", 5);
                                                    consumed = 5;
                                                }
                                            }
                                            break;
                                        case DRJSON_NUMBER:
                                        case DRJSON_INTEGER:
                                        case DRJSON_UINTEGER: {
                                            char numbuf[32];
                                            int nlen = 0;
                                            if(oval.kind == DRJSON_NUMBER)
                                                nlen = snprintf(numbuf, sizeof numbuf, "%g", oval.number);
                                            else if(oval.kind == DRJSON_INTEGER)
                                                nlen = snprintf(numbuf, sizeof numbuf, "%lld", (long long)oval.integer);
                                            else
                                                nlen = snprintf(numbuf, sizeof numbuf, "%llu", (unsigned long long)oval.uinteger);

                                            if(nlen > 0 && nlen < obj_budget){
                                                drt_puts(drt, numbuf, nlen);
                                                consumed = nlen;
                                            }
                                            break;
                                        }
                                        case DRJSON_STRING: {
                                            const char* str = NULL;
                                            size_t slen = 0;
                                            int err = drjson_get_str_and_len(jctx, oval, &str, &slen);
                                            if(!err && str && obj_budget >= 4){
                                                drt_putc(drt, '"');
                                                consumed = 1;
                                                size_t str_to_print = slen;
                                                if((int)str_to_print > obj_budget - 2)
                                                    str_to_print = obj_budget - 2;
                                                drt_puts(drt, str, str_to_print);
                                                consumed += (int)str_to_print;
                                                drt_putc(drt, '"');
                                                consumed += 1;
                                            }
                                            break;
                                        }
                                        default:
                                            break;
                                    }

                                    if(consumed == 0){
                                        show_values = 0;
                                        break;
                                    }

                                    obj_budget -= consumed;
                                    obj_shown++;
                                }
                            }

                            if(show_values && obj_shown == obj_keys_len){
                                // Successfully showed all items
                                budget = obj_budget;
                            }
                            else {
                                // Fall back to just showing keys
                                obj_shown = 0;
                                for(int64_t ki = 0; ki < obj_keys_len && budget > 10; ki++){
                                    DrJsonValue okey = drjson_get_by_index(jctx, obj_keys, ki);
                                    const char* okey_str = NULL;
                                    size_t okey_len = 0;
                                    int err = drjson_get_str_and_len(jctx, okey, &okey_str, &okey_len);

                                    if(!err && okey_str){
                                        if(obj_shown > 0){
                                            drt_puts(drt, ", ", 2);
                                            budget -= 2;
                                        }

                                        size_t to_print = okey_len;
                                        if((int)to_print > budget - 5)
                                            to_print = budget - 5;

                                        if(to_print > 0){
                                            drt_puts(drt, okey_str, to_print);
                                            budget -= (int)to_print;
                                            obj_shown++;
                                        }

                                        if(budget < 10)
                                            break;
                                    }
                                }

                                if(obj_shown < obj_keys_len){
                                    drt_puts(drt, ", ...", 5);
                                    budget -= 5;
                                }
                            }

                            drt_putc(drt, '}');
                            budget--;
                            shown++;
                            complex_shown++;
                            break;
                        }
                        default:
                            goto budget_exceeded;
                    }
                }
                budget_exceeded:

                if(shown < len){
                    int64_t remaining = len - shown;
                    char buf[64];
                    int blen = snprintf(buf, sizeof buf, ", ... %lld more]", (long long)remaining);
                    if(blen > 0 && blen < (int)sizeof buf){
                        drt_puts(drt, buf, blen);
                    }
                    else {
                        drt_puts(drt, ", ...]", 6);
                    }
                }
                else {
                    drt_putc(drt, ']');
                }
            }
            break;
        }
        case DRJSON_OBJECT: {
            int64_t len = drjson_len(jctx, val);
            if(len == 0){
                drt_puts(drt, "{}", 2);
            }
            else {
                drt_putc(drt, '{');
                DrJsonValue keys = drjson_object_keys(val);
                int64_t keys_len = drjson_len(jctx, keys);
                int shown = 0;
                int budget = max_width - 20; // Reserve space for braces, ", ... N more}"
                int show_values = (keys_len <= 5 && keys_len > 0); // Try to show values for small objects

                for(int64_t i = 0; i < keys_len && budget > 10; i++){
                    DrJsonValue key = drjson_get_by_index(jctx, keys, i);
                    const char* key_str = NULL;
                    size_t key_len = 0;
                    int err = drjson_get_str_and_len(jctx, key, &key_str, &key_len);

                    if(!err && key_str){
                        if(i > 0){
                            drt_puts(drt, ", ", 2);
                            budget -= 2;
                        }

                        // Show key
                        int to_print = (int)key_len;
                        if(to_print > budget - 10)
                            to_print = budget - 10;

                        if(to_print > 0){
                            drt_puts(drt, key_str, to_print);
                            budget -= to_print;
                        }
                        else {
                            break;
                        }

                        // Show value if enabled
                        if(show_values){
                            DrJsonValue value = drjson_object_get_item(jctx, val, key_str, key_len);

                            // Add ": "
                            if(budget >= 2){
                                drt_puts(drt, ": ", 2);
                                budget -= 2;
                            }
                            else {
                                break;
                            }

                            // Render value (use placeholders for complex types)
                            int consumed = 0;
                            switch(value.kind){
                                case DRJSON_NULL:
                                    if(budget >= 4){
                                        drt_puts(drt, "null", 4);
                                        consumed = 4;
                                    }
                                    break;
                                case DRJSON_BOOL:
                                    if(value.boolean){
                                        if(budget >= 4){
                                            drt_puts(drt, "true", 4);
                                            consumed = 4;
                                        }
                                    }
                                    else {
                                        if(budget >= 5){
                                            drt_puts(drt, "false", 5);
                                            consumed = 5;
                                        }
                                    }
                                    break;
                                case DRJSON_NUMBER:
                                case DRJSON_INTEGER:
                                case DRJSON_UINTEGER: {
                                    char numbuf[32];
                                    int nlen = 0;
                                    if(value.kind == DRJSON_NUMBER)
                                        nlen = snprintf(numbuf, sizeof numbuf, "%g", value.number);
                                    else if(value.kind == DRJSON_INTEGER)
                                        nlen = snprintf(numbuf, sizeof numbuf, "%lld", (long long)value.integer);
                                    else
                                        nlen = snprintf(numbuf, sizeof numbuf, "%llu", (unsigned long long)value.uinteger);

                                    if(nlen > 0 && nlen < budget){
                                        drt_puts(drt, numbuf, nlen);
                                        consumed = nlen;
                                    }
                                    break;
                                }
                                case DRJSON_STRING: {
                                    const char* str = NULL;
                                    size_t slen = 0;
                                    int err = drjson_get_str_and_len(jctx, value, &str, &slen);
                                    if(!err && str && budget >= 4){
                                        drt_putc(drt, '"');
                                        consumed = 1;
                                        size_t str_to_print = slen;
                                        if((int)str_to_print > budget - 2)
                                            str_to_print = budget - 2;
                                        drt_puts(drt, str, str_to_print);
                                        consumed += (int)str_to_print;
                                        if(str_to_print < slen && budget > consumed + 4){
                                            drt_puts(drt, "...", 3);
                                            consumed += 3;
                                        }
                                        drt_putc(drt, '"');
                                        consumed += 1;
                                    }
                                    break;
                                }
                                case DRJSON_ARRAY: {
                                    // Show placeholder for arrays
                                    int64_t arr_len = drjson_len(jctx, value);
                                    if(arr_len == 0){
                                        if(budget >= 2){
                                            drt_puts(drt, "[]", 2);
                                            consumed = 2;
                                        }
                                    }
                                    else {
                                        if(budget >= 5){
                                            drt_puts(drt, "[...]", 5);
                                            consumed = 5;
                                        }
                                    }
                                    break;
                                }
                                case DRJSON_OBJECT: {
                                    // Show placeholder for objects
                                    int64_t obj_len = drjson_len(jctx, value);
                                    if(obj_len == 0){
                                        if(budget >= 2){
                                            drt_puts(drt, "{}", 2);
                                            consumed = 2;
                                        }
                                    }
                                    else {
                                        if(budget >= 5){
                                            drt_puts(drt, "{...}", 5);
                                            consumed = 5;
                                        }
                                    }
                                    break;
                                }
                                default:
                                    break;
                            }

                            if(consumed == 0){
                                break; // Not enough budget
                            }
                            budget -= consumed;
                        }

                        shown++;
                    }
                }

                if(shown < keys_len){
                    int64_t remaining = keys_len - shown;
                    char buf[64];
                    int blen = snprintf(buf, sizeof buf, ", ... %lld more}", (long long)remaining);
                    if(blen > 0 && blen < (int)sizeof buf){
                        drt_puts(drt, buf, blen);
                    }
                    else {
                        drt_puts(drt, ", ...}", 6);
                    }
                }
                else {
                    drt_putc(drt, '}');
                }
            }
            break;
        }
        case DRJSON_ERROR:
            drt_puts(drt, "<error>", 7);
            break;
        default:
            drt_puts(drt, "<unknown>", 9);
            break;
    }
}

static const StringView HELP_LINES[] = {
    SV("DrJson TUI - Keyboard Commands"),
    SV(""),
    SV("Navigation:"),
    SV("  j/↓/J       Move cursor down"),
    SV("  k/↑/K       Move cursor up"),
    SV("  h/←         Jump to parent (and collapse)"),
    SV("  H           Jump to parent (keep expanded)"),
    SV("  l/→/L       Enter container (expand if needed)"),
    SV("  ]           Next sibling (skip children)"),
    SV("  [           Previous sibling"),
    SV("  -/_         Jump to parent (no collapse)"),
    SV(""),
    SV("Scrolling:"),
    SV("  Ctrl-D      Scroll down half page"),
    SV("  Ctrl-U      Scroll up half page"),
    SV("  Ctrl-F/PgDn Scroll down full page"),
    SV("  Ctrl-B/PgUp Scroll up full page"),
    SV("  g/Home      Jump to top"),
    SV("  G/End       Jump to bottom"),
    SV(""),
    SV("Viewport:"),
    SV("  zz          Center cursor on screen"),
    SV("  zt          Cursor to top of screen"),
    SV("  zb          Cursor to bottom of screen"),
    SV(""),
    SV("Editing:"),
    SV("  ck          Edit key (empty buffer)"),
    SV("  cv          Edit value (empty buffer)"),
    SV("  Enter       Edit current value (prefilled)"),
    SV("  r/R         Rename key (prefilled, object members only)"),
    SV("  dd          Delete current item"),
    SV("  o           Insert after cursor (arrays/objects)"),
    SV("  O           Insert before cursor (arrays/objects)"),
    SV("  mj/m↓/Ctrl-↓  Move item down (swap with next sibling)"),
    SV("  mk/m↑/Ctrl-↑  Move item up (swap with previous sibling)"),
    SV(""),
    SV("Expand/Collapse:"),
    SV("  Space       Toggle expand/collapse"),
    SV("  N+Enter     Jump to index N (e.g., 0↵, 15↵)"),
    SV("  zo/zO       Expand recursively (open)"),
    SV("  zc/zC       Collapse recursively (close)"),
    SV("  zR          Expand all (open all folds)"),
    SV("  zM          Collapse all (close all folds)"),
    SV(""),
    SV("Focus:"),
    SV("  f           Focus on current container (object/array)"),
    SV("  F           Unfocus to return to previous view"),
    SV("  :focus      Focus on current container"),
    SV("  :unfocus    Return to previous view"),
    SV(""),
    SV("Search:"),
    SV("  /           Start recursive search (case-insensitive)"),
    SV("              Supports re patterns: foo.*bar, test"),
    SV("  //          Start query search (press / twice, case-insensitive)"),
    SV("              Parses first part as a query, rest is the text pattern"),
    SV("  *           Search for word under cursor"),
    SV("  n           Next match"),
    SV("  N           Previous match"),
    SV(""),
    SV("In Edit Mode:"),
    SV("  Enter       Commit changes"),
    SV("  ESC/Ctrl-C  Cancel editing"),
    SV("  ←/→         Move cursor"),
    SV("  Backspace   Delete char before cursor"),
    SV("  Delete      Delete char at cursor"),
    SV("  Home/Ctrl-A Move to start"),
    SV("  End/Ctrl-E  Move to end"),
    SV("  Ctrl-K      Delete to end of line"),
    SV("  Ctrl-U      Delete entire line"),
    SV("  Note: Keys don't need quotes unless they start with \" or '"),
    SV(""),
    SV("In Search Mode:"),
    SV("  Enter       Execute search"),
    SV("  ESC/Ctrl-C  Cancel search"),
    SV("  ↑/Ctrl-P    Previous search (history)"),
    SV("  ↓/Ctrl-N    Next search (history)"),
    SV("  ←/→         Move cursor in search text"),
    SV("  Backspace   Delete char before cursor"),
    SV("  Delete      Delete char at cursor"),
    SV("  Home/Ctrl-A Move to start"),
    SV("  End/Ctrl-E  Move to end"),
    SV("  Ctrl-K      Delete to end of line"),
    SV("  Ctrl-U      Delete entire line"),
    SV("  Ctrl-W      Delete word backward"),
    SV(""),
    SV("Clipboard:"),
    SV("  yy          Yank (copy) current value to clipboard"),
    SV("  Y           Yank (copy) current value (no delay)"),
    SV("  yp          Yank (copy) current item's JSON path"),
    SV("  :yank/:y    Yank current value to clipboard"),
    SV("  :path       Yank current item's JSON path"),
    SV("  p/P         Paste from clipboard"),
    SV("  :paste/:p   Same as p key"),
    SV(""),
    SV("Mouse:"),
    SV("  Click       Jump to item and toggle expand"),
    SV("  Wheel       Scroll up/down"),
    SV(""),
    SV("Commands:"),
    SV("  :           Enter command mode"),
    SV("  :help       Show available commands"),
    SV("  :wq         Write and quit"),
    SV("  :reload/:e! Reload file from disk"),
    SV(""),
    SV("In Command Mode:"),
    SV("  Tab         Show completion menu"),
    SV("  Enter       Execute command"),
    SV("  ESC/Ctrl-C  Cancel command"),
    SV("  ←/→         Move cursor in command text"),
    SV("  Backspace   Delete char before cursor"),
    SV("  Delete      Delete char at cursor"),
    SV("  Home/Ctrl-A Move to start"),
    SV("  End/Ctrl-E  Move to end"),
    SV("  Ctrl-K      Delete to end of line"),
    SV("  Ctrl-U      Delete entire line"),
    SV("  Ctrl-W      Delete word backward"),
    SV(""),
    SV("In Completion Menu:"),
    SV("  ↑/Ctrl-P    Move selection up"),
    SV("  ↓/Ctrl-N    Move selection down"),
    SV("  Tab         Move to next completion"),
    SV("  Enter       Accept selected completion"),
    SV("  ESC/Ctrl-C  Cancel completion"),
    SV("  Any key     Cancel and continue editing"),
    SV(""),
    SV("Other:"),
    SV("  q/Q         Quit"),
    SV("  Ctrl-Z      Suspend (Unix only)"),
    SV("  ?/F1        Toggle this help"),
    SV(""),
    SV("Help Navigation:"),
    SV("  n/→         Next page"),
    SV("  p/←         Previous page"),
    SV("  Any other   Close help"),
};

// Calculate display width of UTF-8 string (counts code points, not bytes)
static
int
utf8_display_width(const char* str, size_t byte_len){
    int width = 0;
    for(size_t i = 0; i < byte_len; i++){
        unsigned char c = str[i];
        // Skip UTF-8 continuation bytes (10xxxxxx)
        if((c & 0xC0) == 0x80) continue;
        // This is a code point start
        width++;
    }
    return width;
}

// Shared help rendering function
// Takes an array of StringView lines and renders them with pagination
static
void
nav_render_help(Drt* drt, int screenw, int screenh, int page, int*_Nullable out_num_pages,
                        const StringView* help_lines, int total_lines){
    // Calculate lines available for content (leave room for borders and page indicator)
    int max_content_height = screenh - 6;  // Top border, bottom border, page indicator, margins
    if(max_content_height < 10) max_content_height = 10;

    // Calculate number of pages
    int num_pages = (total_lines + max_content_height - 1) / max_content_height;
    if(out_num_pages) *out_num_pages = num_pages;

    // Clamp page to valid range
    if(page < 0) page = 0;
    if(page >= num_pages) page = num_pages - 1;

    // Calculate which lines to show on this page
    int start_line = page * max_content_height;
    int end_line = start_line + max_content_height;
    if(end_line > total_lines) end_line = total_lines;
    int num_lines = end_line - start_line;

    // Find max display width needed (using UTF-8 code point count)
    int max_width = 0;
    for(int i = 0; i < total_lines; i++){
        int width = utf8_display_width(help_lines[i].text, help_lines[i].length);
        if(width > max_width) max_width = width;
    }

    int box_height = num_lines + 3;  // +3 for top border, bottom border, and page indicator
    int start_y = (screenh - box_height) / 2;
    if(start_y < 1) start_y = 1;

    int box_width = max_width + 4;  // +4 for left/right borders and padding
    int start_x = (screenw - box_width) / 2;
    if(start_x < 0) start_x = 0;

    // Draw top border: ┌───...───┐
    drt_move(drt, start_x, start_y);
    drt_puts(drt, "┌", 3);
    for(int x = 0; x < box_width - 2; x++){
        drt_puts(drt, "─", 3);
    }
    drt_puts(drt, "┐", 3);

    // Draw help text with side borders
    for(int i = 0; i < num_lines && start_y + i + 1 < screenh; i++){
        int line_idx = start_line + i;

        // Left border
        drt_move(drt, start_x, start_y + i + 1);
        drt_puts(drt, "│", 3);

        // Content
        drt_putc(drt, ' ');
        drt_push_state(drt);

        // Highlight headers with bold
        if(help_lines[line_idx].length && (help_lines[line_idx].text[help_lines[line_idx].length-1] == ':' || help_lines[line_idx].text[0] == ':')){
            drt_set_style(drt, DRT_STYLE_BOLD);
        }

        drt_puts_utf8(drt, help_lines[line_idx].text, help_lines[line_idx].length);
        drt_pop_state(drt);

        // Padding and right border
        int content_width = utf8_display_width(help_lines[line_idx].text, help_lines[line_idx].length);
        int padding = box_width - 2 - 1 - content_width;  // -2 for borders, -1 for left space
        for(int p = 0; p < padding; p++){
            drt_putc(drt, ' ');
        }
        drt_puts(drt, "│", 3);
    }

    // Draw bottom border with optional page indicator
    int bottom_y = start_y + num_lines + 1;
    if(num_pages > 1){
        // Left border
        drt_move(drt, start_x, bottom_y);
        drt_puts(drt, "│", 3);

        // Page indicator
        drt_putc(drt, ' ');
        int before_x, before_y;
        drt_cursor(drt, &before_x, &before_y);
        drt_printf(drt, "Page %d/%d", page + 1, num_pages);
        int after_x, after_y;
        drt_cursor(drt, &after_x, &after_y);
        int indicator_len = after_x - before_x;

        // Padding
        int padding = box_width - 2 - 1 - indicator_len;
        for(int p = 0; p < padding; p++){
            drt_putc(drt, ' ');
        }

        // Right border
        drt_puts(drt, "│", 3);

        // Final bottom border line
        bottom_y++;
    }

    // Draw bottom border: └───...───┘
    drt_move(drt, start_x, bottom_y);
    drt_puts(drt, "└", 3);
    for(int x = 0; x < box_width - 2; x++){
        drt_puts(drt, "─", 3);
    }
    drt_puts(drt, "┘", 3);
}

static
void
build_command_helps(void){
    if(cmd_helps) return;
    size_t command_count = sizeof commands / sizeof commands[0];
    size_t count = command_count;
    for(size_t i = 0; i < command_count - 1; i++){
        if(commands[i].handler != commands[i+1].handler){
            count+=2;
        }
    }
    count += 2;
    count++;
    StringView* helps = calloc(count, sizeof *helps);
    if(!helps) return;
    size_t j = 0;
    helps[j++] = SV("Commands");
    helps[j++] = SV("");
    for(size_t i = 0; i < command_count; i++){
        helps[j++] = commands[i].signature;
        if(i + 1 < command_count && commands[i].handler != commands[i+1].handler){
            helps[j++] = commands[i].short_help;
            helps[j++] = SV("");
        }
    }
    helps[j++] = commands[command_count-1].short_help;
    if(j != count) __builtin_debugtrap();
    cmd_helps = helps;
    cmd_helps_count = count;
    if(0)LOG("command_count: %zu\n", command_count);
}

// Build JSON path to current cursor position
// Returns number of characters written (not including null terminator)
static
size_t
nav_build_json_path(JsonNav* nav, char* buf, size_t buf_size){
    if(nav->item_count == 0 || buf_size == 0) return 0;

    NavItem* cursor_item = &nav->items[nav->cursor_pos];

    // Collect path components by walking backwards to root
    typedef struct PathComponent PathComponent;
    struct PathComponent {
        _Bool is_array_index;
        int64_t index;
        DrJsonAtom key;
    };

    PathComponent components[64]; // Support up to 64 levels deep
    int component_count = 0;

    // Start from cursor position and walk backwards to root
    size_t current_pos = nav->cursor_pos;
    int current_depth = cursor_item->depth;

    // Add the cursor item itself
    if(current_depth > 0 && component_count < 64){
        if(cursor_item->key.bits != 0){
            components[component_count].is_array_index = 0;
            components[component_count].key = cursor_item->key;
            component_count++;
        }
        else if(cursor_item->index >= 0){
            components[component_count].is_array_index = 1;
            components[component_count].index = cursor_item->index;
            component_count++;
        }
    }

    // Walk backwards to find all parent items
    for(size_t i = current_pos; i > 0 && current_depth > 0; i--){
        NavItem* item = &nav->items[i - 1];
        if(item->depth < current_depth){
            // Found a parent
            if(item->depth > 0 && component_count < 64){
                if(item->key.bits != 0){
                    components[component_count].is_array_index = 0;
                    components[component_count].key = item->key;
                    component_count++;
                }
                else if(item->index >= 0){
                    components[component_count].is_array_index = 1;
                    components[component_count].index = item->index;
                    component_count++;
                }
            }
            current_depth = item->depth;
        }
    }

    // Build the path string (components are in reverse order)
    size_t written = 0;

    // Start with root indicator
    if(written + 1 < buf_size){
        buf[written++] = '$';
    }

    // Add components in reverse order (from root to cursor)
    for(int i = component_count - 1; i >= 0 && written < buf_size; i--){
        if(components[i].is_array_index){
            // Array index: [123]
            char index_buf[32];
            int len = snprintf(index_buf, sizeof index_buf, "[%lld]", (long long)components[i].index);
            if(len > 0 && written + (size_t)len < buf_size){
                memcpy(buf + written, index_buf, len);
                written += len;
            }
        }
        else {
            // Object key: .keyname
            DrJsonValue key_val = drjson_atom_to_value(components[i].key);
            const char* key_str = NULL;
            size_t key_len = 0;
            int err = drjson_get_str_and_len(nav->jctx, key_val, &key_str, &key_len);
            if(!err && key_str && written + key_len + 1 < buf_size){
                buf[written++] = '.';
                memcpy(buf + written, key_str, key_len);
                written += key_len;
            }
        }
    }

    if(written < buf_size){
        buf[written] = '\0';
    }
    else if(buf_size > 0){
        buf[buf_size - 1] = '\0';
    }

    return written;
}

// Render one row of an array of numbers (up to 10 items per row)
static
void
nav_render_flat_array_row(Drt* drt, DrJsonContext* jctx, DrJsonValue val, int row_index){
    int64_t len = drjson_len(jctx, val);
    if(len == 0){
        drt_puts(drt, "[]", 2);
        return;
    }
    drt_puts(drt, "  ", 2);

    int64_t start_idx = row_index * ITEMS_PER_ROW;
    int64_t end_idx = start_idx + ITEMS_PER_ROW;
    if(end_idx > len) end_idx = len;

    // Calculate width needed for the largest index
    int max_width = snprintf(NULL, 0, "%lld", (long long)(len - 1));

    // Show index range with padding
    drt_push_state(drt);
    drt_set_8bit_color(drt, 3); // yellow for array indices
    drt_printf(drt, "%*lld – %*lld", max_width, (long long)start_idx, max_width, (long long)(end_idx - 1));
    drt_pop_state(drt);
    drt_puts(drt, ": ", 2);

    drt_putc(drt, '[');

    for(int64_t i = start_idx; i < end_idx; i++){
        DrJsonValue item = drjson_get_by_index(jctx, val, i);

        // Format the number
        char buf[64];
        int num_len = 0;
        if(item.kind == DRJSON_NUMBER){
            num_len = snprintf(buf, sizeof buf, "%g", item.number);
        }
        else if(item.kind == DRJSON_INTEGER){
            num_len = snprintf(buf, sizeof buf, "%lld", (unsigned long long)item.integer);
        }
        else if(item.kind == DRJSON_UINTEGER){
            num_len = snprintf(buf, sizeof buf, "%lld", (unsigned long long)item.uinteger);
        }

        if(i > start_idx){
            drt_puts(drt, ", ", 2);
        }

        drt_push_state(drt);
        drt_set_8bit_color(drt, 2);
        drt_puts(drt, buf, num_len);
        drt_pop_state(drt);
    }

    drt_putc(drt, ']');
}

static
void
nav_render(JsonNav* nav, Drt* drt, int screenw, int screenh, LineEditor* count_buffer){
    if(nav->needs_rebuild)
        nav_rebuild(nav);

    drt_move(drt, 0, 0);
    drt_clear_color(drt);
    drt_bg_clear_color(drt);

    // Track cursor position for text editing
    int cursor_x = -1;
    int cursor_y = -1;
    _Bool show_cursor = 0;

    // Render status line at top
    drt_push_state(drt);
    if(nav->search_input_active){
        const char* prompt = (nav->search_mode == SEARCH_QUERY) ? " Query Search: " : " Search: ";
        int prompt_len = (nav->search_mode == SEARCH_QUERY) ? 15 : 9;
        drt_puts(drt, prompt, prompt_len);
        int start_x = prompt_len;
        le_render(drt, &nav->search_buffer);
        cursor_x = start_x + (int)nav->search_buffer.cursor_pos;
        cursor_y = 0;
        show_cursor = 1;
    }
    else if(nav->search_buffer.length > 0){
        const char* search_label = (nav->search_mode == SEARCH_QUERY) ? "Query Search" : "Search";
        drt_printf(drt, " %s — %zu items — %s: %.*s ",
                   nav->filename[0] ? nav->filename : "DrJson TUI",
                   nav->item_count,
                   search_label,
                   (int)nav->search_buffer.length, nav->search_buffer.data);
    }
    else {
        drt_printf(drt, " %s — %zu items ",
                   nav->filename[0] ? nav->filename : "DrJson TUI",
                   nav->item_count);
    }

    // Show count accumulator if non-zero (right after main status)
    if(count_buffer->length > 0){
        int cx, cy;
        drt_cursor(drt, &cx, &cy);
        drt_puts(drt, "— Count: ", 11);  // 11 bytes (em dash is 3 bytes)
        int start_x = cx + 9;  // 9 display characters (em dash displays as 1 char)
        le_render(drt, count_buffer);
        // Count buffer is being edited, position cursor there
        cursor_x = start_x + (int)count_buffer->cursor_pos;
        cursor_y = 0;
        show_cursor = 1;
        drt_putc(drt, ' ');
    }

    // Show pending key if waiting for second key in sequence
    if(nav->pending_key != 0){
        int cx, cy;
        drt_cursor(drt, &cx, &cy);
        drt_printf(drt, "— %c", nav->pending_key);
    }

    drt_clear_to_end_of_row(drt);
    drt_pop_state(drt);

    // Render visible items
    size_t end_idx = nav->scroll_offset + (size_t)screenh - 2; // -1 for status line, -1 for breadcrumb
    if(end_idx > nav->item_count)
        end_idx = nav->item_count;

    int y_offset = 0; // Track extra lines inserted for array insertion
    for(size_t i = nav->scroll_offset; i < end_idx; i++){
        NavItem* item = &nav->items[i];
        int y = 1 + (int)(i - nav->scroll_offset) + y_offset;

        // Check if we should render an insert line before this item
        if(nav->insert_mode != INSERT_NONE && nav->insert_visual_pos == i){
            if(y < screenh - 1){ // Make sure we have room
                drt_move(drt, 0, y);

                // Get parent container depth for indentation
                NavItem* parent = &nav->items[nav->insert_container_pos];
                int insert_depth = parent->depth + 1;

                // Indentation
                for(int d = 0; d < insert_depth; d++){
                    drt_puts(drt, "  ", 2);
                }

                // No expansion indicator (leaf item)
                drt_puts(drt, "  ", 2);

                // Highlight as this is where we're inserting
                drt_push_state(drt);
                drt_set_style(drt, DRT_STYLE_BOLD|DRT_STYLE_UNDERLINE);

                if(nav->insert_mode == INSERT_ARRAY){
                    // Show array index
                    drt_push_state(drt);
                    drt_set_8bit_color(drt, 3); // yellow
                    drt_printf(drt, "%zu", nav->insert_index == SIZE_MAX ?
                              (size_t)drjson_len(nav->jctx, parent->value) : nav->insert_index);
                    drt_pop_state(drt);
                    drt_puts(drt, ": ", 2);

                    // Render edit buffer (value)
                    int start_x, start_y;
                    drt_cursor(drt, &start_x, &start_y);
                    le_render(drt, &nav->edit_buffer);
                    cursor_x = start_x + (int)nav->edit_buffer.cursor_pos;
                    cursor_y = y;
                    show_cursor = 1;
                }
                else if(nav->insert_mode == INSERT_OBJECT){
                    if(nav->edit_key_mode){
                        // Entering key - show the key being typed
                        int start_x, start_y;
                        drt_cursor(drt, &start_x, &start_y);
                        le_render(drt, &nav->edit_buffer);
                        cursor_x = start_x + (int)nav->edit_buffer.cursor_pos;
                        cursor_y = y;
                        show_cursor = 1;
                        drt_puts(drt, ": ", 2);
                    }
                    else {
                        // Entering value - show the key we already entered
                        const char* key_str;
                        size_t key_len;
                        int err = drjson_get_atom_str_and_length(nav->jctx, nav->insert_object_key, &key_str, &key_len);

                        drt_push_state(drt);
                        drt_set_8bit_color(drt, 6); // cyan
                        if(!err)
                            drt_puts(drt, key_str, key_len);
                        drt_pop_state(drt);
                        drt_puts(drt, ": ", 2);

                        // Render edit buffer (value)
                        int start_x, start_y;
                        drt_cursor(drt, &start_x, &start_y);
                        le_render(drt, &nav->edit_buffer);
                        cursor_x = start_x + (int)nav->edit_buffer.cursor_pos;
                        cursor_y = y;
                        show_cursor = 1;
                    }
                }

                drt_clear_to_end_of_row(drt);
                drt_pop_state(drt);

                y_offset++;
                y++;

                // Don't render more items if we've filled the screen
                if(y >= screenh - 1) break;
            }
        }

        drt_move(drt, 0, y);


        // Indentation
        for(int d = 0; d < item->depth; d++){
            drt_puts(drt, "  ", 2);
        }

        // For flat view items, skip the expansion indicator and key/index
        if(!item->is_flat_view){
            // Expansion indicator for containers
            if(nav_is_container(item->value)){
                if(nav_is_expanded(nav, item->value)){
                    drt_putc_mb(drt, "▼", 3, 1);
                }
                else {
                    drt_putc_mb(drt, "▶", 3, 1);
                }
                drt_putc(drt, ' ');
            }
            else {
                drt_puts(drt, "  ", 2);
            }
        }

        // Highlight cursor line
        if(i == nav->cursor_pos){
            drt_push_state(drt);
            drt_set_style(drt, DRT_STYLE_BOLD|DRT_STYLE_UNDERLINE);
        }

        // Key if object member, or index if array element (skip for flat view)
        if(!item->is_flat_view){
            if(item->key.bits != 0){
                // If editing the key, show edit buffer here (but not in insert mode, which renders at insertion position)
                if(i == nav->cursor_pos && nav->edit_mode && nav->edit_key_mode && nav->insert_mode == INSERT_NONE){
                    int start_x, start_y;
                    drt_cursor(drt, &start_x, &start_y);
                    le_render(drt, &nav->edit_buffer);
                    cursor_x = start_x + (int)nav->edit_buffer.cursor_pos;
                    cursor_y = y;
                    show_cursor = 1;
                    drt_puts(drt, ": ", 2);
                }
                else {
                    // Normal key rendering
                    const char* key_str = NULL;
                    size_t key_len = 0;
                    DrJsonValue key_val = drjson_atom_to_value(item->key);
                    int err = drjson_get_str_and_len(nav->jctx, key_val, &key_str, &key_len);
                    if(!err && key_str){
                        drt_push_state(drt);
                        drt_set_8bit_color(drt, 6); // cyan
                        drt_puts(drt, key_str, key_len);
                        drt_pop_state(drt);
                        drt_puts(drt, ": ", 2);
                    }
                }
            }
            else if(item->index >= 0){
                drt_push_state(drt);
                drt_set_8bit_color(drt, 3); // yellow
                drt_printf(drt, "%lld", (long long)item->index);
                drt_pop_state(drt);
                drt_puts(drt, ": ", 2);
            }
        }

        // Value summary or edit buffer if in edit mode
        int cx, cy;
        drt_cursor(drt, &cx, &cy);
        int remaining = screenw - cx;
        if(remaining < 10) remaining = 10;

        // Show edit buffer if this is the cursor line and we're in value edit mode
        // (but not in insert mode, which renders the buffer at the insert position)
        if(i == nav->cursor_pos && nav->edit_mode && !nav->edit_key_mode && nav->insert_mode == INSERT_NONE){
            int start_x = cx;
            le_render(drt, &nav->edit_buffer);
            cursor_x = start_x + (int)nav->edit_buffer.cursor_pos;
            cursor_y = y;
            show_cursor = 1;
        }
        else {
            // Use flat rendering for flat view items
            if(item->is_flat_view){
                nav_render_flat_array_row(drt, nav->jctx, item->value, (int)item->index);
            }
            else {
                nav_render_value_summary(drt, nav->jctx, item->value, remaining);
            }
        }

        // Clear rest of line
        drt_clear_to_end_of_row(drt);

        if(i == nav->cursor_pos){
            drt_pop_state(drt);
        }
    }

    // Check if we should render insert line at the end (appending)
    if(nav->insert_mode != INSERT_NONE && nav->insert_visual_pos >= end_idx && nav->insert_visual_pos >= nav->scroll_offset){
        int y = 1 + (int)(end_idx - nav->scroll_offset) + y_offset;
        if(y < screenh - 1){
            drt_move(drt, 0, y);

            // Get parent container depth for indentation
            NavItem* parent = &nav->items[nav->insert_container_pos];
            int insert_depth = parent->depth + 1;

            // Indentation
            for(int d = 0; d < insert_depth; d++){
                drt_puts(drt, "  ", 2);
            }

            // No expansion indicator (leaf item)
            drt_puts(drt, "  ", 2);

            // Highlight as this is where we're inserting
            drt_push_state(drt);
            drt_set_style(drt, DRT_STYLE_BOLD|DRT_STYLE_UNDERLINE);

            if(nav->insert_mode == INSERT_ARRAY){
                // Show array index
                drt_push_state(drt);
                drt_set_8bit_color(drt, 3); // yellow
                drt_printf(drt, "%zu", nav->insert_index == SIZE_MAX ?
                          (size_t)drjson_len(nav->jctx, parent->value) : nav->insert_index);
                drt_pop_state(drt);
                drt_puts(drt, ": ", 2);

                // Render edit buffer (value)
                int start_x, start_y;
                drt_cursor(drt, &start_x, &start_y);
                le_render(drt, &nav->edit_buffer);
                cursor_x = start_x + (int)nav->edit_buffer.cursor_pos;
                cursor_y = y;
                show_cursor = 1;
            }
            else if(nav->insert_mode == INSERT_OBJECT){
                if(nav->edit_key_mode){
                    // Entering key - show the key being typed
                    int start_x, start_y;
                    drt_cursor(drt, &start_x, &start_y);
                    le_render(drt, &nav->edit_buffer);
                    cursor_x = start_x + (int)nav->edit_buffer.cursor_pos;
                    cursor_y = y;
                    show_cursor = 1;
                    drt_puts(drt, ": ", 2);
                }
                else {
                    // Entering value - show the key we already entered
                    const char* key_str;
                    size_t key_len;
                    int err = drjson_get_atom_str_and_length(nav->jctx, nav->insert_object_key, &key_str, &key_len);

                    if(!err){
                        drt_push_state(drt);
                        drt_set_8bit_color(drt, 6); // cyan
                        drt_puts(drt, key_str, key_len);
                        drt_pop_state(drt);
                    }
                    drt_puts(drt, ": ", 2);

                    // Render edit buffer (value)
                    int start_x, start_y;
                    drt_cursor(drt, &start_x, &start_y);
                    le_render(drt, &nav->edit_buffer);
                    cursor_x = start_x + (int)nav->edit_buffer.cursor_pos;
                    cursor_y = y;
                    show_cursor = 1;
                }
            }

            drt_clear_to_end_of_row(drt);
            drt_pop_state(drt);

            y_offset++;
        }
    }

    // Clear remaining lines (but not the bottom line - that's for breadcrumb)
    for(int y = 1 + (int)(end_idx - nav->scroll_offset) + y_offset; y < screenh - 1; y++){
        drt_move(drt, 0, y);
        drt_clear_to_end_of_row(drt);
    }

    // Render completion menu if active (above command line)
    if(nav->in_completion_menu && nav->completion_count > 0){
        int visible_items = 10;
        if(nav->completion_count < visible_items) visible_items = nav->completion_count;

        // Render up to visible_items above the bottom line
        for(int i = 0; i < visible_items; i++){
            int match_idx = nav->completion_scroll + i;
            if(match_idx >= nav->completion_count) break;

            int y = screenh - 2 - visible_items + i;
            if(y < 1) break; // Don't overwrite status line

            drt_move(drt, 0, y);
            drt_push_state(drt);

            // Highlight selected item with bold and underline
            if(match_idx == nav->completion_selected){
                drt_set_style(drt, DRT_STYLE_BOLD | DRT_STYLE_UNDERLINE);
            }

            drt_putc(drt, ' ');
            drt_puts(drt, nav->completion_matches[match_idx], strlen(nav->completion_matches[match_idx]));
            drt_putc(drt, ' ');
            drt_clear_to_end_of_row(drt);
            drt_pop_state(drt);
        }
    }

    // Render breadcrumb (JSON path) or command prompt at bottom
    drt_move(drt, 0, screenh - 1);
    drt_push_state(drt);

    if(nav->command_mode){
        // Show command prompt
        drt_putc(drt, ':');
        int start_x = 1;
        le_render(drt, &nav->command_buffer);
        cursor_x = start_x + (int)nav->command_buffer.cursor_pos;
        cursor_y = screenh - 1;
        show_cursor = 1;
    }
    else if(nav->message_length > 0){
        // Show message to user (bold for visibility)
        drt_putc(drt, ' ');
        drt_set_style(drt, DRT_STYLE_BOLD);
        drt_puts(drt, nav->message, nav->message_length);
        drt_putc(drt, ' ');
    }
    else if(nav->item_count > 0){
        // Show breadcrumb (JSON path)
        char path_buf[512];
        size_t path_len = nav_build_json_path(nav, path_buf, sizeof path_buf);
        if(path_len > 0){
            drt_putc(drt, ' ');
            drt_puts(drt, path_buf, path_len);
            drt_putc(drt, ' ');
        }
    }
    drt_clear_to_end_of_row(drt);
    drt_pop_state(drt);

    if(nav->command_mode){
        StringView cmd_name = nav->command_buffer.sv;
        for(size_t i = 0; i < nav->command_buffer.length; i++){
            if(nav->command_buffer.data[i] == ' '){
                cmd_name.length = i;
                break;
            }
        }
        const Command* cmd = cmd_by_name(cmd_name);
        if(cmd){
            drt_move(drt, 0, screenh-2);
            drt_push_state(drt);
            drt_set_style(drt, DRT_STYLE_ITALIC);
            drt_set_8bit_color(drt, 7);
            drt_puts(drt, cmd->signature.text, cmd->signature.length);
            drt_pop_state(drt);
            drt_clear_to_end_of_row(drt);
        }
    }

    // Position cursor and show/hide based on whether we're editing
    if(show_cursor && cursor_x >= 0 && cursor_y >= 0){
        drt_move_cursor(drt, cursor_x, cursor_y);
        drt_set_cursor_visible(drt, 1);
    }
    else {
        drt_set_cursor_visible(drt, 0);
    }
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
    SetConsoleMode(globals.TS.STDIN, ENABLE_VIRTUAL_TERMINAL_INPUT);
    SetConsoleMode(globals.TS.STDOUT, ENABLE_PROCESSED_OUTPUT|ENABLE_WRAP_AT_EOL_OUTPUT|ENABLE_VIRTUAL_TERMINAL_PROCESSING|DISABLE_NEWLINE_AUTO_RETURN);
#endif
    // alternative buffer
    printf("\033[?1049h");
    fflush(stdout);
    // thin cursor
    printf("\x1b[5 q");
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
    SetConsoleMode(globals.TS.STDIN, ENABLE_VIRTUAL_TERMINAL_INPUT);
    SetConsoleMode(globals.TS.STDOUT, ENABLE_PROCESSED_OUTPUT|ENABLE_WRAP_AT_EOL_OUTPUT|ENABLE_VIRTUAL_TERMINAL_PROCESSING|DISABLE_NEWLINE_AUTO_RETURN);
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
DRJSON_WARN_UNUSED
int
parse_as_string(DrJsonContext* jctx, const char* txt, size_t len, DrJsonAtom* outatom){
    strip_whitespace(&txt, &len);
    if(!len || (*txt != '"' && *txt != '\''))
        return drjson_atomize(jctx, txt, len, outatom);
    DrJsonParseContext pctx = {
        .cursor = txt,
        .begin = txt,
        .end = txt + len,
        .depth = 0,
        .ctx = jctx,
    };
    DrJsonValue new_value = drjson_parse(&pctx, 0);
    if(new_value.kind == DRJSON_ERROR)
        return 1;
    if(pctx.cursor == pctx.end && new_value.kind == DRJSON_STRING){
        *outatom = new_value.atom;
        return 0;
    }
    return drjson_atomize(jctx, txt, len, outatom);
}

static
DRJSON_WARN_UNUSED
int
parse_as_value(DrJsonContext* jctx, const char* txt, size_t len, DrJsonValue* outvalue){
    strip_whitespace(&txt, &len);
    if(!len) return 1;
    DrJsonParseContext pctx = {
        .cursor = txt,
        .begin = txt,
        .end = txt + len,
        .depth = 0,
        .ctx = jctx,
    };
    DrJsonValue new_value = drjson_parse(&pctx, 0);
    if(new_value.kind == DRJSON_ERROR)
        return 1;
    if(pctx.cursor != pctx.end){
        if(len && (*txt != '"' && *txt != '\'') && new_value.kind == DRJSON_STRING){
            DrJsonAtom at;
            int err = drjson_atomize(jctx, txt, len, &at);
            if(err) return err;
            new_value = drjson_atom_to_value(at);
        }
        else {
            return 1;
        }
    }
    *outvalue = new_value;
    return 0;
}


int
main(int argc, const char*_Nonnull const*_Nonnull argv){
    // Set these here instead of in static storage for bss optimization.
    globals.needs_recalc = 1;
    globals.needs_rescale = 1;
    globals.needs_redisplay = 1;
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

    _Bool braceless = 0;
    ArgToParse kw_args[] = {
        {
            .name = SV("--braceless"),
            .dest = ARGDEST(&braceless),
            .help = "Don't require opening and closing braces around the document",
        },
        {
            .name = SV("--intern-objects"),
            .altname1 = SV("--intern"),
            .help = "Reuse duplicate arrays and objects while parsing. Slower but can use less memory. Sometimes.",
            .dest = ARGDEST(&globals.intern),
            .hidden = 1,
        },
        {
            .name = SV("-l"),
            .altname1 = SV("--logfile"),
            .dest = ARGDEST(&LOGFILE),
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
        .name = argc?argv[0]:"drj",
        .description = "TUI interface to drjson.",
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
            puts("drj v" DRJSON_VERSION);
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
    #ifdef _WIN32
    globals.TS.STDIN = GetStdHandle(STD_INPUT_HANDLE);
    globals.TS.STDOUT = GetStdHandle(STD_OUTPUT_HANDLE);
    #else
    int pid = getpid();
    LOG("pid: %d\n", pid);
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sighandler;
    sa.sa_flags = 0;
    sigaction(SIGWINCH, &sa, NULL);
    sigaction(SIGCONT, &sa, NULL);
    // signal(SIGWINCH, sighandler);
    #endif
    begin_tui();
    atexit(end_tui);
    LongString jsonstr = {0};
    if(read_file(jsonpath.text, &jsonstr) != 0){
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
    unsigned flags = DRJSON_PARSE_FLAG_NO_COPY_STRINGS | DRJSON_PARSE_FLAG_ERROR_ON_TRAILING;
    if(braceless) flags |= DRJSON_PARSE_FLAG_BRACELESS_OBJECT;
    if(globals.intern) flags |= DRJSON_PARSE_FLAG_INTERN_OBJECTS;
    DrJsonValue document = drjson_parse(&ctx, flags);
    if(document.kind == DRJSON_ERROR){
        size_t l, c;
        drjson_get_line_column(&ctx, &l, &c);
        drjson_print_error_fp(stderr,  jsonpath.text, jsonpath.length, l, c, document);
        return 1;
    }
    // Initialize navigation
    JsonNav nav;
    nav_init(&nav, jctx, document, jsonpath.text, allocator);
    nav.was_opened_with_braceless = braceless;  // Track initial file's braceless state

    // Count buffer for vim-style numeric prefixes
    LineEditor count_buffer;
    le_init(&count_buffer, 32);

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

        // Render the navigation view
        nav_render(&nav, &globals.drt, globals.screenw, globals.screenh, &count_buffer);

        // Render help overlay if active
        if(nav.show_help){
            nav_render_help(&globals.drt, globals.screenw, globals.screenh, nav.help_page, NULL, nav.help_lines, nav.help_lines_count);
        }

        drt_paint(&globals.drt);

        int c = 0, cx = 0, cy = 0, magnitude = 0;
        int kmod = 0;
        int r = get_input(&globals.TS, &globals.needs_rescale, &c, &cx, &cy, &magnitude, &kmod);
        if(r == -1) goto finally;
        if(!r) continue;

        // If help is showing, handle help navigation
        if(nav.show_help){
            int num_pages = 0;
            nav_render_help(&globals.drt, globals.screenw, globals.screenh, nav.help_page, &num_pages, nav.help_lines, nav.help_lines_count);

            // Clamp help_page in case window was resized
            if(nav.help_page >= num_pages)
                nav.help_page = num_pages - 1;
            if(nav.help_page < 0)
                nav.help_page = 0;
            if(kmod) continue;
            switch(c){
                case 'n':
                case RIGHT:
                    // Next page
                    if(nav.help_page < num_pages - 1)
                        nav.help_page++;
                    continue;
                case 'p':
                case LEFT:
                    // Previous page
                    if(nav.help_page > 0)
                        nav.help_page--;
                    continue;
                default:
                    // Any other key closes help
                    nav.show_help = 0;
                    nav.help_page = 0;
                    le_clear(&count_buffer);
                    continue;
            }
        }

        // Handle search input mode
        if(nav.search_input_active){
            if(kmod) continue;
            switch(c){
                case ESC:
                case CTRL_C:
                    // Cancel search
                    nav.search_mode = SEARCH_INACTIVE;
                    nav.search_input_active = 0;
                    le_clear(&nav.search_buffer);
                    continue;
                case '/':
                    // If buffer is empty and in recursive mode, switch to query mode
                    if(nav.search_buffer.length == 0 && nav.search_mode == SEARCH_RECURSIVE){
                        nav.search_mode = SEARCH_QUERY;
                        continue;
                    }
                    // Otherwise, treat as regular character input
                    break;
                case ENTER:
                case CTRL_J:{
                    // Add to history before searching
                    le_history_add(&nav.search_history, nav.search_buffer.data, nav.search_buffer.length);
                    le_history_reset(&nav.search_buffer);

                    // Save search string before setup (since setup modifies the buffer)
                    char search_str[256];
                    size_t search_len = nav.search_buffer.length < sizeof search_str ? nav.search_buffer.length : sizeof search_str - 1;
                    memcpy(search_str, nav.search_buffer.data, search_len);
                    search_str[search_len] = '\0';

                    // Set up search using helper
                    enum SearchMode mode = nav.search_mode;
                    if(nav_setup_search(&nav, search_str, search_len, mode) != 0){
                        // Setup failed (e.g., query parse failed), fall back to recursive
                        nav_setup_search(&nav, search_str, search_len, SEARCH_RECURSIVE);
                    }

                    // Perform search
                    nav_search_recursive(&nav);
                    nav.search_input_active = 0;
                    nav_center_cursor(&nav, globals.screenh);
                    continue;
                }
                case UP:
                case CTRL_P:
                    // Navigate to previous history entry
                    le_history_prev(&nav.search_buffer);
                    continue;
                case DOWN:
                case CTRL_N:
                    // Navigate to next history entry
                    le_history_next(&nav.search_buffer);
                    continue;
                default:
                    if(le_handle_key(&nav.search_buffer, c, 1))
                        // Common line editing keys handled
                        continue;
                    if(c >= 32 && c < 127){
                        // Add printable character
                        le_history_reset(&nav.search_buffer);
                        le_append_char(&nav.search_buffer, (char)c);
                        continue;
                    }
                    continue;
            }
        }

        // Handle command mode
        if(nav.command_mode){
            if(kmod) continue;
            // Handle completion menu navigation
            if(nav.in_completion_menu){
                switch(c){
                    case UP:
                    case CTRL_P:
                    case SHIFT_TAB:
                        nav_completion_move(&nav, -1);
                        continue;
                    case DOWN:
                    case CTRL_N:
                    case TAB:
                        nav_completion_move(&nav, 1);
                        continue;
                    case ENTER:
                    case CTRL_J:
                        nav_accept_completion(&nav);
                        continue;
                    case ESC:
                    case CTRL_C:
                        // Cancel completion menu
                        nav_cancel_completion(&nav);
                        continue;
                    default:
                        // Any other key exits completion menu and continues with normal handling
                        nav_exit_completion(&nav);
                        // Fall through to normal command mode handling
                        break;
                }
            }
            // Normal command mode handling
            switch(c){
                case ESC:
                case CTRL_C:
                    // Cancel command
                    nav.command_mode = 0;
                    nav.tab_count = 0;
                    le_clear(&nav.command_buffer);
                    continue;
                case ENTER:
                case CTRL_J:{
                    // Execute command
                    if(nav.command_buffer.length)
                        le_history_add(&nav.command_history, nav.command_buffer.data, nav.command_buffer.length);
                    int cmd_result = nav_execute_command(&nav, nav.command_buffer.data, nav.command_buffer.length);
                    nav.command_mode = 0;
                    nav.tab_count = 0;
                    le_clear(&nav.command_buffer);
                    if(cmd_result == CMD_QUIT)
                        goto finally;
                }continue;
                case TAB:
                    nav_complete_command(&nav);
                    continue;
                case UP:
                case CTRL_P:
                    // Navigate to previous history entry
                    le_history_prev(&nav.command_buffer);
                    continue;
                case DOWN:
                case CTRL_N:
                    // Navigate to next history entry
                    le_history_next(&nav.command_buffer);
                    continue;
                default:
                    if(le_handle_key(&nav.command_buffer, c, 0)){
                        // Common line editing keys handled
                        nav.tab_count = 0; // Reset tab completion
                        continue;
                    }
                    if(c >= 32 && c < 127){
                        // Add printable character
                        nav.tab_count = 0; // Reset tab completion
                        le_append_char(&nav.command_buffer, (char)c);
                        continue;
                    }
                    // Ignore other keys in command mode
                    continue;
            }
        }

        // Handle edit mode
        if(nav.edit_mode){
            if(kmod) continue;
            switch(c){
                case ESC:
                case CTRL_C:
                    // Cancel edit
                    nav.edit_mode = 0;
                    nav.edit_key_mode = 0;
                    nav.insert_mode = INSERT_NONE;
                    le_clear(&nav.edit_buffer);
                    continue;
                case ENTER:
                case CTRL_J:{
                    int err;
                    if(nav.edit_key_mode){
                        DrJsonAtom new_key = {0};
                        err = parse_as_string(nav.jctx, nav.edit_buffer.data, nav.edit_buffer.length, &new_key);
                        if(nav.insert_mode == INSERT_OBJECT){
                            // Store the key and switch to value editing
                            nav.insert_object_key = new_key;
                            nav.edit_key_mode = 0;
                            le_clear(&nav.edit_buffer);
                            continue; // Stay in edit mode but now editing value
                        }
                        // Find parent object and replace the key
                        size_t parent_idx = nav_find_parent(&nav, nav.cursor_pos);
                        if(parent_idx == SIZE_MAX) break;
                        NavItem* parent = &nav.items[parent_idx];
                        NavItem* item = &nav.items[nav.cursor_pos];
                        if(parent->value.kind == DRJSON_OBJECT){
                            err = drjson_object_replace_key_atom(nav.jctx, parent->value, item->key, new_key);
                            if(err){
                                nav_set_messagef(&nav, "Error: Key already exists or cannot be replaced");
                                goto exit_edit_mode;
                            }
                            nav.needs_rebuild = 1;
                            nav_rebuild(&nav);
                        }
                        goto exit_edit_mode;
                    }

                    // Commit the edit
                    DrJsonValue new_value;
                    err = parse_as_value(nav.jctx, nav.edit_buffer.data, nav.edit_buffer.length, &new_value);
                    if(err){
                        nav_set_messagef(&nav, "Error: Invalid value syntax");
                        goto exit_edit_mode;
                    }
                    if(nav.insert_mode == INSERT_ARRAY){
                        // Insert into the array
                        NavItem* array_item = &nav.items[nav.insert_container_pos];
                        DrJsonValue array = array_item->value;
                        if(array.kind != DRJSON_ARRAY){
                            nav_set_messagef(&nav, "Error: Not an array");
                            goto exit_edit_mode;
                        }
                        if(nav.insert_index == SIZE_MAX)
                            err = drjson_array_push_item(nav.jctx, array, new_value); // Append to end
                        else
                            err = drjson_array_insert_item(nav.jctx, array, nav.insert_index, new_value); // Insert at specific index
                        if(err){
                            nav_set_messagef(&nav, "Error: Could not insert into array");
                            goto exit_edit_mode;
                        }
                        nav_set_messagef(&nav, "Item inserted");
                        nav.needs_rebuild = 1;
                        nav_rebuild(&nav);
                        goto exit_edit_mode;
                    }
                    if(nav.insert_mode == INSERT_OBJECT){
                        // Insert into the object at the specified index
                        NavItem* object_item = &nav.items[nav.insert_container_pos];
                        DrJsonValue object = object_item->value;
                        if(object.kind != DRJSON_OBJECT){
                            nav_set_messagef(&nav, "Error: Not an object");
                            goto exit_edit_mode;
                        }
                        size_t insert_index = nav.insert_index;
                        if(insert_index == SIZE_MAX) insert_index = drjson_len(nav.jctx, object);
                        int err = drjson_object_insert_item_at_index(nav.jctx, object, nav.insert_object_key, new_value, insert_index);
                        if(err){
                            nav_set_messagef(&nav, "Error: Could not insert into object (key may already exist)");
                            goto exit_edit_mode;
                        }
                        nav_set_messagef(&nav, "Item inserted");
                        nav.needs_rebuild = 1;
                        nav_rebuild(&nav);
                        goto exit_edit_mode;
                    }
                    size_t parent_idx = nav_find_parent(&nav, nav.cursor_pos);
                    if(parent_idx == SIZE_MAX){
                        // it's the root
                        nav.root = new_value;
                        nav.needs_rebuild = 1;
                        nav_rebuild(&nav);
                        nav_set_messagef(&nav, "Root value updated");
                        goto exit_edit_mode;
                    }
                    NavItem* parent = &nav.items[parent_idx];
                    if(parent->value.kind == DRJSON_OBJECT){
                        NavItem* item = &nav.items[nav.cursor_pos];
                        err = drjson_object_set_item_atom(nav.jctx, parent->value, item->key, new_value);
                        if(err){
                            nav_set_messagef(&nav, "Error: Could not update value");
                            goto exit_edit_mode;
                        }
                        nav_set_messagef(&nav, "Value updated");
                        nav.needs_rebuild = 1;
                        nav_rebuild(&nav);
                        goto exit_edit_mode;
                    }
                    if(parent->value.kind == DRJSON_ARRAY){
                        NavItem* item = &nav.items[nav.cursor_pos];
                        if(item->is_flat_view){
                            nav_set_messagef(&nav, "Error: Array element editing of flat views not yet supported");
                            goto exit_edit_mode;
                        }
                        err = drjson_array_set_by_index(nav.jctx, parent->value, item->index, new_value);
                        if(err){
                            nav_set_messagef(&nav, "Error: Could not update value");
                            goto exit_edit_mode;
                        }
                        nav_set_messagef(&nav, "Value updated");
                        nav.needs_rebuild = 1;
                        nav_rebuild(&nav);
                    }

                    exit_edit_mode:;
                    nav.edit_mode = 0;
                    nav.edit_key_mode = 0;
                    nav.insert_mode = INSERT_NONE;
                    le_clear(&nav.edit_buffer);
                }continue;
                default:
                    if(le_handle_key(&nav.edit_buffer, c, 0))
                        continue;
                    if(c >= 32 && c < 127){
                        // Add printable character
                        le_append_char(&nav.edit_buffer, (char)c);
                        continue;
                    }
                    continue;
            }
        }

        // Handle digit input to build count
        if(c >= '0' && c <= '9'){
            if(kmod) continue;
            le_append_char(&count_buffer, (char)c);
            continue;
        }

        // Handle text editing for count buffer (only when count buffer has content)
        if(count_buffer.length > 0 && !kmod && le_handle_key(&count_buffer, c, 0)){
            continue;
        }

        // Clear any displayed message on user action
        if(nav.message_length > 0)
            nav_clear_message(&nav);

        // Handle pending multi-key sequences
        if(nav.pending_key != 0){
            int c2 = c;
            int c = nav.pending_key;
            nav.pending_key = 0;  // Clear pending state
            if(kmod) continue;

            if(c == 'z'){
                switch(c2){
                    case 'z':
                        // zz - center cursor
                        nav_center_cursor(&nav, globals.screenh);
                        continue;
                    case 't':
                        // zt - cursor to top of screen
                        nav.scroll_offset = nav.cursor_pos;
                        continue;
                    case 'b':{
                        // zb - cursor to bottom of screen
                        int visible_rows = globals.screenh - 2;  // Account for status and breadcrumb
                        if(visible_rows < 1) visible_rows = 1;
                        if(nav.cursor_pos >= (size_t)(visible_rows - 1)){
                            nav.scroll_offset = nav.cursor_pos - (size_t)(visible_rows - 1);
                        }
                        else {
                            nav.scroll_offset = 0;
                        }
                    }continue;
                    case 'c':
                    case 'C':
                        // zc/zC - collapse current item recursively
                        nav_collapse_recursive(&nav);
                        continue;
                    case 'o':
                    case 'O':
                        // zo/zO - expand recursively (open)
                        nav_expand_recursive(&nav);
                        nav_ensure_cursor_visible(&nav, globals.screenh);
                        continue;
                    case 'M':
                        // zM - collapse all (close all folds)
                        nav_collapse_all(&nav);
                        continue;
                    case 'R':
                        // zR - expand all (open all folds)
                        nav_expand_all(&nav);
                        continue;
                    default:
                        // Unrecognized z sequence, clear count and ignore
                        le_clear(&count_buffer);
                        continue;
                }
            }
            else if(c == 'c'){
                switch(c2){
                    case 'k':
                    case 'K':
                        // ck - edit key (empty buffer)
                        if(nav.item_count > 0){
                            NavItem* item = &nav.items[nav.cursor_pos];
                            if(item->key.bits != 0 && item->depth > 0){
                                nav.edit_mode = 1;
                                nav.edit_key_mode = 1;
                                le_clear(&nav.edit_buffer);
                            }
                            else {
                                nav_set_messagef(&nav, "Can only rename keys on object members");
                            }
                        }
                        continue;
                    case 'v':
                    case 'V':
                        // cv - edit value (empty buffer)
                        if(nav.item_count > 0){
                            nav.edit_mode = 1;
                            nav.edit_key_mode = 0;
                            le_clear(&nav.edit_buffer);
                        }
                        continue;
                    default:
                        // Unrecognized c sequence, clear count and ignore
                        le_clear(&count_buffer);
                        continue;
                }
            }
            else if(c == 'd'){
                switch(c2){
                    case 'd':{
                        // dd - delete current item
                        size_t parent_idx = nav_find_parent(&nav, nav.cursor_pos);
                        if(parent_idx == SIZE_MAX){
                            nav_set_messagef(&nav, "Cannot delete root value");
                            continue;
                        }
                        NavItem* parent = &nav.items[parent_idx];
                        NavItem* item = &nav.items[nav.cursor_pos];
                        if(parent->value.kind == DRJSON_OBJECT){
                            int err = drjson_object_delete_item_atom(nav.jctx, parent->value, item->key);
                            if(err){
                                nav_set_messagef(&nav, "Error: Could not delete item");
                                continue;
                            }
                            nav_set_messagef(&nav, "Item deleted");
                            nav.needs_rebuild = 1;
                            nav_rebuild(&nav);
                            // Move cursor up if we deleted the last item
                            if(nav.cursor_pos >= nav.item_count && nav.cursor_pos > 0){
                                nav.cursor_pos--;
                            }
                        }
                        if(parent->value.kind == DRJSON_ARRAY){
                            DrJsonValue result = drjson_array_del_item(nav.jctx, parent->value, (size_t)item->index);
                            if(result.kind == DRJSON_ERROR){
                                nav_set_messagef(&nav, "Error: Could not delete item");
                                continue;
                            }
                            nav_set_messagef(&nav, "Item deleted");
                            nav.needs_rebuild = 1;
                            nav_rebuild(&nav);
                            // Move cursor up if we deleted the last item
                            if(nav.cursor_pos >= nav.item_count && nav.cursor_pos > 0)
                                nav.cursor_pos--;
                        }
                    }continue;
                    default:
                        // Unrecognized d sequence, clear count and ignore
                        le_clear(&count_buffer);
                        continue;
                }
            }
            else if(c == 'y'){
                switch(c2){
                    case 'p':
                    case 'P':{
                        // yp - yank path
                        CmdArgs empty_args = {0};
                        cmd_path(&nav, &empty_args);
                        continue;
                    }

                    case 'y':
                    case 'Y':{
                        // yy - yank value
                        CmdArgs empty_args = {0};
                        cmd_yank(&nav, &empty_args);
                        continue;
                    }
                    default:
                        le_clear(&count_buffer);
                        continue;
                }
            }
            else if(c == 'm'){
                switch(c2){
                    case 'j':
                    case DOWN:
                        nav_move_item_relative(&nav, +1);
                        continue;
                    case 'k':
                    case UP:
                        nav_move_item_relative(&nav, -1);
                        continue;
                    default:
                        le_clear(&count_buffer);
                        continue;
                }
            }
        }
        if(kmod == KMOD_CTRL){
            switch(c){
                case UP:
                    nav_move_item_relative(&nav, -1);
                    continue;
                case DOWN:
                    nav_move_item_relative(&nav, +1);
                    continue;
                default:
                    continue;
            }
        }
        if(kmod) continue;

        // Handle input
        switch(c){
            case 'z':
            case 'c':
            case 'd':
            case 'y':
            case 'm':
                nav.pending_key = c;
                break;

            case CTRL_Z:
                #ifdef _WIN32
                #else
                end_tui();
                raise(SIGTSTP);
                begin_tui();
                globals.needs_redisplay = 1;
                #endif
                break;

            case 'f':{
                CmdArgs empty_args = {0};
                cmd_focus(&nav, &empty_args);
                break;
            }

            case 'F':{
                CmdArgs empty_args = {0};
                cmd_unfocus(&nav, &empty_args);
                break;
            }

            case UP:
            case 'k':
            case 'K':
                nav_move_cursor(&nav, -magnitude);
                nav_ensure_cursor_visible(&nav, globals.screenh);
                break;

            case DOWN:
            case 'j':
            case 'J':
                nav_move_cursor(&nav, magnitude);
                nav_ensure_cursor_visible(&nav, globals.screenh);
                break;

            case PAGE_UP:
            case CTRL_B:
                nav_move_cursor(&nav, -(globals.screenh - 2));
                nav_ensure_cursor_visible(&nav, globals.screenh);
                break;

            case PAGE_DOWN:
            case CTRL_F:
                nav_move_cursor(&nav, globals.screenh - 2);
                nav_ensure_cursor_visible(&nav, globals.screenh);
                break;

            case CTRL_U:
                // Scroll up half page (vim-style)
                nav_move_cursor(&nav, -(globals.screenh / 2));
                nav_ensure_cursor_visible(&nav, globals.screenh);
                break;

            case CTRL_D:
                // Scroll down half page (vim-style)
                nav_move_cursor(&nav, globals.screenh / 2);
                nav_ensure_cursor_visible(&nav, globals.screenh);
                break;

            case HOME:
            case 'g':
                nav.cursor_pos = 0;
                nav_ensure_cursor_visible(&nav, globals.screenh);
                break;

            case END:
            case 'G':
                // Go to end
                if(nav.item_count > 0)
                    nav.cursor_pos = nav.item_count - 1;
                nav_ensure_cursor_visible(&nav, globals.screenh);
                break;

            case CTRL_J:
            case ENTER:
                if(count_buffer.length > 0){
                    // Jump to item at index (0-indexed, matching array display)
                    int n = atoi(count_buffer.data);
                    nav_jump_to_nth_child(&nav, n);
                    nav_ensure_cursor_visible(&nav, globals.screenh);
                }
                else if(nav.item_count > 0){
                    editing_inline:;
                    NavItem* item = &nav.items[nav.cursor_pos];
                    // Enter edit mode for any value
                    nav.edit_mode = 1;
                    le_clear(&nav.edit_buffer);

                    // Use drjson_print_value_mem to serialize the value
                    char temp_buf[1024];
                    size_t printed = 0;
                    drjson_print_value_mem(nav.jctx, temp_buf, sizeof temp_buf, item->value, -1, 0, &printed);

                    // Copy to edit buffer
                    for(size_t i = 0; i < printed && i < nav.edit_buffer.capacity - 1; i++){
                        le_append_char(&nav.edit_buffer, temp_buf[i]);
                    }
                }
                break;

            case ' ':
                if(count_buffer.length > 0){
                    // Jump to item at index (0-indexed, matching array display)
                    int n = atoi(count_buffer.data);
                    nav_jump_to_nth_child(&nav, n);
                    nav_ensure_cursor_visible(&nav, globals.screenh);
                }
                else {
                    // Toggle expand/collapse
                    nav_toggle_expand_at_cursor(&nav);
                    nav_ensure_cursor_visible(&nav, globals.screenh);
                }
                break;

            case RIGHT:
            case 'l':
            case 'L':
                // Jump into container (expand if needed)
                nav_jump_into_container(&nav);
                nav_ensure_cursor_visible(&nav, globals.screenh);
                break;

            case LEFT:
            case 'h':
                if(nav.cursor_pos == 0){
                    CmdArgs empty_args = {0};
                    cmd_unfocus(&nav, &empty_args);
                    break;
                }
                // Jump to parent container and collapse it
                nav_jump_to_parent(&nav, 1);
                nav_ensure_cursor_visible(&nav, globals.screenh);
                break;

            case 'H':
                // Jump to parent container without collapsing
                nav_jump_to_parent(&nav, 0);
                nav_ensure_cursor_visible(&nav, globals.screenh);
                break;

            case 'A':
                if(nav.item_count > 0)
                    goto editing_inline;
                break;

            case 'C':
                if(nav.item_count > 0){
                    nav.edit_mode = 1;
                    le_clear(&nav.edit_buffer);
                }
                break;

            case 'r':
            case 'R':
                // Rename key (edit key mode)
                if(nav.item_count > 0){
                    NavItem* item = &nav.items[nav.cursor_pos];
                    // Only allow renaming keys for object members
                    if(item->key.bits != 0 && item->depth > 0){
                        nav.edit_mode = 1;
                        nav.edit_key_mode = 1;
                        le_clear(&nav.edit_buffer);

                        // Get the current key string and populate edit buffer (without quotes)
                        const char* key_str;
                        size_t key_len;
                        int err = drjson_get_atom_str_and_length(nav.jctx, item->key, &key_str, &key_len);

                        // Copy key as-is (no quotes)
                        if(!err){
                            for(size_t i = 0; i < key_len && nav.edit_buffer.length < nav.edit_buffer.capacity - 1; i++){
                                le_append_char(&nav.edit_buffer, key_str[i]);
                            }
                        }
                    }
                    else {
                        nav_set_messagef(&nav, "Can only rename keys on object members");
                    }
                }
                break;

            case '-':
            case '_':
                // Jump to parent container without collapsing
                nav_jump_to_parent(&nav, 0);
                nav_ensure_cursor_visible(&nav, globals.screenh);
                break;

            case ']':
                // Jump to next sibling (same depth)
                nav_jump_to_next_sibling(&nav);
                nav_ensure_cursor_visible(&nav, globals.screenh);
                break;

            case '[':
                // Jump to previous sibling (same depth)
                nav_jump_to_prev_sibling(&nav);
                nav_ensure_cursor_visible(&nav, globals.screenh);
                break;

            case '?':
            case F1:
                nav.show_help = 1;
                nav.help_lines = HELP_LINES;
                nav.help_lines_count = sizeof HELP_LINES / sizeof HELP_LINES[0];
                nav.help_page = 0;  // Start at first page
                break;

            case '/':
                // Enter search mode (always recursive)
                nav.search_mode = SEARCH_RECURSIVE;
                nav.search_input_active = 1;
                le_clear(&nav.search_buffer);
                break;

            case ';':
            case ':':
                // Enter command mode
                nav.command_mode = 1;
                le_clear(&nav.command_buffer);
                break;

            case '*':
                // Search for the thing under the cursor
                if(nav.item_count > 0){
                    NavItem* item = &nav.items[nav.cursor_pos];
                    const char* search_text = NULL;
                    size_t search_len = 0;

                    // Try to get the key first
                    if(item->key.bits != 0){
                        DrJsonValue key_val = drjson_atom_to_value(item->key);
                        (void)drjson_get_str_and_len(nav.jctx, key_val, &search_text, &search_len);
                    }
                    // If no key, try to get the value if it's a string
                    else if(item->value.kind == DRJSON_STRING){
                        (void)drjson_get_str_and_len(nav.jctx, item->value, &search_text, &search_len);
                    }
                    // If value is a number, convert it to string for searching
                    else if(item->value.kind == DRJSON_INTEGER || item->value.kind == DRJSON_UINTEGER || item->value.kind == DRJSON_NUMBER){
                        static char number_buf[64];
                        if(item->value.kind == DRJSON_INTEGER){
                            search_len = (size_t)snprintf(number_buf, sizeof number_buf, "%lld", (long long)item->value.integer);
                        }
                        else if(item->value.kind == DRJSON_UINTEGER){
                            search_len = (size_t)snprintf(number_buf, sizeof number_buf, "%llu", (unsigned long long)item->value.uinteger);
                        }
                        else {
                            search_len = (size_t)snprintf(number_buf, sizeof number_buf, "%g", item->value.number);
                        }
                        search_text = number_buf;
                    }

                    // If we found text, search for it
                    if(search_text && search_len > 0){
                        le_clear(&nav.search_buffer);
                        // Copy the text to the search buffer
                        for(size_t i = 0; i < search_len; i++){
                            le_append_char(&nav.search_buffer, search_text[i]);
                        }
                        // Set search mode and parse as number
                        nav.search_mode = SEARCH_RECURSIVE;
                        nav.search_numeric.is_numeric = 0;

                        // Try to parse as number
                        Int64Result int_res = parse_int64(search_text, search_len);
                        if(int_res.errored == PARSENUMBER_NO_ERROR){
                            nav.search_numeric.is_numeric = 1;
                            nav.search_numeric.is_integer = 1;
                            nav.search_numeric.int_value = int_res.result;
                        }
                        else {
                            Uint64Result uint_res = parse_uint64(search_text, search_len);
                            if(uint_res.errored == PARSENUMBER_NO_ERROR){
                                nav.search_numeric.is_numeric = 1;
                                nav.search_numeric.is_integer = 1;
                                nav.search_numeric.int_value = (int64_t)uint_res.result;
                            }
                            else {
                                DoubleResult double_res = parse_double(search_text, search_len);
                                if(double_res.errored == PARSENUMBER_NO_ERROR){
                                    nav.search_numeric.is_numeric = 1;
                                    nav.search_numeric.is_integer = 0;
                                    nav.search_numeric.double_value = double_res.result;
                                }
                            }
                        }

                        // Perform recursive search immediately
                        nav_search_recursive(&nav);
                        nav_center_cursor(&nav, globals.screenh);
                    }
                }
                break;

            case 'n':
                // Next search match
                nav_search_next(&nav);
                nav_center_cursor(&nav, globals.screenh);
                break;

            case 'N':
                // Previous search match
                nav_search_prev(&nav);
                nav_center_cursor(&nav, globals.screenh);
                break;

            case 'Y':{
                // Yank (copy) current value to clipboard
                CmdArgs empty_args = {0};
                cmd_yank(&nav, &empty_args);
                break;
            }

            case 'p':
            case 'P':
                // Paste from clipboard
                do_paste(&nav, nav.cursor_pos, c=='p');
                break;

            case 'o':
            case 'O':
                if(!nav.item_count) break;

                {
                    NavItem* item = &nav.items[nav.cursor_pos];
                    NavItem* parent = NULL;
                    size_t insert_idx = 0;
                    size_t insert_container_pos = nav.cursor_pos;
                    // If cursor is on an expanded container, insert into it
                    if(nav_is_expanded(&nav, item->value)){
                        parent = item;
                        insert_idx = c == 'o'?SIZE_MAX:0;
                    }
                    else {
                        // Find parent object
                        size_t parent_pos = 0;
                        for(size_t i = nav.cursor_pos; i > 0; i--){
                            if(nav.items[i-1].depth == item->depth)
                                insert_idx++;
                            if(nav.items[i - 1].depth < item->depth){
                                parent = &nav.items[i - 1];
                                parent_pos = i - 1;
                                break;
                            }
                        }
                        if(!parent) break; // bug
                        if(c == 'o') insert_idx++;
                        insert_container_pos = parent_pos;
                    }
                    nav.insert_index = insert_idx;
                    nav.edit_mode = 1;
                    nav.edit_key_mode = parent->value.kind == DRJSON_OBJECT;
                    nav.insert_container_pos = insert_container_pos;
                    le_clear(&nav.edit_buffer);
                    nav.insert_mode = parent->value.kind == DRJSON_OBJECT? INSERT_OBJECT : INSERT_ARRAY;
                    nav.insert_visual_pos = nav_calc_insert_visual_pos(&nav, nav.insert_container_pos, nav.insert_index);

                    // Ensure the insert position is visible on screen
                    if(nav.insert_visual_pos < nav.scroll_offset)
                        nav.scroll_offset = nav.insert_visual_pos;
                    else if(nav.insert_visual_pos >= nav.scroll_offset + (size_t)(globals.screenh - 2))
                        nav.scroll_offset = nav.insert_visual_pos - (size_t)(globals.screenh - 3);
                }
                break;

            case LCLICK_DOWN:
                // Handle mouse clicks (only on release to avoid double-toggle)
                // Status line is at y=0, items start at y=1
                if(cy >= 1 && cy < globals.screenh){
                    size_t clicked_idx = (size_t)(cy - 1) + nav.scroll_offset;
                    if(clicked_idx < nav.item_count){
                        // Move cursor to clicked item
                        nav.cursor_pos = clicked_idx;
                        // Toggle expand if it's a container
                        NavItem* item = &nav.items[clicked_idx];
                        if(nav_is_container(item->value)){
                            nav_toggle_expand_at_cursor(&nav);
                        }
                    }
                }
                break;

            default:
                break;
        }

        // Reset count accumulator after command
        le_clear(&count_buffer);

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
    }
    finally:;
    le_free(&count_buffer);
    nav_free(&nav);
    return 0;
}
#ifdef __clang__
#pragma clang assume_nonnull end
#endif
