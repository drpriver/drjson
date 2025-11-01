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
    int64_t array_index;      // Index if this is an array element (-1 if not)
    _Bool has_key;            // Whether this item has a key (object member vs array element)
    _Bool is_flat_view;       // If true, this is a synthetic flat array view child
    int flat_row_index;       // For flat view items, which row (0, 1, 2, ...)
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
    _Bool show_help;          // Show help overlay

    // Search state
    LineEditor search_buffer;       // Current search query
    LineEditorHistory search_history; // Search history
    enum SearchMode search_mode;    // Current search mode
    size_t* search_matches;         // Array of indices of items that match
    size_t search_match_count;
    size_t search_match_capacity;
    size_t current_match_idx;       // Index into search_matches array
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

static void nav_rebuild_recursive(JsonNav* nav, DrJsonValue val, int depth,
                                   DrJsonAtom key, _Bool has_key, int64_t array_index);

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
    nav_rebuild_recursive(nav, nav->root, 0, (DrJsonAtom){0}, 0, -1);
    nav->needs_rebuild = 0;

    // Clamp cursor to valid range
    if(nav->item_count == 0)
        nav->cursor_pos = 0;
    else if(nav->cursor_pos >= nav->item_count)
        nav->cursor_pos = nav->item_count - 1;
}

static
void
nav_rebuild_recursive(JsonNav* nav, DrJsonValue val, int depth,
                      DrJsonAtom key, _Bool has_key, int64_t array_index){
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
        .array_index = array_index,
        .has_key = has_key,
        .is_flat_view = 0,
        .flat_row_index = 0
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
                    .array_index = -1,
                    .has_key = 0,
                    .is_flat_view = 1,
                    .flat_row_index = row
                };
                nav_append_item(nav, flat_item);
            }
        }
        else {
            int64_t len = drjson_len(nav->jctx, val);

            if(val.kind == DRJSON_ARRAY){
                for(int64_t i = 0; i < len; i++){
                    DrJsonValue child = drjson_get_by_index(nav->jctx, val, i);
                    nav_rebuild_recursive(nav, child, depth + 1, (DrJsonAtom){0}, 0, i);
                }
            }
            else { // DRJSON_OBJECT
                DrJsonValue items = drjson_object_items(val);
                int64_t items_len = drjson_len(nav->jctx, items);
                for(int64_t i = 0; i < items_len; i += 2){
                    DrJsonValue k = drjson_get_by_index(nav->jctx, items, i);
                    DrJsonValue v = drjson_get_by_index(nav->jctx, items, i + 1);
                    nav_rebuild_recursive(nav, v, depth + 1, k.atom, 1, -1);
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
        .items = NULL,
        .item_count = 0,
        .item_capacity = 0,
        .expanded = {0},
        .cursor_pos = 0,
        .scroll_offset = 0,
        .needs_rebuild = 1,
        .show_help = 0,
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

    size_t id = nav_get_container_id(item->value);
    expansion_toggle(&nav->expanded, id);
    nav->needs_rebuild = 1;
    nav_rebuild(nav);
}

// Helper to recursively expand a value and all its descendants
static void
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
static void
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

    nav_collapse_recursive_helper(nav, item->value);

    nav->needs_rebuild = 1;
    nav_rebuild(nav);
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
                if(nav_is_container(parent->value) && nav_is_expanded(nav, parent->value)){
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
            if(nav->items[i].is_flat_view && nav->items[i].flat_row_index == target_row){
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
                if(nav->items[i].is_flat_view && nav->items[i].flat_row_index == target_row){
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
                    if(nav->items[i].is_flat_view && nav->items[i].flat_row_index == target_row){
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

    // Account for status line taking up one row
    int visible_rows = viewport_height - 1;
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
    if(item->has_key){
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
nav_value_matches_query(JsonNav* nav, DrJsonValue val, DrJsonAtom key, _Bool has_key,
                         const char* query, size_t query_len){
    // Check key if present
    if(has_key){
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
nav_search_recursive_helper(JsonNav* nav, DrJsonValue val, DrJsonAtom key, _Bool has_key,
                              const char* query, size_t query_len){
    _Bool found_match = 0;

    // Check if this value matches
    if(nav_value_matches_query(nav, val, key, has_key, query, query_len)){
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
                if(nav_search_recursive_helper(nav, child, (DrJsonAtom){0}, 0, query, query_len)){
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
                if(nav_search_recursive_helper(nav, v, k.atom, 1, query, query_len)){
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
    nav_search_recursive_helper(nav, nav->root, (DrJsonAtom){0}, 0,
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

    // Account for status line taking up one row
    int visible_rows = viewport_height - 1;
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

static
void
nav_render_help(Drt* drt, int screenw, int screenh){
    StringView help_lines[] = {
        SV("DrJson TUI - Keyboard Commands"),
        SV(""),
        SV("Navigation:"),
        SV("  j/↓         Move cursor down"),
        SV("  k/↑         Move cursor up"),
        SV("  h/←         Jump to parent (and collapse)"),
        SV("  H           Jump to parent (keep expanded)"),
        SV("  l/→         Enter container (expand if needed)"),
        SV("  ]           Next sibling (skip children)"),
        SV("  [           Previous sibling"),
        SV("  -           Jump to parent (no collapse)"),
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
        SV("Expand/Collapse:"),
        SV("  Enter/Space Toggle expand/collapse"),
        SV("  N+Enter     Jump to index N (e.g., 0↵, 15↵)"),
        SV("  e           Expand recursively"),
        SV("  c           Collapse recursively"),
        SV(""),
        SV("Other:"),
        SV("  q/Ctrl-C    Quit"),
        SV("  ?           Toggle this help"),
        SV(""),
        SV("Press any key to close help..."),
    };

    int num_lines = sizeof help_lines / sizeof help_lines[0];
    int start_y = (screenh - num_lines) / 2;
    if(start_y < 1) start_y = 1;

    int max_width = 0;
    for(int i = 0; i < num_lines; i++){
        int len = (int)help_lines[i].length;
        if(len > max_width) max_width = len;
    }

    int start_x = (screenw - max_width - 4) / 2;
    if(start_x < 0) start_x = 0;

    // Draw box background
    for(int y = 0; y < num_lines + 2 && start_y + y < screenh; y++){
        drt_move(drt, start_x, start_y + y);
        drt_push_state(drt);
        drt_bg_set_8bit_color(drt, 235);
        drt_set_8bit_color(drt, 15);
        for(int x = 0; x < max_width + 4; x++){
            drt_putc(drt, ' ');
        }
        drt_pop_state(drt);
    }

    // Draw help text
    for(int i = 0; i < num_lines && start_y + i + 1 < screenh; i++){
        drt_move(drt, start_x + 2, start_y + i + 1);
        drt_push_state(drt);
        drt_bg_set_8bit_color(drt, 235);

        // Highlight headers
        if(help_lines[i].length && help_lines[i].text[help_lines[i].length-1] == ':'){
            drt_set_8bit_color(drt, 11); // bright yellow
            drt_set_style(drt, DRT_STYLE_BOLD);
        } else {
            drt_set_8bit_color(drt, 15);
        }

        drt_puts_utf8(drt, help_lines[i].text, help_lines[i].length);
        drt_pop_state(drt);
    }
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
    size_t end_idx = nav->scroll_offset + (size_t)screenh - 1; // -1 for status line
    if(end_idx > nav->item_count)
        end_idx = nav->item_count;

    for(size_t i = nav->scroll_offset; i < end_idx; i++){
        NavItem* item = &nav->items[i];
        int y = 1 + (int)(i - nav->scroll_offset);

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
            if(item->has_key){
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
            else if(item->array_index >= 0){
                drt_push_state(drt);
                drt_set_8bit_color(drt, 220); // yellow/gold
                drt_printf(drt, "%lld", (long long)item->array_index);
                drt_pop_state(drt);
                drt_puts(drt, ": ", 2);
            }
        }

        // Value summary
        int cx, cy;
        drt_cursor(drt, &cx, &cy);
        int remaining = screenw - cx;
        if(remaining < 10) remaining = 10;

        // Use flat rendering for flat view items
        if(item->is_flat_view){
            nav_render_flat_array_row(drt, nav->jctx, item->value, item->flat_row_index);
        }
        else {
            nav_render_value_summary(drt, nav->jctx, item->value, remaining);
        }

        // Clear rest of line
        drt_clear_to_end_of_row(drt);

        if(i == nav->cursor_pos){
            drt_pop_state(drt);
        }
    }

    // Clear remaining lines
    for(int y = 1 + (int)(end_idx - nav->scroll_offset); y < screenh; y++){
        drt_move(drt, 0, y);
        drt_clear_to_end_of_row(drt);
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
                for(i = 2; i < (int)sizeof sequence; i++){
                    if(read_one_nb(sequence+i) == -1) return -1;
                    if(sequence[i] == 0) return 0; // unexpected end of escape sequence
                    if(sequence[i] == ';') break;
                    if(sequence[i] < '0' || sequence[i] > '9') return 0; // out of range, should be decimal
                    mb *= 10;
                    mb += sequence[i] - '0';
                }
                int x = 0;
                for(; i < (int)sizeof sequence; i++){
                    if(read_one_nb(sequence+i) == -1) return -1;
                    if(sequence[i] == 0) return 0; // unexpected end of escape sequence
                    if(sequence[i] == ';') break;
                    if(sequence[i] < '0' || sequence[i] > '9') return 0; // out of range, should be decimal
                    x *= 10;
                    x += sequence[i] - '0';
                }
                int y = 0;
                for(; i < (int)sizeof sequence; i++){
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
    _Bool intern = 0;
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
            .dest = ARGDEST(&intern),
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
    globals.STDIN = GetStdHandle(STD_INPUT_HANDLE);
    globals.STDOUT = GetStdHandle(STD_OUTPUT_HANDLE);
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

    // Initialize navigation
    JsonNav nav;
    nav_init(&nav, jctx, this);

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
            nav_render_help(&globals.drt, globals.screenw, globals.screenh);
        }

        drt_paint(&globals.drt);

        int c = 0, cx = 0, cy = 0, magnitude = 0;
        int r = get_input(&c, &cx, &cy, &magnitude);
        if(r == -1) goto finally;
        if(!r) continue;

        // If help is showing, any key closes it
        if(nav.show_help){
            nav.show_help = 0;
            le_clear(&count_buffer);
            continue;
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
                if(recursive){
                    nav_search_recursive(&nav);
                }
                else {
                    nav_search(&nav);
                }
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
            int r2 = get_input(&c2, &cx2, &cy2, &magnitude2);
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
                    int visible_rows = globals.screenh - 1;
                    if(visible_rows < 1) visible_rows = 1;
                    if(nav.cursor_pos >= (size_t)(visible_rows - 1)){
                        nav.scroll_offset = nav.cursor_pos - (size_t)(visible_rows - 1);
                    } else {
                        nav.scroll_offset = 0;
                    }
                    continue;
                }
            }
            // If not a recognized z command, ignore
            le_clear(&count_buffer);
            continue;
        }

        // Handle input
        switch(c){
            case CTRL_C:
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

            case 'e':
            case 'E':
                // Expand recursively
                nav_expand_recursive(&nav);
                nav_ensure_cursor_visible(&nav, globals.screenh);
                break;

            case 'c':
            case 'C':
                // Collapse current item recursively
                nav_collapse_recursive(&nav);
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
                // Toggle help
                nav.show_help = !nav.show_help;
                break;

            case '/':
                // Enter search mode
                nav.search_mode = SEARCH_NORMAL;
                le_clear(&nav.search_buffer);
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


