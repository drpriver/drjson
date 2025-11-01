#ifndef LINEEDIT_H
#define LINEEDIT_H
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

// History for line editors
typedef struct LineEditorHistory LineEditorHistory;
struct LineEditorHistory {
    char** entries;          // Array of history entries
    size_t count;            // Number of entries
    size_t capacity;         // Capacity of array
    size_t browse_index;     // Current position when browsing (count = not browsing)
    char saved_current[256]; // Save current unsaved text when starting to browse
    size_t saved_length;     // Length of saved text
    _Bool browsing;          // True if currently browsing history
};

typedef struct LineEditor LineEditor;
struct LineEditor {
    char* data;
    size_t length;
    size_t capacity;
    size_t cursor_pos;       // Cursor position (0 to length)
    LineEditorHistory* history; // Optional history
};

static inline
void
le_history_init(LineEditorHistory* hist){
    hist->entries = NULL;
    hist->count = 0;
    hist->capacity = 0;
    hist->browse_index = 0;
    hist->saved_length = 0;
    hist->browsing = 0;
}

static inline
void
le_history_free(LineEditorHistory* hist){
    for(size_t i = 0; i < hist->count; i++){
        free(hist->entries[i]);
    }
    free(hist->entries);
    hist->entries = NULL;
    hist->count = 0;
    hist->capacity = 0;
}

static inline
void
le_history_add(LineEditorHistory* hist, const char* text, size_t length){
    if(length == 0) return; // Don't add empty entries

    // Don't add if it matches the most recent entry
    if(hist->count > 0){
        size_t last_len = strlen(hist->entries[hist->count - 1]);
        if(last_len == length && memcmp(hist->entries[hist->count - 1], text, length) == 0){
            return;
        }
    }

    // Expand array if needed
    if(hist->count >= hist->capacity){
        size_t new_cap = hist->capacity == 0 ? 16 : hist->capacity * 2;
        char** new_entries = realloc(hist->entries, new_cap * sizeof(char*));
        if(!new_entries) return;
        hist->entries = new_entries;
        hist->capacity = new_cap;
    }

    // Add entry
    hist->entries[hist->count] = malloc(length + 1);
    if(!hist->entries[hist->count]) return;
    memcpy(hist->entries[hist->count], text, length);
    hist->entries[hist->count][length] = '\0';
    hist->count++;
}

static inline
void
le_init(LineEditor* le, size_t capacity){
    le->data = malloc(capacity);
    le->data[0] = '\0';
    le->length = 0;
    le->capacity = capacity;
    le->cursor_pos = 0;
    le->history = NULL;
}

static inline
void
le_free(LineEditor* le){
    free(le->data);
    le->data = NULL;
    le->length = 0;
    le->capacity = 0;
    le->cursor_pos = 0;
}

static inline
void
le_clear(LineEditor* le){
    le->length = 0;
    le->cursor_pos = 0;
    if(le->data) le->data[0] = '\0';
}

static inline
void
le_append_char(LineEditor* le, char c){
    if(le->length + 1 < le->capacity){
        // Insert character at cursor position
        if(le->cursor_pos < le->length){
            // Shift characters to the right
            memmove(le->data + le->cursor_pos + 1,
                    le->data + le->cursor_pos,
                    le->length - le->cursor_pos);
        }
        le->data[le->cursor_pos] = c;
        le->length++;
        le->cursor_pos++;
        le->data[le->length] = '\0';
    }
}

static inline
void
le_backspace(LineEditor* le){
    if(le->cursor_pos > 0 && le->length > 0){
        // Shift characters to the left
        memmove(le->data + le->cursor_pos - 1,
                le->data + le->cursor_pos,
                le->length - le->cursor_pos);
        le->length--;
        le->cursor_pos--;
        le->data[le->length] = '\0';
    }
}

static inline
void
le_delete(LineEditor* le){
    if(le->cursor_pos < le->length){
        // Shift characters to the left
        memmove(le->data + le->cursor_pos,
                le->data + le->cursor_pos + 1,
                le->length - le->cursor_pos - 1);
        le->length--;
        le->data[le->length] = '\0';
    }
}

static inline
void
le_move_left(LineEditor* le){
    if(le->cursor_pos > 0){
        le->cursor_pos--;
    }
}

static inline
void
le_move_right(LineEditor* le){
    if(le->cursor_pos < le->length){
        le->cursor_pos++;
    }
}

static inline
void
le_move_home(LineEditor* le){
    le->cursor_pos = 0;
}

static inline
void
le_move_end(LineEditor* le){
    le->cursor_pos = le->length;
}

// Kill (delete) from cursor to end of line (Ctrl-K)
static inline
void
le_kill_line(LineEditor* le){
    if(le->cursor_pos < le->length){
        le->length = le->cursor_pos;
        le->data[le->length] = '\0';
    }
}

// Kill (delete) entire line (Ctrl-U)
static inline
void
le_kill_whole_line(LineEditor* le){
    le->length = 0;
    le->cursor_pos = 0;
    if(le->data) le->data[0] = '\0';
}

// Delete word backward (Ctrl-W)
static inline
void
le_delete_word_backward(LineEditor* le){
    if(le->cursor_pos == 0) return;

    size_t orig_pos = le->cursor_pos;

    // Skip trailing whitespace
    while(le->cursor_pos > 0 && le->data[le->cursor_pos - 1] == ' '){
        le->cursor_pos--;
    }

    // Delete word characters
    while(le->cursor_pos > 0 && le->data[le->cursor_pos - 1] != ' '){
        le->cursor_pos--;
    }

    // Shift remaining text left
    if(le->cursor_pos < orig_pos){
        size_t delete_len = orig_pos - le->cursor_pos;
        memmove(le->data + le->cursor_pos,
                le->data + orig_pos,
                le->length - orig_pos);
        le->length -= delete_len;
        le->data[le->length] = '\0';
    }
}

// Navigate to previous history entry (up arrow / Ctrl-P)
static inline
void
le_history_prev(LineEditor* le){
    if(!le->history || le->history->count == 0) return;

    // If not browsing yet, save current text and start from end of history
    if(!le->history->browsing){
        le->history->browsing = 1;
        le->history->browse_index = le->history->count;
        le->history->saved_length = le->length < 256 ? le->length : 255;
        memcpy(le->history->saved_current, le->data, le->history->saved_length);
        le->history->saved_current[le->history->saved_length] = '\0';
    }

    // Move to previous entry
    if(le->history->browse_index > 0){
        le->history->browse_index--;
        const char* entry = le->history->entries[le->history->browse_index];
        size_t entry_len = strlen(entry);
        if(entry_len < le->capacity){
            memcpy(le->data, entry, entry_len);
            le->data[entry_len] = '\0';
            le->length = entry_len;
            le->cursor_pos = entry_len; // Move cursor to end
        }
    }
}

// Navigate to next history entry (down arrow / Ctrl-N)
static inline
void
le_history_next(LineEditor* le){
    if(!le->history || !le->history->browsing) return;

    le->history->browse_index++;

    // If we've gone past the end, restore saved text and stop browsing
    if(le->history->browse_index >= le->history->count){
        le->history->browsing = 0;
        le->history->browse_index = le->history->count;
        memcpy(le->data, le->history->saved_current, le->history->saved_length);
        le->data[le->history->saved_length] = '\0';
        le->length = le->history->saved_length;
        le->cursor_pos = le->length; // Move cursor to end
    }
    else {
        const char* entry = le->history->entries[le->history->browse_index];
        size_t entry_len = strlen(entry);
        if(entry_len < le->capacity){
            memcpy(le->data, entry, entry_len);
            le->data[entry_len] = '\0';
            le->length = entry_len;
            le->cursor_pos = entry_len; // Move cursor to end
        }
    }
}

// Reset history browsing state
static inline
void
le_history_reset(LineEditor* le){
    if(!le->history) return;
    le->history->browsing = 0;
    le->history->browse_index = le->history->count;
}

// Handle common line editing keys
// Returns 1 if the key was handled, 0 otherwise
// If reset_history is true, resets history browsing before editing operations
// Key codes should match those used in the calling code
static inline
int
le_handle_key(LineEditor* le, int key, _Bool reset_history){
    // Define key codes (should match caller's definitions)
    enum {
        LE_CTRL_A = 1,
        LE_CTRL_B = 2,
        LE_CTRL_D = 4,
        LE_CTRL_E = 5,
        LE_CTRL_F = 6,
        LE_CTRL_H = 8,
        LE_CTRL_K = 11,
        LE_CTRL_U = 21,
        LE_CTRL_W = 23,
        LE_BACKSPACE = 127,
        LE_DELETE = -1,
        LE_LEFT = -4,
        LE_RIGHT = -5,
        LE_HOME = -6,
        LE_END = -7,
    };

    // Editing operations (reset history if requested)
    if(key == LE_BACKSPACE || key == 127 || key == LE_CTRL_H){
        if(reset_history) le_history_reset(le);
        le_backspace(le);
        return 1;
    }
    else if(key == LE_DELETE || key == LE_CTRL_D){
        if(reset_history) le_history_reset(le);
        le_delete(le);
        return 1;
    }
    else if(key == LE_CTRL_K){
        if(reset_history) le_history_reset(le);
        le_kill_line(le);
        return 1;
    }
    else if(key == LE_CTRL_U){
        if(reset_history) le_history_reset(le);
        le_kill_whole_line(le);
        return 1;
    }
    else if(key == LE_CTRL_W){
        if(reset_history) le_history_reset(le);
        le_delete_word_backward(le);
        return 1;
    }
    // Cursor movement operations (don't reset history)
    else if(key == LE_LEFT || key == LE_CTRL_B){
        le_move_left(le);
        return 1;
    }
    else if(key == LE_RIGHT || key == LE_CTRL_F){
        le_move_right(le);
        return 1;
    }
    else if(key == LE_HOME || key == LE_CTRL_A){
        le_move_home(le);
        return 1;
    }
    else if(key == LE_END || key == LE_CTRL_E){
        le_move_end(le);
        return 1;
    }

    return 0; // Key not handled
}

#endif
