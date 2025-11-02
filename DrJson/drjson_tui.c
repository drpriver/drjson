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
#include "tui_get_input.h"
#include "tui_get_input.c"
#include "lineedit.h"
#define DRJSON_API static inline
#include "drjson.h"
// Access to private APIs
#include "drjson.c"
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

static inline
void
strip_whitespace(const char** ptext, size_t *pcount){
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
} globals = {
    .needs_recalc = 1, .needs_rescale = 1, .needs_redisplay = 1,
};

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
    SEARCH_NORMAL = 1,
    SEARCH_RECURSIVE = 2,
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

// Tracks which containers (objects/arrays) are expanded
// Uses the unique object_idx/array_idx as identifiers
typedef struct ExpansionSet ExpansionSet;
struct ExpansionSet {
    uint64_t* ids;              // Array of expanded container IDs
    size_t capacity;          // Allocated capacity
};

// Main navigation state
typedef struct JsonNav JsonNav;
struct JsonNav {
    DrJsonContext* jctx;      // DrJson context
    DrJsonValue root;         // Root document value

    // Flattened view (rebuilt when expansion state changes)
    NavItem* items;           // Dynamic array of visible items
    size_t item_count;        // Number of visible items
    size_t item_capacity;     // Allocated capacity

    // Expansion tracking
    ExpansionSet expanded;    // Set of expanded container IDs

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

    // Message display
    char message[512];        // Message to display to user
    _Bool has_message;        // Whether there's a message to show

    // Command mode
    LineEditor command_buffer;      // Command input buffer
    int tab_count;                  // Number of consecutive tabs pressed
    char saved_command[256];        // Original command before tab completion
    size_t saved_command_len;       // Length of saved command

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
    size_t* search_matches;         // Array of indices of items that match
    size_t search_match_count;
    size_t search_match_capacity;
    size_t current_match_idx;       // Index into search_matches array

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



static inline
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
_Bool
expansion_contains(const ExpansionSet* set, size_t id){
    size_t bit = id & 63;
    size_t idx = id / 64;
    uint64_t val = set->ids[idx];
    if(idx >= set->capacity) __builtin_debugtrap();
    return val & (1llu << bit)?1:0;
}

static inline
void
expansion_add(ExpansionSet* set, size_t id){
    size_t bit = id & 63;
    size_t idx = id / 64;
    if(idx >= set->capacity) __builtin_debugtrap();
    set->ids[idx] |= 1lu << bit;
}

static inline
void
expansion_remove(ExpansionSet* set, size_t id){
    size_t bit = id & 63;
    size_t idx = id / 64;
    if(idx >= set->capacity) __builtin_debugtrap();
    set->ids[idx] &= ~(1llu << bit);
}

static inline
void
expansion_toggle(ExpansionSet* set, size_t id){
    size_t bit = id & 63;
    size_t idx = id / 64;
    if(idx >= set->capacity) __builtin_debugtrap();
    set->ids[idx] ^= 1lu << bit;
}

static inline
void
expansion_clear(ExpansionSet* set){
    memset(set->ids, 0, sizeof *set->ids * set->capacity);
}

static inline
void
expansion_free(ExpansionSet* set){
    free(set->ids);
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
    return expansion_contains(&nav->expanded, nav_get_container_id(val));
}

static inline
void
nav_append_item(JsonNav* nav, NavItem item){
    if(nav->item_count >= nav->item_capacity){
        size_t new_cap = nav->item_capacity ? nav->item_capacity * 2 : 256;
        NavItem* new_items = realloc(nav->items, new_cap * sizeof *new_items);
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
nav_init(JsonNav* nav, DrJsonContext* jctx, DrJsonValue root){
    *nav = (JsonNav){
        .jctx = jctx,
        .root = root,
        .needs_rebuild = 1,
    };
    size_t expanded_count = jctx->arrays.count > jctx->objects.count? jctx->arrays.count : jctx->objects.count;
    expanded_count += 1;
    expanded_count *= 2;
    expanded_count /= 64;
    expanded_count++; // round up, whatever
    nav->expanded.ids = calloc(expanded_count, sizeof *nav->expanded.ids);
    nav->expanded.capacity = expanded_count;
    // Expand root document by default if it's a container
    if(nav_is_container(root)){
        expansion_add(&nav->expanded, nav_get_container_id(root));
    }
    le_init(&nav->search_buffer, 256);
    le_history_init(&nav->search_history);
    nav->search_buffer.history = &nav->search_history;
    le_init(&nav->command_buffer, 512);
    le_init(&nav->edit_buffer, 512);
    nav_rebuild(nav);
}

static
void
nav_reinit(JsonNav* nav){
    // Reset navigation state but keep buffers
    nav->cursor_pos = 0;
    nav->scroll_offset = 0;
    nav->needs_rebuild = 1;
    nav->has_message = 0;
    nav->show_help = 0;
    nav->command_mode = 0;

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
    nav->search_match_count = 0;

    nav->in_completion_menu = 0;
    nav->tab_count = 0;

    // Re-initialize expansion set for the new context
    size_t expanded_count = nav->jctx->arrays.count > nav->jctx->objects.count ? nav->jctx->arrays.count : nav->jctx->objects.count;
    expanded_count += 1;
    expanded_count *= 2;
    expanded_count /= 64;
    expanded_count++; // round up, whatever
    nav->expanded.ids = realloc(nav->expanded.ids, expanded_count * sizeof *nav->expanded.ids);
    nav->expanded.capacity = expanded_count;
    expansion_clear(&nav->expanded);

    if(nav_is_container(nav->root)){
        expansion_add(&nav->expanded, nav_get_container_id(nav->root));
    }
    nav_rebuild(nav);
}


static inline
void
nav_free(JsonNav* nav){
    free(nav->items);
    free(nav->search_matches);
    expansion_free(&nav->expanded);
    le_free(&nav->search_buffer);
    le_history_free(&nav->search_history);
    le_free(&nav->command_buffer);
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
                    expansion_toggle(&nav->expanded, id);
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
    expansion_toggle(&nav->expanded, id);
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
    expansion_add(&nav->expanded, nav_get_container_id(val));

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
    expansion_remove(&nav->expanded, nav_get_container_id(val));

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
                    expansion_remove(&nav->expanded, id);
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
        expansion_add(&nav->expanded, id);
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
    expansion_clear(&nav->expanded);
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

// Check if a NavItem matches the search query (case-insensitive substring match)
static
_Bool
nav_item_matches_query(JsonNav* nav, NavItem* item, const char* query, size_t query_len){
    if(query_len == 0) return 0;

    // Check key if present
    if(item->key.bits != 0){
        const char* key_str = NULL;
        size_t key_len = 0;
        DrJsonValue key_val = drjson_atom_to_value(item->key);
        drjson_get_str_and_len(nav->jctx, key_val, &key_str, &key_len);
        if(key_str){
            // Simple case-insensitive substring search
            for(size_t i = 0; i + query_len <= key_len; i++){
                _Bool match = 1;
                for(size_t j = 0; j < query_len; j++){
                    char c1 = key_str[i + j];
                    char c2 = query[j];
                    // Simple ASCII case-insensitive comparison
                    if(c1 >= 'A' && c1 <= 'Z') c1 = c1 - 'A' + 'a';
                    if(c2 >= 'A' && c2 <= 'Z') c2 = c2 - 'A' + 'a';
                    if(c1 != c2){
                        match = 0;
                        break;
                    }
                }
                if(match) return 1;
            }
        }
    }

    // Check value for strings
    if(item->value.kind == DRJSON_STRING){
        const char* str = NULL;
        size_t len = 0;
        drjson_get_str_and_len(nav->jctx, item->value, &str, &len);
        if(str){
            for(size_t i = 0; i + query_len <= len; i++){
                _Bool match = 1;
                for(size_t j = 0; j < query_len; j++){
                    char c1 = str[i + j];
                    char c2 = query[j];
                    if(c1 >= 'A' && c1 <= 'Z') c1 = c1 - 'A' + 'a';
                    if(c2 >= 'A' && c2 <= 'Z') c2 = c2 - 'A' + 'a';
                    if(c1 != c2){
                        match = 0;
                        break;
                    }
                }
                if(match) return 1;
            }
        }
    }

    return 0;
}

// Helper to check if a DrJsonValue matches the query
static
_Bool
nav_value_matches_query(JsonNav* nav, DrJsonValue val, DrJsonAtom key,
                         const char* query, size_t query_len){
    // Check key if present
    if(key.bits != 0){
        const char* key_str = NULL;
        size_t key_len = 0;
        DrJsonValue key_val = drjson_atom_to_value(key);
        drjson_get_str_and_len(nav->jctx, key_val, &key_str, &key_len);
        if(key_str){
            for(size_t i = 0; i + query_len <= key_len; i++){
                _Bool match = 1;
                for(size_t j = 0; j < query_len; j++){
                    char c1 = key_str[i + j];
                    char c2 = query[j];
                    if(c1 >= 'A' && c1 <= 'Z') c1 = c1 - 'A' + 'a';
                    if(c2 >= 'A' && c2 <= 'Z') c2 = c2 - 'A' + 'a';
                    if(c1 != c2){
                        match = 0;
                        break;
                    }
                }
                if(match) return 1;
            }
        }
    }

    // Check value for strings
    if(val.kind == DRJSON_STRING){
        const char* str = NULL;
        size_t len = 0;
        drjson_get_str_and_len(nav->jctx, val, &str, &len);
        if(str){
            for(size_t i = 0; i + query_len <= len; i++){
                _Bool match = 1;
                for(size_t j = 0; j < query_len; j++){
                    char c1 = str[i + j];
                    char c2 = query[j];
                    if(c1 >= 'A' && c1 <= 'Z') c1 = c1 - 'A' + 'a';
                    if(c2 >= 'A' && c2 <= 'Z') c2 = c2 - 'A' + 'a';
                    if(c1 != c2){
                        match = 0;
                        break;
                    }
                }
                if(match) return 1;
            }
        }
    }

    return 0;
}

// Recursive helper for recursive search
// Returns true if this value or any descendant matches the query
static
_Bool
nav_search_recursive_helper(JsonNav* nav, DrJsonValue val, DrJsonAtom key,
                              const char* query, size_t query_len){
    _Bool found_match = 0;

    // Check if this value matches
    if(nav_value_matches_query(nav, val, key, query, query_len)){
        found_match = 1;
        // Expand this container if it's a container
        if(nav_is_container(val)){
            expansion_add(&nav->expanded, nav_get_container_id(val));
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
                    expansion_add(&nav->expanded, nav_get_container_id(val));
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
                    expansion_add(&nav->expanded, nav_get_container_id(val));
                }
            }
        }
    }

    return found_match;
}

// Perform search and populate search_matches array
static
void
nav_search(JsonNav* nav){
    // Clear existing matches
    nav->search_match_count = 0;

    if(nav->search_buffer.length == 0) return;

    // Search through all visible items
    for(size_t i = 0; i < nav->item_count; i++){
        if(nav_item_matches_query(nav, &nav->items[i], nav->search_buffer.data, nav->search_buffer.length)){
            // Add to matches array
            if(nav->search_match_count >= nav->search_match_capacity){
                size_t new_cap = nav->search_match_capacity ? nav->search_match_capacity * 2 : 64;
                size_t* new_matches = realloc(nav->search_matches, new_cap * sizeof(size_t));
                if(!new_matches) continue;
                nav->search_matches = new_matches;
                nav->search_match_capacity = new_cap;
            }
            nav->search_matches[nav->search_match_count++] = i;
        }
    }

    // Jump to first match if any
    if(nav->search_match_count > 0){
        nav->current_match_idx = 0;
        nav->cursor_pos = nav->search_matches[0];
    }
}

// Perform recursive search (search entire tree, expanding as needed)
static
void
nav_search_recursive(JsonNav* nav){
    if(nav->search_buffer.length == 0) return;

    // Recursively search the entire tree and expand containers with matches
    nav_search_recursive_helper(nav, nav->root, (DrJsonAtom){0},
                                 nav->search_buffer.data, nav->search_buffer.length);

    // Rebuild to show expanded items
    nav->needs_rebuild = 1;
    nav_rebuild(nav);

    // Now search through visible items
    nav_search(nav);
}

// Jump to next search match
static inline
void
nav_search_next(JsonNav* nav){
    if(nav->search_match_count == 0) return;

    nav->current_match_idx = (nav->current_match_idx + 1) % nav->search_match_count;
    nav->cursor_pos = nav->search_matches[nav->current_match_idx];
}

// Jump to previous search match
static inline
void
nav_search_prev(JsonNav* nav){
    if(nav->search_match_count == 0) return;

    if(nav->current_match_idx == 0)
        nav->current_match_idx = nav->search_match_count - 1;
    else
        nav->current_match_idx--;

    nav->cursor_pos = nav->search_matches[nav->current_match_idx];
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
    } else {
        nav->scroll_offset = 0;
    }

    // Don't scroll past the end
    if(nav->scroll_offset + (size_t)visible_rows > nav->item_count){
        if(nav->item_count > (size_t)visible_rows){
            nav->scroll_offset = nav->item_count - (size_t)visible_rows;
        } else {
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
nav_set_message(JsonNav* nav, const char* fmt, ...){
    va_list args;
    va_start(args, fmt);
    vsnprintf(nav->message, sizeof(nav->message), fmt, args);
    va_end(args);
    nav->has_message = 1;
}

// Clear the current message
static inline
void
nav_clear_message(JsonNav* nav){
    nav->has_message = 0;
}

static int parse_as_string(DrJsonContext* jctx, const char* txt, size_t len, DrJsonAtom* outatom);
static int parse_as_value(DrJsonContext* jctx, const char* txt, size_t len, DrJsonValue* outvalue);

//------------------------------------------------------------
// Command Mode
//------------------------------------------------------------

// Command handler function type
// Returns: 0 = success, -1 = error, 1 = quit requested
typedef int (CommandHandler)(JsonNav* nav, const char* args, size_t args_len);

typedef struct Command Command;
struct Command {
    StringView name;
    StringView help_name;
    StringView short_help;
    CommandHandler* handler;
};
static CommandHandler cmd_help, cmd_write, cmd_quit, cmd_open, cmd_pwd, cmd_cd, cmd_yank, cmd_paste, cmd_query;

static const Command commands[] = {
    {SV("help"),  SV(":help"), SV("  Show help"),         cmd_help},
    {SV("h"),     SV(":h"), SV("  Show help"),         cmd_help},
    {SV("open"),  SV(":open <file>"), SV("  Open JSON at <file>"), cmd_open},
    {SV("o"),     SV(":o <file>"), SV("  Open JSON at <file>"), cmd_open},
    {SV("edit"),  SV(":edit <file>"), SV("  Open JSON at <file>"), cmd_open},
    {SV("e"),     SV(":e <file>"), SV("  Open JSON at <file>"), cmd_open},
    {SV("save"),  SV(":save <file>"), SV("  Save JSON to <file>"), cmd_write},
    {SV("w"),     SV(":w <file>"), SV("  Save JSON to <file>"), cmd_write},
    {SV("quit"),  SV(":quit"), SV("  Quit"),              cmd_quit},
    {SV("q"),     SV(":q"), SV("  Quit"),              cmd_quit},
    {SV("exit"),  SV(":exit"), SV("  Quit"),              cmd_quit},
    {SV("pwd"),   SV(":pwd"), SV("  Print working directory"), cmd_pwd},
    {SV("cd"),    SV(":cd <dir>"), SV("  Change directory"), cmd_cd},
    {SV("yank"),  SV(":yank"), SV("  Yank (copy) current value to clipboard"), cmd_yank},
    {SV("y"),     SV(":y"), SV("  Yank (copy) current value to clipboard"), cmd_yank},
    {SV("paste"), SV(":paste"), SV("  Paste from clipboard"), cmd_paste},
    {SV("p"),     SV(":p"), SV("  Paste from clipboard"), cmd_paste},
    {SV("query"), SV(":query <path>"), SV("  Navigate to path (e.g., foo.bar[0].baz)"), cmd_query},
};

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
cmd_open(JsonNav* nav, const char* args, size_t args_len){
    if(args_len == 0){
        nav_set_message(nav, "Error: No filename provided");
        return CMD_ERROR;
    }

    // Copy filename to null-terminated buffer
    char filepath[1024];
    if(args_len >= sizeof(filepath)){
        nav_set_message(nav, "Error: Filename too long");
        return CMD_ERROR;
    }
    memcpy(filepath, args, args_len);
    filepath[args_len] = '\0';

    LongString file_content = {0};
    if(read_file(filepath, &file_content) != 0){
        nav_set_message(nav, "Error: Could not read file '%s'", filepath);
        return CMD_ERROR;
    }

    DrJsonParseContext pctx = {
        .ctx = nav->jctx,
        .begin = file_content.text,
        .cursor = file_content.text,
        .end = file_content.text + file_content.length,
        .depth = 0,
    };
    unsigned parse_flags = 0;
    if(globals.intern) parse_flags |= DRJSON_PARSE_FLAG_INTERN_OBJECTS;
    DrJsonValue new_root = drjson_parse(&pctx, parse_flags);

    free((void*)file_content.text); // done with this now

    if(new_root.kind == DRJSON_ERROR){
        size_t line=0, col=0;
        drjson_get_line_column(&pctx, &line, &col);
        nav_set_message(nav, "Error parsing '%s': %s at line %zu col %zu", filepath, new_root.err_mess, line, col);
        drjson_gc(nav->jctx, &nav->root, 1);
        return CMD_ERROR;
    }
    nav->root = new_root;
    nav_reinit(nav);
    LOG("gc\n");
    drjson_gc(nav->jctx, &nav->root, 1);
    LOG("nav->jctx->arrays.count: %zu\n", nav->jctx->arrays.count);
    LOG("nav->jctx->free_array: %zu\n", nav->jctx->arrays.free_array);
    LOG("nav->jctx->objects.count: %zu\n", nav->jctx->objects.count);
    LOG("nav->jctx->free_object: %zu\n", nav->jctx->objects.free_object);

    nav_set_message(nav, "Opened '%s'", filepath);
    return CMD_OK;
}

static
int
cmd_write(JsonNav* nav, const char* args, size_t args_len){
    if(args_len == 0){
        nav_set_message(nav, "Error: No filename provided");
        return CMD_ERROR;
    }

    // Copy filename to null-terminated buffer
    char filepath[1024];
    if(args_len >= sizeof(filepath)){
        nav_set_message(nav, "Error: Filename too long");
        return CMD_ERROR;
    }
    memcpy(filepath, args, args_len);
    filepath[args_len] = '\0';

    // Write JSON to file
    FILE* fp = fopen(filepath, "wb");
    if(!fp){
        nav_set_message(nav, "Error: Could not open file '%s' for writing", filepath);
        return CMD_ERROR;
    }

    int print_err = drjson_print_value_fp(nav->jctx, fp, nav->root, 0, DRJSON_PRETTY_PRINT);
    int close_err = fclose(fp);

    if(print_err || close_err){
        nav_set_message(nav, "Error: Failed to write to '%s'", filepath);
        return CMD_ERROR;
    }

    nav_set_message(nav, "Wrote to '%s'", filepath);
    return CMD_OK;
}

static
int
cmd_quit(JsonNav* nav, const char* args, size_t args_len){
    (void)nav;
    (void)args;
    (void)args_len;
    return CMD_QUIT; // Signal quit
}

static
int
cmd_help(JsonNav* nav, const char* args, size_t args_len){
    (void)args;
    (void)args_len;
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
cmd_pwd(JsonNav* nav, const char* args, size_t args_len){
    (void)args;
    (void)args_len;

    char cwd[1024];
    #ifdef _WIN32
    DWORD len = GetCurrentDirectoryA(sizeof(cwd), cwd);
    if(len == 0 || len >= sizeof(cwd)){
        nav_set_message(nav, "Error: Could not get current directory");
        return CMD_ERROR;
    }
    #else
    if(getcwd(cwd, sizeof(cwd)) == NULL){
        nav_set_message(nav, "Error: Could not get current directory: %s", strerror(errno));
        return CMD_ERROR;
    }
    #endif

    nav_set_message(nav, "%s", cwd);
    return CMD_OK;
}

static
int
cmd_cd(JsonNav* nav, const char* args, size_t args_len){
    if(args_len == 0){
        // No argument - change to home directory
        #ifdef _WIN32
        const char* home = getenv("USERPROFILE");
        if(!home) home = getenv("HOMEDRIVE");
        #else
        const char* home = getenv("HOME");
        #endif

        if(!home){
            nav_set_message(nav, "Error: Could not determine home directory");
            return CMD_ERROR;
        }

        #ifdef _WIN32
        if(!SetCurrentDirectoryA(home)){
            nav_set_message(nav, "Error: Could not change to home directory");
            return CMD_ERROR;
        }
        #else
        if(chdir(home) != 0){
            nav_set_message(nav, "Error: Could not change to home directory: %s", strerror(errno));
            return CMD_ERROR;
        }
        #endif

        nav_set_message(nav, "Changed to %s", home);
        return CMD_OK;
    }

    // Copy directory path to null-terminated buffer
    char dirpath[1024];
    if(args_len >= sizeof(dirpath)){
        nav_set_message(nav, "Error: Directory path too long");
        return CMD_ERROR;
    }
    memcpy(dirpath, args, args_len);
    dirpath[args_len] = '\0';

    #ifdef _WIN32
    if(!SetCurrentDirectoryA(dirpath)){
        nav_set_message(nav, "Error: Could not change directory to '%s'", dirpath);
        return CMD_ERROR;
    }
    #else
    if(chdir(dirpath) != 0){
        nav_set_message(nav, "Error: Could not change directory to '%s': %s", dirpath, strerror(errno));
        return CMD_ERROR;
    }
    #endif

    nav_set_message(nav, "Changed to %s", dirpath);
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
typedef void* (*objc_getClass_t)(const char*);
typedef void* (*sel_registerName_t)(const char*);
typedef void* (*objc_msgSend_t)(void*, void*, ...);

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
ObjCClipboard*
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
    } else {
        // Fallback: try old API
        void** NSStringPboardType_ptr = (void**)dlsym(cached.appkit, "NSStringPboardType");
        if(NSStringPboardType_ptr && *NSStringPboardType_ptr){
            cached.pasteboardType = *NSStringPboardType_ptr;
        } else {
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
cmd_yank(JsonNav* nav, const char* args, size_t args_len){
    (void)args;
    (void)args_len;

    if(nav->item_count == 0){
        nav_set_message(nav, "Error: Nothing to yank");
        return CMD_ERROR;
    }

    NavItem* item = &nav->items[nav->cursor_pos];

    // Determine what to yank: if item has a key, yank {key: value}, else just value
    DrJsonValue yank_value = item->value;

    if(item->key.bits != 0){
        // Create a temporary object with {key: value}
        DrJsonValue temp_obj = drjson_make_object(nav->jctx);
        drjson_object_set_item_atom(nav->jctx, temp_obj, item->key, item->value);
        yank_value = temp_obj;
    }

    #ifdef _WIN32
    // Windows: use in-memory buffer
    MemBuffer buf = {0};
    DrJsonTextWriter writer = {
        .up = &buf,
        .write = membuf_write,
    };

    int print_err = drjson_print_value(nav->jctx, &writer, yank_value, 0, 0);

    if(print_err){
        free(buf.data);
        nav_set_message(nav, "Error: Could not serialize value");
        return CMD_ERROR;
    }

    if(buf.size > 10*1024*1024){  // 10MB limit
        free(buf.data);
        nav_set_message(nav, "Error: Value too large to yank");
        return CMD_ERROR;
    }

    int result = copy_to_clipboard(buf.data, buf.size);
    free(buf.data);

    if(result != 0){
        nav_set_message(nav, "Error: Could not copy to clipboard");
        return CMD_ERROR;
    }

    #elif defined(__APPLE__)
    // macOS: use in-memory buffer and native clipboard API
    MemBuffer buf = {0};
    DrJsonTextWriter writer = {
        .up = &buf,
        .write = membuf_write,
    };

    int print_err = drjson_print_value(nav->jctx, &writer, yank_value, 0, DRJSON_APPEND_ZERO);

    if(print_err){
        free(buf.data);
        nav_set_message(nav, "Error: Could not serialize value");
        return CMD_ERROR;
    }

    if(0)
    if(buf.size > 10*1024*1024){  // 10MB limit
        free(buf.data);
        nav_set_message(nav, "Error: Value too large to yank");
        return CMD_ERROR;
    }

    int result = macos_copy_to_clipboard(buf.data, buf.size);
    free(buf.data);

    if(result != 0){
        nav_set_message(nav, "Error: Could not copy to clipboard");
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
        nav_set_message(nav, "Error: Could not open clipboard command (tried tmux, xclip, xsel)");
        return CMD_ERROR;
    }

    int print_err = drjson_print_value_fp(nav->jctx, pipe, yank_value, 0, 0);
    int status = pclose(pipe);

    if(print_err || status != 0){
        nav_set_message(nav, "Error: Could not copy to clipboard");
        return CMD_ERROR;
    }
    #endif

    nav_set_message(nav, "Yanked to clipboard");
    return CMD_OK;
}

static
int
do_paste(JsonNav* nav, size_t cursor_pos, _Bool after){
    DrJsonValue paste_value;
    {
        // Read from clipboard
        LongString clipboard_text = {0};
        if(read_from_clipboard(&clipboard_text) != 0){
            nav_set_message(nav, "Error: Could not read from clipboard");
            return CMD_ERROR;
        }

        if(clipboard_text.length == 0){
            free((void*)clipboard_text.text);
            nav_set_message(nav, "Error: Clipboard is empty");
            return CMD_ERROR;
        }

        int err = parse_as_value(nav->jctx, clipboard_text.text, clipboard_text.length, &paste_value);
        free((void*)clipboard_text.text);
        if(err || paste_value.kind == DRJSON_ERROR){
            nav_set_message(nav, "Error: Clipboard does not contain valid JSON");
            return CMD_ERROR;
        }
        LOG("Read %zu bytes from clipboard\n", clipboard_text.length);
    }
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
                nav_set_message(nav, "Error: can't find parent");
                return CMD_ERROR;
            }
            if(after) insert_idx++;
        }
    }
    if(parent->value.kind == DRJSON_ARRAY){
        int err = drjson_array_insert_item(nav->jctx, parent->value, insert_idx, paste_value);
        if(err){
            nav_set_message(nav, "Error: couldn't insert into array at index %zu", insert_idx);
            return CMD_ERROR;
        }
    }
    else {
        if(parent->value.kind != DRJSON_OBJECT)
            return CMD_ERROR;
        if(paste_value.kind != DRJSON_OBJECT){
            nav_set_message(nav, "Error: can only paste objects into objects");
            return CMD_ERROR;
        }
        size_t len = drjson_len(nav->jctx, paste_value);
        for(size_t i = 0; i < len; i++){
            DrJsonValue key = drjson_get_by_index(nav->jctx, drjson_object_keys(paste_value), i);
            DrJsonValue value = drjson_get_by_index(nav->jctx, drjson_object_values(paste_value), i);
            int err = drjson_object_insert_item_at_index(nav->jctx, parent->value, key.atom, value, insert_idx);
            if(err){
                nav_set_message(nav, "Error: failed to insert key");
            }
            else {
                insert_idx++;
            }
        }
    }
    nav->needs_rebuild = 1;
    nav_rebuild(nav);
    return CMD_OK;
}

static
int
cmd_paste(JsonNav* nav, const char* args, size_t args_len){
    (void)args;
    (void)args_len;

    if(nav->item_count == 0){
        nav_set_message(nav, "Error: Nothing to paste into");
        return CMD_ERROR;
    }
    return do_paste(nav, nav->cursor_pos, 0);
}

static
int
cmd_query(JsonNav* nav, const char* args, size_t args_len){
    if(args_len == 0){
        nav_set_message(nav, "Error: No query path provided");
        return CMD_ERROR;
    }

    if(nav->item_count == 0){
        nav_set_message(nav, "Error: No JSON loaded");
        return CMD_ERROR;
    }

    // Parse the path into components
    DrJsonPath path;
    int parse_err = drjson_path_parse(nav->jctx, args, args_len, &path);
    if(parse_err){
        nav_set_message(nav, "Error: Invalid path syntax: %.*s", (int)args_len, args);
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
                nav_set_message(nav, "Error: Cannot index non-object with key at segment %zu", seg_idx);
                return CMD_ERROR;
            }

            DrJsonValue next = drjson_object_get_item_atom(nav->jctx, current, seg->key);
            if(next.kind == DRJSON_ERROR){
                const char* key_str;
                size_t key_len;
                drjson_get_atom_str_and_length(nav->jctx, seg->key, &key_str, &key_len);
                nav_set_message(nav, "Error: Key '%.*s' not found", (int)key_len, key_str);
                return CMD_ERROR;
            }

            // Expand the current container if needed
            if(nav_is_container(current)){
                expansion_add(&nav->expanded, nav_get_container_id(current));
            }

            current = next;
        }
        else if(seg->kind == DRJSON_PATH_INDEX){
            // Navigate by index (array element)
            if(current.kind != DRJSON_ARRAY){
                nav_set_message(nav, "Error: Cannot index non-array with [%lld] at segment %zu", (long long)seg->index, seg_idx);
                return CMD_ERROR;
            }

            DrJsonValue next = drjson_get_by_index(nav->jctx, current, seg->index);
            if(next.kind == DRJSON_ERROR){
                nav_set_message(nav, "Error: Index [%lld] out of bounds", (long long)seg->index);
                return CMD_ERROR;
            }

            // Expand the current container if needed
            if(nav_is_container(current)){
                expansion_add(&nav->expanded, nav_get_container_id(current));
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
            nav_set_message(nav, "Navigated to: %.*s", (int)args_len, args);
            return CMD_OK;
        }
    }

    // This shouldn't happen, but handle it just in case
    nav_set_message(nav, "Error: Found value but couldn't locate it in view");
    return CMD_ERROR;
}

// Tab completion for command mode
// Opens completion menu with all matches
// Returns: 0 = no completion, 1 = menu shown
static
int
nav_complete_command(JsonNav* nav){
    LineEditor* le = &nav->command_buffer;

    // Save original command on first tab
    if(!nav->in_completion_menu){
        nav->saved_command_len = le->length < sizeof(nav->saved_command) ? le->length : sizeof(nav->saved_command) - 1;
        memcpy(nav->saved_command, le->data, nav->saved_command_len);
        nav->saved_command[nav->saved_command_len] = '\0';
    }

    // Use saved command for determining context
    const char* source = nav->saved_command;
    size_t source_len = nav->saved_command_len;

    // Find where we are in the command
    // If cursor is at start or only whitespace before cursor, complete command names
    _Bool completing_command = 1;
    for(size_t i = 0; i < source_len; i++){
        if(source[i] == ' '){
            completing_command = 0;
            break;
        }
    }

    nav->completion_count = 0;

    if(completing_command){
        // Complete command names using command table
        // Find prefix
        size_t prefix_len = source_len;
        const char* prefix = source;

        // Find matching commands
        for(size_t i = 0; i < sizeof commands / sizeof commands[0] && nav->completion_count < 64; i++){
            size_t cmd_len = commands[i].name.length;
            if(cmd_len >= prefix_len && memcmp(commands[i].name.text, prefix, prefix_len) == 0){
                // Store the full command name
                size_t copy_len = cmd_len < 255 ? cmd_len : 255;
                memcpy(nav->completion_matches[nav->completion_count], commands[i].name.text, copy_len);
                nav->completion_matches[nav->completion_count][copy_len] = '\0';
                nav->completion_count++;
            }
        }

        if(nav->completion_count == 0){
            return 0; // No matches
        }

        // Show completion menu
        nav->in_completion_menu = 1;
        nav->completion_selected = 0;
        nav->completion_scroll = 0;

        // Update buffer with first completion
        const char* first = nav->completion_matches[0];
        size_t first_len = strlen(first);
        le->length = first_len < le->capacity ? first_len : le->capacity - 1;
        memcpy(le->data, first, le->length);
        le->data[le->length] = '\0';
        le->cursor_pos = le->length;

        return 1;
    }
    else {
        // Complete file paths
        // Use saved original command to extract the path prefix
        const char* source = nav->saved_command;
        size_t source_len = nav->saved_command_len;

        // Extract the path part after the command
        size_t path_start = 0;
        for(size_t i = 0; i < source_len; i++){
            if(source[i] == ' '){
                path_start = i + 1;
                break;
            }
        }

        // Skip leading spaces in path
        while(path_start < source_len && source[path_start] == ' '){
            path_start++;
        }

        if(path_start >= source_len){
            // No path typed yet, list current directory
            path_start = source_len;
        }

        // Extract the path prefix from the original saved command
        size_t path_len = source_len - path_start;
        char path_prefix[1024];
        if(path_len >= sizeof(path_prefix)) path_len = sizeof(path_prefix) - 1;
        memcpy(path_prefix, source + path_start, path_len);
        path_prefix[path_len] = '\0';

        // Split into directory and filename parts
        char dir_path[1024] = ".";
        char file_prefix[256] = "";
        size_t file_prefix_len = 0;

        // Find last '/' to split directory and filename
        for(size_t i = path_len; i > 0; i--){
            if(path_prefix[i-1] == '/' || path_prefix[i-1] == '\\'){
                // Copy directory part
                size_t dir_len = i;
                if(dir_len >= sizeof(dir_path)) dir_len = sizeof(dir_path) - 1;
                memcpy(dir_path, path_prefix, dir_len);
                dir_path[dir_len] = '\0';

                // Copy filename prefix
                file_prefix_len = path_len - i;
                if(file_prefix_len >= sizeof(file_prefix)) file_prefix_len = sizeof(file_prefix) - 1;
                memcpy(file_prefix, path_prefix + i, file_prefix_len);
                file_prefix[file_prefix_len] = '\0';
                break;
            }
        }

        // If no '/' found, use current directory and full path as prefix
        if(dir_path[0] != '.' || dir_path[1] != '\0'){
            // We found a directory part
        }
        else {
            // No directory separator found, use whole path as filename prefix
            file_prefix_len = path_len;
            if(file_prefix_len >= sizeof(file_prefix)) file_prefix_len = sizeof(file_prefix) - 1;
            memcpy(file_prefix, path_prefix, file_prefix_len);
            file_prefix[file_prefix_len] = '\0';
        }

        // List matching files
        #ifndef _WIN32
        DIR* d = opendir(dir_path);
        if(!d) return 0;

        // Collect matching entries into completion menu
        struct dirent* entry;
        while((entry = readdir(d)) != NULL && nav->completion_count < 64){
            // Skip . and ..
            if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){
                continue;
            }

            // Check if it matches the prefix
            size_t entry_len = strlen(entry->d_name);
            if(entry_len >= file_prefix_len &&
               memcmp(entry->d_name, file_prefix, file_prefix_len) == 0){

                // Build the full completed path for this match
                char completed[256];
                size_t completed_len = 0;

                // Copy command part from saved original
                for(size_t i = 0; i < path_start; i++){
                    if(completed_len < sizeof(completed) - 1){
                        completed[completed_len++] = source[i];
                    }
                }

                // Copy directory part if present
                if(dir_path[0] != '.' || dir_path[1] != '\0'){
                    size_t dir_len = strlen(dir_path);
                    for(size_t i = 0; i < dir_len && completed_len < sizeof(completed) - 1; i++){
                        completed[completed_len++] = dir_path[i];
                    }
                }

                // Copy matched filename
                for(size_t i = 0; i < entry_len && completed_len < sizeof(completed) - 1; i++){
                    completed[completed_len++] = entry->d_name[i];
                }

                completed[completed_len] = '\0';

                // Store in completion matches
                memcpy(nav->completion_matches[nav->completion_count], completed, completed_len + 1);
                nav->completion_count++;
            }
        }
        closedir(d);

        if(nav->completion_count == 0){
            return 0; // No matches
        }

        // Show completion menu
        nav->in_completion_menu = 1;
        nav->completion_selected = 0;
        nav->completion_scroll = 0;

        // Update buffer with first completion
        const char* first = nav->completion_matches[0];
        size_t first_len = strlen(first);
        le->length = first_len < le->capacity ? first_len : le->capacity - 1;
        memcpy(le->data, first, le->length);
        le->data[le->length] = '\0';
        le->cursor_pos = le->length;

        return 1;
        #else
        // Windows file completion - similar but with FindFirstFile
        // For brevity, just return no completion on Windows for now
        return 0;
        #endif
    }
}

// Accept the currently selected completion
static
void
nav_accept_completion(JsonNav* nav){
    if(!nav->in_completion_menu || nav->completion_count == 0){
        return;
    }

    LineEditor* le = &nav->command_buffer;
    const char* completion = nav->completion_matches[nav->completion_selected];
    size_t completion_len = strlen(completion);

    if(completion_len < le->capacity){
        memcpy(le->data, completion, completion_len);
        le->data[completion_len] = '\0';
        le->length = completion_len;
        le->cursor_pos = completion_len;
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
    if(!nav->in_completion_menu){
        return;
    }

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
    if(!nav->in_completion_menu || nav->completion_count == 0){
        return;
    }

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

    LineEditor* le = &nav->command_buffer;
    le->length = selected_len < le->capacity ? selected_len : le->capacity - 1;
    memcpy(le->data, selected, le->length);
    le->data[le->length] = '\0';
    le->cursor_pos = le->length;

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
    for(size_t i = 0; i < sizeof commands / sizeof commands[0]; i++){
        size_t name_len = commands[i].name.length;
        if(name_len == cmd_len && memcmp(commands[i].name.text, command, cmd_len) == 0){
            // Found matching command, call handler
            const char* args = arg_start ? arg_start : "";
            size_t args_len = arg_start ? arg_len : 0;
            strip_whitespace(&args, &args_len);
            return commands[i].handler(nav, args, args_len);
        }
    }

    // Unknown command
    nav_set_message(nav, "Unknown command: %.*s", (int)cmd_len, command);
    return CMD_ERROR;
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
            drjson_get_str_and_len(jctx, val, &str, &len);
            if(str){
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
                            } else {
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
                            } else {
                                goto budget_exceeded;
                            }
                            break;
                        }
                        case DRJSON_STRING: {
                            const char* str = NULL;
                            size_t slen = 0;
                            drjson_get_str_and_len(jctx, item, &str, &slen);
                            if(str && budget >= 4){ // At least room for ""
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
                            } else {
                                goto budget_exceeded;
                            }
                            break;
                        }
                        case DRJSON_ARRAY: {
                            // Show array preview
                            int64_t arr_len = drjson_len(jctx, item);

                            if(budget < 5){
                                goto budget_exceeded;
                            }

                            drt_putc(drt, '[');
                            budget--;

                            if(arr_len > 0){
                                drt_puts(drt, "...", 3);
                                budget -= 3;
                            }

                            drt_putc(drt, ']');
                            budget--;
                            shown++;
                            complex_shown++;
                            break;
                        }
                        case DRJSON_OBJECT: {
                            // Show object preview with first few keys
                            DrJsonValue obj_keys = drjson_object_keys(item);
                            int64_t obj_keys_len = drjson_len(jctx, obj_keys);

                            if(budget < 5){
                                goto budget_exceeded;
                            }

                            drt_putc(drt, '{');
                            budget--;

                            int obj_shown = 0;
                            for(int64_t ki = 0; ki < obj_keys_len && budget > 10; ki++){
                                DrJsonValue okey = drjson_get_by_index(jctx, obj_keys, ki);
                                const char* okey_str = NULL;
                                size_t okey_len = 0;
                                drjson_get_str_and_len(jctx, okey, &okey_str, &okey_len);

                                if(okey_str){
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
                    } else {
                        drt_puts(drt, ", ...]", 6);
                    }
                } else {
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

                for(int64_t i = 0; i < keys_len && budget > 0; i++){
                    DrJsonValue key = drjson_get_by_index(jctx, keys, i);
                    const char* key_str = NULL;
                    size_t key_len = 0;
                    drjson_get_str_and_len(jctx, key, &key_str, &key_len);

                    if(key_str){
                        int needed = (int)key_len + (i > 0 ? 2 : 0); // +2 for ", "
                        if(needed > budget && shown > 0)
                            break;

                        if(i > 0){
                            drt_puts(drt, ", ", 2);
                            budget -= 2;
                        }

                        size_t to_print = key_len;
                        if((int)to_print > budget)
                            to_print = budget;

                        drt_puts(drt, key_str, to_print);
                        budget -= (int)to_print;
                        shown++;
                    }
                }

                if(shown < keys_len){
                    int64_t remaining = keys_len - shown;
                    char buf[64];
                    int blen = snprintf(buf, sizeof buf, ", ... %lld more}", (long long)remaining);
                    if(blen > 0 && blen < (int)sizeof buf){
                        drt_puts(drt, buf, blen);
                    } else {
                        drt_puts(drt, ", ...}", 6);
                    }
                } else {
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
    SV(""),
    SV("Expand/Collapse:"),
    SV("  Space       Toggle expand/collapse"),
    SV("  N+Enter     Jump to index N (e.g., 0↵, 15↵)"),
    SV("  zo/zO       Expand recursively (open)"),
    SV("  zc/zC       Collapse recursively (close)"),
    SV("  zR          Expand all (open all folds)"),
    SV("  zM          Collapse all (close all folds)"),
    SV(""),
    SV("Search:"),
    SV("  /           Start search (case-insensitive)"),
    SV("  *           Start recursive search"),
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
    SV("  y/Y         Yank (copy) current value to clipboard"),
    SV("  :yank/:y    Same as y key"),
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
    SV("  ?           Toggle this help"),
    SV(""),
    SV("Help Navigation:"),
    SV("  n/→         Next page"),
    SV("  p/←         Previous page"),
    SV("  Any other   Close help"),
};

// Shared help rendering function
// Takes an array of StringView lines and renders them with pagination
static
void
nav_render_help(Drt* drt, int screenw, int screenh, int page, int* out_num_pages,
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

    // Find max width needed
    int max_width = 0;
    for(int i = 0; i < total_lines; i++){
        int len = (int)help_lines[i].length;
        if(len > max_width) max_width = len;
    }

    int box_height = num_lines + 3;  // +3 for top border, bottom border, and page indicator
    int start_y = (screenh - box_height) / 2;
    if(start_y < 1) start_y = 1;

    int start_x = (screenw - max_width - 4) / 2;
    if(start_x < 0) start_x = 0;

    // Draw box background
    for(int y = 0; y < box_height && start_y + y < screenh; y++){
        drt_move(drt, start_x, start_y + y);
        drt_push_state(drt);
        drt_bg_set_8bit_color(drt, 235);
        drt_set_8bit_color(drt, 15);
        for(int x = 0; x < max_width + 4; x++){
            drt_putc(drt, ' ');
        }
        drt_pop_state(drt);
    }

    // Draw help text for current page
    for(int i = 0; i < num_lines && start_y + i + 1 < screenh; i++){
        int line_idx = start_line + i;
        drt_move(drt, start_x + 2, start_y + i + 1);
        drt_push_state(drt);
        drt_bg_set_8bit_color(drt, 235);

        // Highlight headers
        if(help_lines[line_idx].length && (help_lines[line_idx].text[help_lines[line_idx].length-1] == ':' || help_lines[line_idx].text[0] == ':')){
            drt_set_8bit_color(drt, 11); // bright yellow
            drt_set_style(drt, DRT_STYLE_BOLD);
        } else {
            drt_set_8bit_color(drt, 15);
        }

        drt_puts_utf8(drt, help_lines[line_idx].text, help_lines[line_idx].length);
        drt_pop_state(drt);
    }

    // Draw page indicator at bottom
    if(num_pages > 1){
        drt_move(drt, start_x + 2, start_y + num_lines + 1);
        drt_push_state(drt);
        drt_bg_set_8bit_color(drt, 235);
        drt_set_8bit_color(drt, 8);  // gray
        drt_printf(drt, "Page %d/%d", page + 1, num_pages);
        drt_pop_state(drt);
    }
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
        helps[j++] = commands[i].help_name;
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
            int len = snprintf(index_buf, sizeof(index_buf), "[%lld]", (long long)components[i].index);
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
            drjson_get_str_and_len(nav->jctx, key_val, &key_str, &key_len);
            if(key_str && written + key_len + 1 < buf_size){
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
    drt_set_8bit_color(drt, 220); // yellow/gold like array indices
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
            num_len = snprintf(buf, sizeof(buf), "%g", item.number);
        }
        else if(item.kind == DRJSON_INTEGER){
            num_len = snprintf(buf, sizeof(buf), "%lld", (unsigned long long)item.integer);
        }
        else if(item.kind == DRJSON_UINTEGER){
            num_len = snprintf(buf, sizeof(buf), "%lld", (unsigned long long)item.uinteger);
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
    if(nav->search_mode == SEARCH_RECURSIVE){
        drt_puts(drt, " Recursive Search: ", 19);
        int start_x = 19;
        le_render(drt, &nav->search_buffer);
        cursor_x = start_x + (int)nav->search_buffer.cursor_pos;
        cursor_y = 0;
        show_cursor = 1;
    }
    else if(nav->search_mode == SEARCH_NORMAL){
        drt_puts(drt, " Search: ", 9);
        int start_x = 9;
        le_render(drt, &nav->search_buffer);
        cursor_x = start_x + (int)nav->search_buffer.cursor_pos;
        cursor_y = 0;
        show_cursor = 1;
    }
    else if(nav->search_match_count > 0){
        drt_printf(drt, " DrJson TUI — %zu items — Match %zu/%zu ",
                   nav->item_count, nav->current_match_idx + 1, nav->search_match_count);
    }
    else {
        drt_printf(drt, " DrJson TUI — %zu items ", nav->item_count);
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
                    drt_set_8bit_color(drt, 220); // yellow/gold
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
                        drjson_get_atom_str_and_length(nav->jctx, nav->insert_object_key, &key_str, &key_len);

                        drt_push_state(drt);
                        drt_set_8bit_color(drt, 45); // cyan
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
                    drjson_get_str_and_len(nav->jctx, key_val, &key_str, &key_len);
                    if(key_str){
                        drt_push_state(drt);
                        drt_set_8bit_color(drt, 45); // cyan
                        drt_puts(drt, key_str, key_len);
                        drt_pop_state(drt);
                        drt_puts(drt, ": ", 2);
                    }
                }
            }
            else if(item->index >= 0){
                drt_push_state(drt);
                drt_set_8bit_color(drt, 220); // yellow/gold
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
                drt_set_8bit_color(drt, 220); // yellow/gold
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
                    drjson_get_atom_str_and_length(nav->jctx, nav->insert_object_key, &key_str, &key_len);

                    drt_push_state(drt);
                    drt_set_8bit_color(drt, 45); // cyan
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

            int y = screenh - 1 - visible_items + i;
            if(y < 1) break; // Don't overwrite status line

            drt_move(drt, 0, y);
            drt_push_state(drt);

            // Highlight selected item
            if(match_idx == nav->completion_selected){
                drt_bg_set_8bit_color(drt, 240); // lighter gray
                drt_set_8bit_color(drt, 15);     // white text
            }
            else {
                drt_bg_set_8bit_color(drt, 235); // dark gray
                drt_set_8bit_color(drt, 250);    // light gray text
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
    drt_bg_set_8bit_color(drt, 235); // dark gray background

    if(nav->command_mode){
        // Show command prompt
        drt_putc(drt, ':');
        int start_x = 1;
        le_render(drt, &nav->command_buffer);
        cursor_x = start_x + (int)nav->command_buffer.cursor_pos;
        cursor_y = screenh - 1;
        show_cursor = 1;
    }
    else if(nav->has_message){
        // Show message to user
        drt_putc(drt, ' ');
        drt_set_8bit_color(drt, 226); // bright yellow text for visibility
        drt_puts(drt, nav->message, strlen(nav->message));
        drt_putc(drt, ' ');
    }
    else if(nav->item_count > 0){
        // Show breadcrumb (JSON path)
        char path_buf[512];
        size_t path_len = nav_build_json_path(nav, path_buf, sizeof(path_buf));
        if(path_len > 0){
            drt_putc(drt, ' ');
            drt_set_8bit_color(drt, 250); // light gray text
            drt_puts(drt, path_buf, path_len);
            drt_putc(drt, ' ');
        }
    }
    drt_clear_to_end_of_row(drt);
    drt_pop_state(drt);

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
        if(len && (*txt != '"' && *txt != '\\') && new_value.kind == DRJSON_STRING){
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
main(int argc, const char* const* argv){
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
    unsigned flags = DRJSON_PARSE_FLAG_NONE;
    if(braceless) flags |= DRJSON_PARSE_FLAG_BRACELESS_OBJECT;
    if(globals.intern) flags |= DRJSON_PARSE_FLAG_INTERN_OBJECTS;
    flags |= DRJSON_PARSE_FLAG_NO_COPY_STRINGS;
    DrJsonValue document = drjson_parse(&ctx, flags);
    if(document.kind == DRJSON_ERROR){
        size_t l, c;
        drjson_get_line_column(&ctx, &l, &c);
        drjson_print_error_fp(stderr,  jsonpath.text, jsonpath.length, l, c, document);
        return 1;
    }
    // Initialize navigation
    JsonNav nav;
    nav_init(&nav, jctx, document);

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
        int r = get_input(&globals.TS, &globals.needs_rescale, &c, &cx, &cy, &magnitude);
        if(r == -1) goto finally;
        if(!r) continue;

        // If help is showing, handle help navigation
        if(nav.show_help){
            int num_pages = 0;
            nav_render_help(&globals.drt, globals.screenw, globals.screenh, nav.help_page, &num_pages, nav.help_lines, nav.help_lines_count);

            // Clamp help_page in case window was resized
            if(nav.help_page >= num_pages){
                nav.help_page = num_pages - 1;
            }
            if(nav.help_page < 0){
                nav.help_page = 0;
            }

            if(c == 'n' || c == RIGHT){
                // Next page
                if(nav.help_page < num_pages - 1){
                    nav.help_page++;
                }
                continue;
            }
            else if(c == 'p' || c == LEFT){
                // Previous page
                if(nav.help_page > 0){
                    nav.help_page--;
                }
                continue;
            }
            else {
                // Any other key closes help
                nav.show_help = 0;
                nav.help_page = 0;
                le_clear(&count_buffer);
                continue;
            }
        }

        // Handle search input mode
        if(nav.search_mode != SEARCH_INACTIVE){
            if(c == ESC || c == CTRL_C){
                // Cancel search
                nav.search_mode = SEARCH_INACTIVE;
                le_clear(&nav.search_buffer);
                continue;
            }
            else if(c == ENTER || c == CTRL_J){
                // Add to history before searching
                le_history_add(&nav.search_history, nav.search_buffer.data, nav.search_buffer.length);
                le_history_reset(&nav.search_buffer);

                // Perform search (recursive if in recursive mode)
                _Bool recursive = (nav.search_mode == SEARCH_RECURSIVE);
                nav.search_mode = SEARCH_INACTIVE;
                if(recursive)
                    nav_search_recursive(&nav);
                else
                    nav_search(&nav);
                nav_center_cursor(&nav, globals.screenh);
                continue;
            }
            else if(c == UP || c == CTRL_P){
                // Navigate to previous history entry
                le_history_prev(&nav.search_buffer);
                continue;
            }
            else if(c == DOWN || c == CTRL_N){
                // Navigate to next history entry
                le_history_next(&nav.search_buffer);
                continue;
            }
            else if(le_handle_key(&nav.search_buffer, c, 1)){
                // Common line editing keys handled
                continue;
            }
            else if(c >= 32 && c < 127){
                // Add printable character
                le_history_reset(&nav.search_buffer);
                le_append_char(&nav.search_buffer, (char)c);
                continue;
            }
            // Ignore other keys in search mode
            continue;
        }

        // Handle command mode
        if(nav.command_mode){
            // Handle completion menu navigation
            if(nav.in_completion_menu){
                if(c == UP || c == CTRL_P){
                    // Move selection up
                    nav_completion_move(&nav, -1);
                    continue;
                }
                else if(c == DOWN || c == CTRL_N){
                    // Move selection down
                    nav_completion_move(&nav, 1);
                    continue;
                }
                else if(c == ENTER || c == CTRL_J){
                    // Accept completion (don't execute command)
                    nav_accept_completion(&nav);
                    continue;
                }
                else if(c == ESC || c == CTRL_C){
                    // Cancel completion menu
                    nav_cancel_completion(&nav);
                    continue;
                }
                else if(c == TAB){
                    // Tab again cycles through in menu (already showing)
                    nav_completion_move(&nav, 1);
                    continue;
                }
                else {
                    // Any other key exits completion menu and continues with normal handling
                    nav_exit_completion(&nav);
                    // Fall through to normal command mode handling
                }
            }

            // Normal command mode handling
            if(c == ESC || c == CTRL_C){
                // Cancel command
                nav.command_mode = 0;
                nav.tab_count = 0;
                le_clear(&nav.command_buffer);
                continue;
            }
            else if(c == ENTER || c == CTRL_J){
                // Execute command
                int cmd_result = nav_execute_command(&nav, nav.command_buffer.data, nav.command_buffer.length);
                nav.command_mode = 0;
                nav.tab_count = 0;
                le_clear(&nav.command_buffer);
                if(cmd_result == 1){
                    // Quit requested
                    goto finally;
                }
                continue;
            }
            else if(c == TAB){
                // Tab completion
                nav_complete_command(&nav);
                continue;
            }
            else if(le_handle_key(&nav.command_buffer, c, 0)){
                // Common line editing keys handled
                nav.tab_count = 0; // Reset tab completion
                continue;
            }
            else if(c >= 32 && c < 127){
                // Add printable character
                nav.tab_count = 0; // Reset tab completion
                le_append_char(&nav.command_buffer, (char)c);
                continue;
            }
            // Ignore other keys in command mode
            continue;
        }

        // Handle edit mode
        if(nav.edit_mode){
            if(c == ESC || c == CTRL_C){
                // Cancel edit
                nav.edit_mode = 0;
                nav.edit_key_mode = 0;
                nav.insert_mode = INSERT_NONE;
                le_clear(&nav.edit_buffer);
                continue;
            }
            else if(c == ENTER || c == CTRL_J){
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
                            nav_set_message(&nav, "Error: Key already exists or cannot be replaced");
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
                    nav_set_message(&nav, "Error: Invalid value syntax");
                    goto exit_edit_mode;
                }
                if(nav.insert_mode == INSERT_ARRAY){
                    // Insert into the array
                    NavItem* array_item = &nav.items[nav.insert_container_pos];
                    DrJsonValue array = array_item->value;
                    if(array.kind != DRJSON_ARRAY){
                        nav_set_message(&nav, "Error: Not an array");
                        goto exit_edit_mode;
                    }
                    if(nav.insert_index == SIZE_MAX)
                        err = drjson_array_push_item(nav.jctx, array, new_value); // Append to end
                    else
                        err = drjson_array_insert_item(nav.jctx, array, nav.insert_index, new_value); // Insert at specific index
                    if(err){
                        nav_set_message(&nav, "Error: Could not insert into array");
                        goto exit_edit_mode;
                    }
                    nav_set_message(&nav, "Item inserted");
                    nav.needs_rebuild = 1;
                    nav_rebuild(&nav);
                    goto exit_edit_mode;
                }
                if(nav.insert_mode == INSERT_OBJECT){
                    // Insert into the object at the specified index
                    NavItem* object_item = &nav.items[nav.insert_container_pos];
                    DrJsonValue object = object_item->value;
                    if(object.kind != DRJSON_OBJECT){
                        nav_set_message(&nav, "Error: Not an object");
                        goto exit_edit_mode;
                    }
                    size_t insert_index = nav.insert_index;
                    if(insert_index == SIZE_MAX) insert_index = drjson_len(nav.jctx, object);
                    int err = drjson_object_insert_item_at_index(nav.jctx, object, nav.insert_object_key, new_value, insert_index);
                    if(err){
                        nav_set_message(&nav, "Error: Could not insert into object (key may already exist)");
                        goto exit_edit_mode;
                    }
                    nav_set_message(&nav, "Item inserted");
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
                    nav_set_message(&nav, "Root value updated");
                    goto exit_edit_mode;
                }
                NavItem* parent = &nav.items[parent_idx];
                if(parent->value.kind == DRJSON_OBJECT){
                    NavItem* item = &nav.items[nav.cursor_pos];
                    err = drjson_object_set_item_atom(nav.jctx, parent->value, item->key, new_value);
                    if(err){
                        nav_set_message(&nav, "Error: Could not update value");
                        goto exit_edit_mode;
                    }
                    nav_set_message(&nav, "Value updated");
                    nav.needs_rebuild = 1;
                    nav_rebuild(&nav);
                    goto exit_edit_mode;
                }
                if(parent->value.kind == DRJSON_ARRAY){
                    NavItem* item = &nav.items[nav.cursor_pos];
                    if(item->is_flat_view){
                        nav_set_message(&nav, "Error: Array element editing of flat views not yet supported");
                        goto exit_edit_mode;
                    }
                    err = drjson_array_set_by_index(nav.jctx, parent->value, item->index, new_value);
                    if(err){
                        nav_set_message(&nav, "Error: Could not update value");
                        goto exit_edit_mode;
                    }
                    nav_set_message(&nav, "Value updated");
                    nav.needs_rebuild = 1;
                    nav_rebuild(&nav);
                }

                exit_edit_mode:;
                nav.edit_mode = 0;
                nav.edit_key_mode = 0;
                nav.insert_mode = INSERT_NONE;
                le_clear(&nav.edit_buffer);
                continue;
            }
            else if(le_handle_key(&nav.edit_buffer, c, 0)){
                // Common line editing keys handled
                continue;
            }
            else if(c >= 32 && c < 127){
                // Add printable character
                le_append_char(&nav.edit_buffer, (char)c);
                continue;
            }
            // Ignore other keys in edit mode
            continue;
        }

        // Handle digit input to build count
        if(c >= '0' && c <= '9'){
            le_append_char(&count_buffer, (char)c);
            continue;
        }

        // Handle text editing for count buffer (only when count buffer has content)
        if(count_buffer.length > 0 && le_handle_key(&count_buffer, c, 0)){
            continue;
        }

        // Handle 'z' prefix for vim-like commands
        if(c == 'z'){
            int c2 = 0, cx2 = 0, cy2 = 0, magnitude2 = 0;
            int r2 = get_input(&globals.TS, &globals.needs_rescale, &c2, &cx2, &cy2, &magnitude2);
            if(r2 == -1) goto finally;
            if(r2){
                if(c2 == 'z'){
                    // zz - center cursor
                    nav_center_cursor(&nav, globals.screenh);
                    continue;
                }
                else if(c2 == 't'){
                    // zt - cursor to top of screen
                    nav.scroll_offset = nav.cursor_pos;
                    continue;
                }
                else if(c2 == 'b'){
                    // zb - cursor to bottom of screen
                    int visible_rows = globals.screenh - 2;  // Account for status and breadcrumb
                    if(visible_rows < 1) visible_rows = 1;
                    if(nav.cursor_pos >= (size_t)(visible_rows - 1)){
                        nav.scroll_offset = nav.cursor_pos - (size_t)(visible_rows - 1);
                    } else {
                        nav.scroll_offset = 0;
                    }
                    continue;
                }
                else if(c2 == 'c' || c2 == 'C'){
                    // zc/zC - collapse current item recursively
                    nav_collapse_recursive(&nav);
                    continue;
                }
                else if(c2 == 'o' || c2 == 'O'){
                    // zo/zO - expand recursively (open)
                    nav_expand_recursive(&nav);
                    nav_ensure_cursor_visible(&nav, globals.screenh);
                    continue;
                }
                else if(c2 == 'M'){
                    // zM - collapse all (close all folds)
                    nav_collapse_all(&nav);
                    continue;
                }
                else if(c2 == 'R'){
                    // zR - expand all (open all folds)
                    nav_expand_all(&nav);
                    continue;
                }
            }
            // If not a recognized z command, ignore
            le_clear(&count_buffer);
            continue;
        }

        // Handle 'c' prefix for change/edit commands
        if(c == 'c'){
            int c2 = 0, cx2 = 0, cy2 = 0, magnitude2 = 0;
            int r2 = get_input(&globals.TS, &globals.needs_rescale, &c2, &cx2, &cy2, &magnitude2);
            if(r2 == -1) goto finally;
            if(r2){
                if(c2 == 'k'){
                    // ck - edit key (empty buffer)
                    if(nav.item_count > 0){
                        NavItem* item = &nav.items[nav.cursor_pos];
                        if(item->key.bits != 0 && item->depth > 0){
                            nav.edit_mode = 1;
                            nav.edit_key_mode = 1;
                            le_clear(&nav.edit_buffer);
                        }
                        else {
                            nav_set_message(&nav, "Can only rename keys on object members");
                        }
                    }
                    continue;
                }
                else if(c2 == 'v'){
                    // cv - edit value (empty buffer)
                    if(nav.item_count > 0){
                        nav.edit_mode = 1;
                        nav.edit_key_mode = 0;
                        le_clear(&nav.edit_buffer);
                    }
                    continue;
                }
            }
            // If not a recognized c command, ignore
            le_clear(&count_buffer);
            continue;
        }

        // Handle 'd' prefix for delete commands
        if(c == 'd'){
            int c2 = 0, cx2 = 0, cy2 = 0, magnitude2 = 0;
            int r2 = get_input(&globals.TS, &globals.needs_rescale, &c2, &cx2, &cy2, &magnitude2);
            if(r2 == -1) goto finally;
            if(r2){
                if(c2 == 'd'){ // dd - delete current item
                    size_t parent_idx = nav_find_parent(&nav, nav.cursor_pos);
                    if(parent_idx == SIZE_MAX){
                        nav_set_message(&nav, "Cannot delete root value");
                        continue;
                    }
                    NavItem* parent = &nav.items[parent_idx];
                    NavItem* item = &nav.items[nav.cursor_pos];
                    if(parent->value.kind == DRJSON_OBJECT){
                        int err = drjson_object_delete_item_atom(nav.jctx, parent->value, item->key);
                        if(err){
                            nav_set_message(&nav, "Error: Could not delete item");
                            continue;
                        }
                        nav_set_message(&nav, "Item deleted");
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
                            nav_set_message(&nav, "Error: Could not delete item");
                            continue;
                        }
                        nav_set_message(&nav, "Item deleted");
                        nav.needs_rebuild = 1;
                        nav_rebuild(&nav);
                        // Move cursor up if we deleted the last item
                        if(nav.cursor_pos >= nav.item_count && nav.cursor_pos > 0)
                            nav.cursor_pos--;
                    }
                    continue;
                }
            }
            // If not a recognized d command, ignore
            le_clear(&count_buffer);
            continue;
        }

        // Clear any displayed message on user action
        if(nav.has_message){
            nav_clear_message(&nav);
        }

        // Handle input
        switch(c){
            case 'q':
            case 'Q':
                goto finally;

            case CTRL_Z:
                #ifdef _WIN32
                #else
                end_tui();
                raise(SIGTSTP);
                begin_tui();
                globals.needs_redisplay = 1;
                #endif
                break;

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
                    drjson_print_value_mem(nav.jctx, temp_buf, sizeof(temp_buf), item->value, -1, 0, &printed);

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
                        drjson_get_atom_str_and_length(nav.jctx, item->key, &key_str, &key_len);

                        // Copy key as-is (no quotes)
                        for(size_t i = 0; i < key_len && nav.edit_buffer.length < nav.edit_buffer.capacity - 1; i++){
                            le_append_char(&nav.edit_buffer, key_str[i]);
                        }
                    }
                    else {
                        nav_set_message(&nav, "Can only rename keys on object members");
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
                nav.show_help = 1;
                nav.help_lines = HELP_LINES;
                nav.help_lines_count = sizeof HELP_LINES / sizeof HELP_LINES[0];
                nav.help_page = 0;  // Start at first page
                break;

            case '/':
                // Enter search mode
                nav.search_mode = SEARCH_NORMAL;
                le_clear(&nav.search_buffer);
                break;

            case ';':
            case ':':
                // Enter command mode
                nav.command_mode = 1;
                le_clear(&nav.command_buffer);
                break;

            case '*':
                // Enter recursive search mode (same as / but will search recursively)
                nav.search_mode = SEARCH_RECURSIVE;
                le_clear(&nav.search_buffer);
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

            case 'y':
            case 'Y':
                // Yank (copy) current value to clipboard
                cmd_yank(&nav, NULL, 0);
                break;

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

