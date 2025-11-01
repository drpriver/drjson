#ifndef LINEEDIT_H
#define LINEEDIT_H
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

// History for text buffers
typedef struct TextBufferHistory TextBufferHistory;
struct TextBufferHistory {
    char** entries;          // Array of history entries
    size_t count;            // Number of entries
    size_t capacity;         // Capacity of array
    size_t browse_index;     // Current position when browsing (count = not browsing)
    char saved_current[256]; // Save current unsaved text when starting to browse
    size_t saved_length;     // Length of saved text
    _Bool browsing;          // True if currently browsing history
};

typedef struct TextBuffer TextBuffer;
struct TextBuffer {
    char* data;
    size_t length;
    size_t capacity;
    size_t cursor_pos;       // Cursor position (0 to length)
    TextBufferHistory* history; // Optional history
};

static inline
void
textbuffer_history_init(TextBufferHistory* hist){
    hist->entries = NULL;
    hist->count = 0;
    hist->capacity = 0;
    hist->browse_index = 0;
    hist->saved_length = 0;
    hist->browsing = 0;
}

static inline
void
textbuffer_history_free(TextBufferHistory* hist){
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
textbuffer_history_add(TextBufferHistory* hist, const char* text, size_t length){
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
textbuffer_init(TextBuffer* buf, size_t capacity){
    buf->data = malloc(capacity);
    buf->data[0] = '\0';
    buf->length = 0;
    buf->capacity = capacity;
    buf->cursor_pos = 0;
    buf->history = NULL;
}

static inline
void
textbuffer_free(TextBuffer* buf){
    free(buf->data);
    buf->data = NULL;
    buf->length = 0;
    buf->capacity = 0;
    buf->cursor_pos = 0;
}

static inline
void
textbuffer_clear(TextBuffer* buf){
    buf->length = 0;
    buf->cursor_pos = 0;
    if(buf->data) buf->data[0] = '\0';
}

static inline
void
textbuffer_append_char(TextBuffer* buf, char c){
    if(buf->length + 1 < buf->capacity){
        // Insert character at cursor position
        if(buf->cursor_pos < buf->length){
            // Shift characters to the right
            memmove(buf->data + buf->cursor_pos + 1,
                    buf->data + buf->cursor_pos,
                    buf->length - buf->cursor_pos);
        }
        buf->data[buf->cursor_pos] = c;
        buf->length++;
        buf->cursor_pos++;
        buf->data[buf->length] = '\0';
    }
}

static inline
void
textbuffer_backspace(TextBuffer* buf){
    if(buf->cursor_pos > 0 && buf->length > 0){
        // Shift characters to the left
        memmove(buf->data + buf->cursor_pos - 1,
                buf->data + buf->cursor_pos,
                buf->length - buf->cursor_pos);
        buf->length--;
        buf->cursor_pos--;
        buf->data[buf->length] = '\0';
    }
}

static inline
void
textbuffer_delete(TextBuffer* buf){
    if(buf->cursor_pos < buf->length){
        // Shift characters to the left
        memmove(buf->data + buf->cursor_pos,
                buf->data + buf->cursor_pos + 1,
                buf->length - buf->cursor_pos - 1);
        buf->length--;
        buf->data[buf->length] = '\0';
    }
}

static inline
void
textbuffer_move_left(TextBuffer* buf){
    if(buf->cursor_pos > 0){
        buf->cursor_pos--;
    }
}

static inline
void
textbuffer_move_right(TextBuffer* buf){
    if(buf->cursor_pos < buf->length){
        buf->cursor_pos++;
    }
}

static inline
void
textbuffer_move_home(TextBuffer* buf){
    buf->cursor_pos = 0;
}

static inline
void
textbuffer_move_end(TextBuffer* buf){
    buf->cursor_pos = buf->length;
}

// Kill (delete) from cursor to end of line (Ctrl-K)
static inline
void
textbuffer_kill_line(TextBuffer* buf){
    if(buf->cursor_pos < buf->length){
        buf->length = buf->cursor_pos;
        buf->data[buf->length] = '\0';
    }
}

// Kill (delete) entire line (Ctrl-U)
static inline
void
textbuffer_kill_whole_line(TextBuffer* buf){
    buf->length = 0;
    buf->cursor_pos = 0;
    if(buf->data) buf->data[0] = '\0';
}

// Delete word backward (Ctrl-W)
static inline
void
textbuffer_delete_word_backward(TextBuffer* buf){
    if(buf->cursor_pos == 0) return;

    size_t orig_pos = buf->cursor_pos;

    // Skip trailing whitespace
    while(buf->cursor_pos > 0 && buf->data[buf->cursor_pos - 1] == ' '){
        buf->cursor_pos--;
    }

    // Delete word characters
    while(buf->cursor_pos > 0 && buf->data[buf->cursor_pos - 1] != ' '){
        buf->cursor_pos--;
    }

    // Shift remaining text left
    if(buf->cursor_pos < orig_pos){
        size_t delete_len = orig_pos - buf->cursor_pos;
        memmove(buf->data + buf->cursor_pos,
                buf->data + orig_pos,
                buf->length - orig_pos);
        buf->length -= delete_len;
        buf->data[buf->length] = '\0';
    }
}

// Navigate to previous history entry (up arrow / Ctrl-P)
static inline
void
textbuffer_history_prev(TextBuffer* buf){
    if(!buf->history || buf->history->count == 0) return;

    // If not browsing yet, save current text and start from end of history
    if(!buf->history->browsing){
        buf->history->browsing = 1;
        buf->history->browse_index = buf->history->count;
        buf->history->saved_length = buf->length < 256 ? buf->length : 255;
        memcpy(buf->history->saved_current, buf->data, buf->history->saved_length);
        buf->history->saved_current[buf->history->saved_length] = '\0';
    }

    // Move to previous entry
    if(buf->history->browse_index > 0){
        buf->history->browse_index--;
        const char* entry = buf->history->entries[buf->history->browse_index];
        size_t entry_len = strlen(entry);
        if(entry_len < buf->capacity){
            memcpy(buf->data, entry, entry_len);
            buf->data[entry_len] = '\0';
            buf->length = entry_len;
            buf->cursor_pos = entry_len; // Move cursor to end
        }
    }
}

// Navigate to next history entry (down arrow / Ctrl-N)
static inline
void
textbuffer_history_next(TextBuffer* buf){
    if(!buf->history || !buf->history->browsing) return;

    buf->history->browse_index++;

    // If we've gone past the end, restore saved text and stop browsing
    if(buf->history->browse_index >= buf->history->count){
        buf->history->browsing = 0;
        buf->history->browse_index = buf->history->count;
        memcpy(buf->data, buf->history->saved_current, buf->history->saved_length);
        buf->data[buf->history->saved_length] = '\0';
        buf->length = buf->history->saved_length;
        buf->cursor_pos = buf->length; // Move cursor to end
    }
    else {
        const char* entry = buf->history->entries[buf->history->browse_index];
        size_t entry_len = strlen(entry);
        if(entry_len < buf->capacity){
            memcpy(buf->data, entry, entry_len);
            buf->data[entry_len] = '\0';
            buf->length = entry_len;
            buf->cursor_pos = entry_len; // Move cursor to end
        }
    }
}

// Reset history browsing state
static inline
void
textbuffer_history_reset(TextBuffer* buf){
    if(!buf->history) return;
    buf->history->browsing = 0;
    buf->history->browse_index = buf->history->count;
}

#endif
