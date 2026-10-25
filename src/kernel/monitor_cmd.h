/*
 * The Monitor's command core (no hardware, no kprintf): a table of commands,
 * a tokenizer, a dispatcher, help made from the table, Tab completion, a
 * line editor with history. src/kernel/monitor.c holds the table and the
 * handlers; tests/kernel/test_monitor_cmd.c tests this file on the PC.
 *
 * To add a command: add ONE row to the table in monitor.c. The help, the
 * Tab completion and the single-key shortcut come from that row.
 */
#ifndef MONITOR_CMD_H
#define MONITOR_CMD_H

#include <stddef.h>

#define MCMD_MAX_ARGS 16

/* argv[0] is the word of the row that was matched (a subcommand's own name
 * for a subcommand row), argv[1..] are its arguments. 0 = done; any other
 * value stops the rest of a ';' line. */
typedef int (*mcmd_fn)(int argc, char **argv);

typedef struct mcmd {
    const char *name;               /* the word; NULL ends a table */
    const char *args;               /* usage after the name: "NAME [PASSWORD]" (or NULL) */
    const char *help;               /* one line; NULL: no line in the help */
    char key;                       /* single-key shortcut (0: none) */
    mcmd_fn fn;                     /* NULL: a group with subcommands only */
    const struct mcmd *sub;         /* subcommands (a table), or NULL */
    const char *alias;              /* a hidden old word: the words it stands for ("bench 3d") */
    /* extra words for the Tab key after this command's words: the i-th, NULL at the end
     * (the keys of "set"). A word that ends in '=' gets no space after it. */
    const char *(*cand)(int i);
} mcmd_t;

/* errors of the core (a handler returns 0 or a small positive/-1 value) */
#define MCMD_E_UNKNOWN (-100)       /* no such command */
#define MCMD_E_SUBCMD  (-101)       /* the command has subcommands and the word is none of them */
#define MCMD_E_NOFN    (-102)       /* a group with no handler of its own, and no subcommand given */
#define MCMD_E_QUOTE   (-103)       /* a quote that is not closed */
#define MCMD_E_ARGS    (-104)       /* more than MCMD_MAX_ARGS words */

/* The next command of *pp, up to an unquoted ';' or the end: words are split
 * on spaces, "..." and '...' keep spaces, \ takes the next character; the
 * quotes are removed (key="a b" gives the word key=a b). *pp is moved to the
 * text after the ';', or NULL at the end of the line. 0, or a MCMD_E_*. */
int mcmd_split(char **pp, char **argv, int max, int *argc);

/* Runs one command: finds the row (an alias is replaced by its words),
 * follows the subcommand words, calls the handler. On a core error it
 * returns MCMD_E_* and *where is the row it stopped at (NULL: the table)
 * and *word the word that was not understood. */
int mcmd_exec(const mcmd_t *tab, int argc, char **argv, const mcmd_t **where, const char **word);

/* The line runner. before: called with each command before it runs (the
 * echo); error: a message for the user (unknown command, bad subcommand,
 * a quote not closed); a command that fails stops the line. Returns 0, or
 * -1 when the line stopped. The text of line is changed. */
typedef struct {
    void (*before)(int argc, char **argv);
    void (*error)(const char *msg);
} mcmd_hooks_t;
int mcmd_run_line(const mcmd_t *tab, char *line, const mcmd_hooks_t *h);

/* The message for a core error, as mcmd_run_line says it. */
void mcmd_error_text(const mcmd_t *tab, int err, const mcmd_t *where, const char *word, char *out,
                     size_t cap);

/* Names that start like word (or, if none, close to it): "a, b, c" (at most 4); 0 if none. */
int mcmd_suggest(const mcmd_t *tab, const char *word, char *out, size_t cap);

/* The row of a shortcut key, also inside subcommands; NULL if none. The path
 * of words that reaches it goes to path (for the message). */
const mcmd_t *mcmd_find_key(const mcmd_t *tab, int key, char *path, size_t cap);

/* The row of a top-level word (an alias row too), or NULL. */
const mcmd_t *mcmd_find(const mcmd_t *tab, const char *name);

/* The help, made from the table. head and foot are lines of text before and after
 * the list. One row for each command and subcommand that has a help line; the
 * aliases in a last line. Returns the length written (always ends with a newline). */
size_t mcmd_help_all(const mcmd_t *tab, const char *head, const char *foot, char *out, size_t cap);
/* The help of one command by its words (argv = the words after "help"); 0 if none found. */
size_t mcmd_help_one(const mcmd_t *tab, int argc, char **argv, char *out, size_t cap);

/* ---- the line editor (history, Tab, cursor keys) ---- */

#define MEDIT_LINE_MAX 480           /* as long as the line of the old ':' (bm_net.py --line) */
#define MEDIT_HIST 16

/* the keys the editor reads: bytes 0..255, or one of these (a USB keyboard
 * sends them as is; a terminal sends ESC [ A and so on, which the editor decodes) */
#define MEDIT_K_UP    0x101
#define MEDIT_K_DOWN  0x102
#define MEDIT_K_LEFT  0x103
#define MEDIT_K_RIGHT 0x104
#define MEDIT_K_HOME  0x105
#define MEDIT_K_END   0x106
#define MEDIT_K_DEL   0x107
#define MEDIT_K_ESC   0x108         /* an Esc that nothing followed */

/* what medit_feed says */
#define MEDIT_NONE   0              /* the line goes on */
#define MEDIT_LINE   1              /* Enter: medit_text() is the line (it is in the history) */
#define MEDIT_CANCEL 2              /* Ctrl-C: the line is dropped */
#define MEDIT_EXIT   3              /* Esc, or Ctrl-D on an empty line */

typedef void (*medit_out_fn)(const char *s);

typedef struct {
    const mcmd_t *tab;
    medit_out_fn out;               /* the screen/terminal; plain text, \n, \r, \b, ESC [ K */
    const char *prompt;
    char buf[MEDIT_LINE_MAX];
    int len, pos;
    int esc;                        /* 0, 1 after ESC, 2 in ESC [, 3 in ESC O */
    int esc_num;                    /* the number in ESC [ n ~ */
    int last_cr;                    /* the previous key was \r: a \n after it is no Enter */
    int browse;                     /* -1, or the history entry on screen (0 = the newest) */
    char saved[MEDIT_LINE_MAX];     /* the line being typed, while the history is on screen */
    char hist[MEDIT_HIST][MEDIT_LINE_MAX];
    int hist_n;                     /* entries held */
    int hist_head;                  /* where the next one goes */
} medit_t;

void medit_init(medit_t *e, const mcmd_t *tab, medit_out_fn out);
/* A new, empty line; prints the prompt if show is not 0. */
void medit_begin(medit_t *e, const char *prompt, int show);
/* Prints the prompt and the line again (after a command wrote on the screen). */
void medit_redraw(medit_t *e);
int medit_feed(medit_t *e, int key);
const char *medit_text(const medit_t *e);
/* 1 if the last key was a lone ESC byte: more may follow (the caller waits a few
 * ms, then sends MEDIT_K_ESC if nothing came). */
int medit_in_escape(const medit_t *e);
/* The i-th line of the history, 0 = the newest; NULL if none. */
const char *medit_history(const medit_t *e, int i);

#endif
