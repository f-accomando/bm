/*
 * Shell SDK: the named lists of values of a block of the source, between ;@TAG and ;@end (assembly: a label, then
 * .byte lines) or //@TAG and //@end (bm C: const byte name[] = { }). The music of the Sounds tab and the
 * animations of the Sprite tab are such blocks. Pure model: no screen, the tests run on the PC.
 */
#ifndef SHELL_SDK_LISTS_H
#define SHELL_SDK_LISTS_H

#include <stddef.h>

#define SDK_LIST_NAME 12                /* a word of a value, with the NUL */
#define SDK_LIST_ITEMS 96               /* lists in a block */

/* A value: a word, a number, or a word + numbers. */
typedef struct { char word[SDK_LIST_NAME]; int num; int line; } sdk_value_t;
/* A list: its name, the line it starts at, its values value[first] to value[first + count - 1]. */
typedef struct { char name[24]; int line, first, count; } sdk_item_t;

typedef struct {
    const char *p, *end;
    int line, c;
    const char *what;                   /* the tag, for the messages */
    sdk_value_t *value;
    int values, values_max;
    sdk_item_t item[SDK_LIST_ITEMS];
    int items;
    char *err;
    size_t en;
} sdk_reader_t;

/* A letter in upper case; two words the same with no regard to case; a character of a word. */
int sdk_list_up(int c);
int sdk_list_same(const char *a, const char *b);
int sdk_list_word_char(int c);

size_t sdk_list_next_line(const char *s, size_t len, size_t p);
/* The first line from p that starts (after spaces) with the marker, followed by no letter or '-'. len: none. */
size_t sdk_list_find_marker(const char *s, size_t len, size_t p, const char *marker);
int sdk_list_line_number(const char *s, size_t p);
/* The block of a tag: [*from, *to) with its marker lines. 1, 0 when there is none, -1 when it has no end. */
int sdk_list_find(const char *src, size_t len, int c, const char *tag, size_t *from, size_t *to);

/* Reads the lists of a block. Returns 1 with a reader in *out (free it with sdk_list_free), 0 when there is no
 * block, or -1 with "line N: ..." in err. */
int sdk_list_read(const char *src, size_t len, int c, const char *tag, sdk_reader_t **out, char *err, size_t en);
void sdk_list_free(sdk_reader_t *r);
/* "line N: ..." in the reader's err; returns -1. */
int sdk_list_wrong(sdk_reader_t *r, int line, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

/* Text being written: into a buffer of max bytes, or one that grows (grow 1, from NULL; free s). */
typedef struct { char *s; size_t max, len; int full, grow; } sdk_text_t;

void sdk_list_add(sdk_text_t *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void sdk_list_add_str(sdk_text_t *t, const char *s);
/* snprintf at the place `at` of a line; the new end. */
size_t sdk_list_put(char *line, size_t at, size_t max, const char *fmt, ...) __attribute__((format(printf, 4, 5)));

/* A list being written: its lines of values, with a comment each. */
typedef struct {
    sdk_text_t *t;
    int c;
    char line[160];                     /* the values waiting */
    char note[64];
    int has;
} sdk_list_t;

void sdk_list_list_open(sdk_list_t *l, sdk_text_t *t, int c, const char *name, const char *note);
/* One line of values; the one before goes out. */
void sdk_list_list_line(sdk_list_t *l, const char *values, const char *note);
void sdk_list_list_flush(sdk_list_t *l, int last);

/* Puts the text of a whole block (marker lines too) in the Code tab in the place of the block of the tag; a new
 * block goes before the music or the map, else at the end. run as in sdk_edit_replace. */
int sdk_list_commit(const char *tag, int c, const char *text, size_t n, int run, char *err, size_t en);

#endif /* SHELL_SDK_LISTS_H */
