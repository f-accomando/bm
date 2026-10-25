/*
 * The Monitor's command core (src/kernel/monitor_cmd.c) on the PC: the tokenizer, the
 * dispatcher, the help made from the table, the Tab completion, the line editor and its
 * history, with fake handlers. A small terminal follows what the editor prints (\r, \b,
 * ESC [ K), so a check can compare the line on the screen with the line in the editor.
 */
#include "../../src/kernel/monitor_cmd.h"

#include <stdio.h>
#include <string.h>

static int fails, checks;
#define check(c, w) check_((c) != 0, w)
static void check_(int ok, const char *what)
{
    checks++;
    if (!ok) {
        fails++;
        printf("FAIL %s\n", what);
    }
}

/* ---- the fake handlers: they write what they got into `seen` */
static char seen[256];
static int calls;

static int record(const char *tag, int argc, char **argv)
{
    int n = (int)strlen(seen);
    n += snprintf(seen + n, sizeof seen - (size_t)n, "%s:", tag);
    for (int i = 0; i < argc; i++)
        n += snprintf(seen + n, sizeof seen - (size_t)n, "%s%s", i ? "," : "", argv[i]);
    n += snprintf(seen + n, sizeof seen - (size_t)n, ";");
    calls++;
    return 0;
}

static int h_plain(int argc, char **argv) { return record("plain", argc, argv); }
static int h_list(int argc, char **argv) { return record("list", argc, argv); }
static int h_join(int argc, char **argv) { return record("join", argc, argv); }
static int h_wifi(int argc, char **argv) { return record("wifi", argc, argv); }
static int h_pad(int argc, char **argv) { return record("pad", argc, argv); }
static int h_fail(int argc, char **argv) { record("fail", argc, argv); return -1; }
static int h_set(int argc, char **argv) { return record("set", argc, argv); }
static int h_help(int argc, char **argv) { return record("help", argc, argv); }
static int h_3d(int argc, char **argv) { return record("3d", argc, argv); }
static int h_exit(int argc, char **argv) { return record("exit", argc, argv); }

static const char *const keys[] = { "layout=", "led=", "volume=", "wifi_boot=" };
static const char *cand_keys(int i)
{
    return i >= 0 && i < 4 ? keys[i] : NULL;
}

static const mcmd_t pair_sub[] = {
    { .name = "pad", .help = "pair a controller", .key = 'T', .fn = h_pad },
    { .name = "kbd", .help = "pair a keyboard", .key = 'K', .fn = h_pad },
    { .name = "mouse", .help = "pair a mouse", .key = 'O', .fn = h_pad },
    { NULL }
};
static const mcmd_t bt_sub[] = {
    { .name = "pair", .help = "pair a device", .sub = pair_sub },
    { .name = "forget", .args = "[yes]", .help = "forget all", .key = 'P', .fn = h_plain },
    { NULL }
};
static const mcmd_t wifi_sub[] = {
    { .name = "list", .help = "list the networks", .fn = h_list },
    { .name = "join", .args = "NAME [PASSWORD]", .help = "join a network", .fn = h_join },
    { .name = "forget", .help = "forget it", .fn = h_plain },
    { NULL }
};
static const mcmd_t bench_sub[] = {
    { .name = "cpu", .help = "cpu", .key = 'k', .fn = h_plain },
    { .name = "3d", .args = "[tests=A,B profiles=P]", .help = "3D Bench, the pages and a long text to wrap", .key = 'j',
      .fn = h_3d },
    { NULL }
};
static const mcmd_t table[] = {
    { .name = "help", .args = "[COMMAND]", .help = "this list", .key = 'h', .fn = h_help, .cand = NULL },
    { .name = "wifi", .help = "start and join", .key = 'W', .fn = h_wifi, .sub = wifi_sub },
    { .name = "bt", .help = "Bluetooth", .sub = bt_sub },
    { .name = "bench", .help = "benchmarks", .sub = bench_sub },
    { .name = "set", .args = "[KEY=VALUE ...]", .help = "config keys", .fn = h_set, .cand = cand_keys },
    { .name = "send", .help = "send", .key = 'z', .fn = h_plain },
    { .name = "save", .help = "save", .fn = h_plain },
    { .name = "reboot", .help = "reboot", .key = 'r', .fn = h_plain },
    { .name = "exit", .help = "leave", .fn = h_exit },
    { .name = "fail", .help = "fails", .fn = h_fail },
    { .name = "b3d", .alias = "bench 3d" },
    { .name = "pair", .alias = "bt pair" },
    { NULL }
};

/* ---- a run of one line, with its messages */
static char errs[512];
static int echoes;
static void hook_before(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    echoes++;
}
static void hook_error(const char *msg)
{
    strncat(errs, msg, sizeof errs - strlen(errs) - 1);
    strncat(errs, "|", sizeof errs - strlen(errs) - 1);
}
static const mcmd_hooks_t hooks = { hook_before, hook_error };

static int run(const char *text)
{
    char line[512];
    snprintf(line, sizeof line, "%s", text);
    seen[0] = errs[0] = 0;
    calls = echoes = 0;
    return mcmd_run_line(table, line, &hooks);
}

static void test_tokenizer(void)
{
    char *argv[MCMD_MAX_ARGS];
    int argc;
    char a[] = "wifi join  \"my net\" 'a b'c  x\\ y key=\"v w\"; next", *p = a;

    check(mcmd_split(&p, argv, MCMD_MAX_ARGS, &argc) == 0 && p && argc == 6, "split: first command");
    check(!strcmp(argv[0], "wifi") && !strcmp(argv[2], "my net") && !strcmp(argv[3], "a bc") &&
              !strcmp(argv[4], "x y") && !strcmp(argv[5], "key=v w"), "split: quotes and a backslash");
    check(mcmd_split(&p, argv, MCMD_MAX_ARGS, &argc) == 0 && p == NULL && argc == 1 && !strcmp(argv[0], "next"),
          "split: the command after ';'");
    char b[] = "say \"a;b\" ; x";
    p = b;
    check(mcmd_split(&p, argv, MCMD_MAX_ARGS, &argc) == 0 && argc == 2 && !strcmp(argv[1], "a;b") && p,
          "split: a ';' in quotes");
    char c[] = "x \"open";
    p = c;
    check(mcmd_split(&p, argv, MCMD_MAX_ARGS, &argc) == MCMD_E_QUOTE, "split: a quote not closed");
    char d[] = "a b c d e f g h i j k l m n o p q";
    p = d;
    check(mcmd_split(&p, argv, MCMD_MAX_ARGS, &argc) == MCMD_E_ARGS, "split: too many words");
    char e[] = "   ";
    p = e;
    check(mcmd_split(&p, argv, MCMD_MAX_ARGS, &argc) == 0 && argc == 0 && p == NULL, "split: blank");
    char f[] = ";;a";
    p = f;
    check(mcmd_split(&p, argv, MCMD_MAX_ARGS, &argc) == 0 && argc == 0 && p, "split: an empty command before ';'");
    char g[] = "set k=\"\"";
    p = g;
    check(mcmd_split(&p, argv, MCMD_MAX_ARGS, &argc) == 0 && argc == 2 && !strcmp(argv[1], "k="),
          "split: an empty quoted value");
}

static void test_dispatch(void)
{
    check(run("send") == 0 && !strcmp(seen, "plain:send;"), "run: a plain command");
    check(run("wifi join Home pw") == 0 && !strcmp(seen, "join:join,Home,pw;"), "run: subcommand, its arguments");
    check(run("wifi") == 0 && !strcmp(seen, "wifi:wifi;"), "run: a group with a handler, no subcommand");
    check(run("bt pair pad") == 0 && !strcmp(seen, "pad:pad;"), "run: two levels");
    check(run("b3d tests=a profiles=P") == 0 && !strcmp(seen, "3d:3d,tests=a,profiles=P;"),
          "run: an old word becomes 'bench 3d' with its arguments");
    check(run("pair kbd") == 0 && !strcmp(seen, "pad:kbd;"), "run: an alias for a group, then a subcommand");
    check(run("send; save; reboot") == 0 && !strcmp(seen, "plain:send;plain:save;plain:reboot;") && echoes == 3,
          "run: ';' runs them one after the other, echo for each");
    check(run("save;;send") == 0 && calls == 2, "run: an empty command is skipped");
    check(run("fail; send") == -1 && calls == 1, "run: a command that fails stops the line");
    check(run("nope; send") == -1 && calls == 0 && strstr(errs, "'nope': not a command of the line"),
          "run: an unknown word stops the line and says so");
    check(run("sen") == -1 && strstr(errs, "did you mean: send"), "run: suggestion by prefix");
    check(run("sned") == -1 && strstr(errs, "did you mean: send (") != NULL, "run: suggestion for a typo");
    check(run("zzzz") == -1 && !strstr(errs, "did you mean"), "run: no suggestion for a far word");
    check(run("wifi nope") == -1 && strstr(errs, "'wifi': not a subcommand: 'nope'; use: list, join, forget"),
          "run: a bad subcommand lists the good ones");
    check(run("bt") == -1 && strstr(errs, "'bt': needs one of: pair, forget"), "run: a group with no handler");
    check(run("send \"x") == -1 && strstr(errs, "quote") && calls == 0, "run: a quote not closed runs nothing");
    check(run("") == 0 && calls == 0, "run: an empty line");
    check(run("wifi nope; send") == -1 && calls == 0, "run: bad subcommand stops the line");

    char path[64];
    const mcmd_t *r = mcmd_find_key(table, 'T', path, sizeof path);
    check(r && r->fn == h_pad && !strcmp(path, "bt pair pad"), "key: a key inside two levels, its path");
    r = mcmd_find_key(table, 'W', path, sizeof path);
    check(r && r->fn == h_wifi && !strcmp(path, "wifi"), "key: a group row with a handler");
    check(mcmd_find_key(table, 'Q', path, sizeof path) == NULL, "key: none");
    check(mcmd_find_key(table, 'z', path, sizeof path)->fn == h_plain, "key: a plain command");
    char sug[64];
    check(mcmd_suggest(table, "w", sug, sizeof sug) == 1 && !strcmp(sug, "wifi"), "suggest: one prefix");
    check(mcmd_suggest(table, "b", sug, sizeof sug) == 3 && !strcmp(sug, "bt, bench, b3d"), "suggest: prefixes in table order, aliases too");
}

static void test_help(void)
{
    static char out[4096];
    mcmd_help_all(table, "HEAD\n", "FOOT\n", out, sizeof out);
    check(!strncmp(out, "HEAD\n", 5) && strstr(out, "FOOT\n"), "help: head and foot");
    check(strstr(out, "  W  wifi ") && strstr(out, "start and join"), "help: a group row with a handler, its key");
    check(strstr(out, "  T  bt pair pad ") && strstr(out, "  P  bt forget [yes] "), "help: subcommands with their key");
    check(strstr(out, "wifi join NAME [PASSWORD]  join a network") || strstr(out, "wifi join NAME [PASSWORD]"),
          "help: usage with the arguments");
    check(!strstr(out, "  bt  ") && !strstr(out, "bt pair  "), "help: a group with no handler has no line of its own");
    check(strstr(out, "old words that still work: b3d=bench 3d, pair=bt pair\n"), "help: the aliases, one line");
    int ok = 1;
    for (char *l = out; *l;) {
        char *nl = strchr(l, '\n');
        if (!nl)
            break;
        if (nl - l > 78)
            ok = 0;
        l = nl + 1;
    }
    check(ok, "help: no line longer than 78 columns (a long usage goes to its own line)");
    check(strstr(out, "bench 3d [tests=A,B profiles=P]\n"), "help: a long usage has its own line");

    char *w1[] = { "wifi" };
    mcmd_help_one(table, 1, w1, out, sizeof out);
    check(strstr(out, "wifi") && strstr(out, "wifi list") && strstr(out, "wifi join NAME [PASSWORD]") &&
              strstr(out, "wifi forget"), "help one: a group and its subcommands");
    char *w2[] = { "wifi", "join" };
    mcmd_help_one(table, 2, w2, out, sizeof out);
    check(strstr(out, "wifi join NAME [PASSWORD]") && !strstr(out, "wifi list"), "help one: a subcommand");
    char *w3[] = { "b3d" };
    mcmd_help_one(table, 1, w3, out, sizeof out);
    check(strstr(out, "b3d is the old word for 'bench 3d'") && strstr(out, "bench 3d"), "help one: an old word");
    char *w4[] = { "nothing" };
    check(mcmd_help_one(table, 1, w4, out, sizeof out) == 0, "help one: none");
    char *w5[] = { "send", "x" };
    check(mcmd_help_one(table, 2, w5, out, sizeof out) == 0, "help one: a word after a command with no subcommands");
    char small[20];
    size_t n = mcmd_help_all(table, "H", "F", small, sizeof small);
    check(n < sizeof small && strlen(small) == n, "help: a small buffer is not overrun");
}

/* ---- the editor, with a small terminal */
static char scr[32][600];               /* the lines that scrolled and the one with the cursor */
static int row, col;
static char outlog[4096];

static void term_put(char c)
{
    char *l = scr[row];
    if (c == '\n') {
        if (row < 30)
            row++;
        scr[row][0] = 0;
        col = 0;
    } else if (c == '\r') {
        col = 0;
    } else if (c == '\b') {
        if (col > 0)
            col--;
    } else if (c >= 32) {
        size_t len = strlen(l);
        while ((size_t)col > len)
            l[len++] = ' ';
        l[col] = c;
        if ((size_t)col >= len)
            l[col + 1] = 0;
        col++;
    }
}

static void term_out(const char *s)
{
    strncat(outlog, s, sizeof outlog - strlen(outlog) - 1);
    for (; *s; s++) {
        if (s[0] == '\x1b' && s[1] == '[' && s[2] == 'K') {
            scr[row][col] = 0;
            s += 2;
        } else {
            term_put(*s);
        }
    }
}

static medit_t ed;

static void term_reset(void)
{
    memset(scr, 0, sizeof scr);
    row = col = 0;
    outlog[0] = 0;
}

static int typed_ev;                    /* the last event medit_feed gave */
static int feed(int k) { return typed_ev = medit_feed(&ed, k); }
static void type(const char *s)
{
    for (; *s; s++)
        feed((unsigned char)*s);
}
static void begin(void)
{
    term_reset();
    medit_begin(&ed, "bm> ", 1);
}

/* the line on the screen, the cursor on it, are those of the editor */
static int screen_ok(void)
{
    char want[700];
    char have[700];
    snprintf(want, sizeof want, "bm> %s", medit_text(&ed));
    snprintf(have, sizeof have, "%s", scr[row]);
    for (size_t n = strlen(have); n > 0 && have[n - 1] == ' ' && (int)n > col; n--)
        have[n - 1] = 0;                /* a blanked character is a space on the screen */
    return !strcmp(have, want) && col == 4 + ed.pos;
}

static void test_editor_basic(void)
{
    medit_init(&ed, table, term_out);
    begin();
    type("sned");
    check(!strcmp(medit_text(&ed), "sned") && screen_ok(), "edit: typing is echoed");
    feed(0x7F);
    feed(8);
    check(!strcmp(medit_text(&ed), "sn") && screen_ok(), "edit: Backspace (0x7F and 0x08)");
    type("d");
    feed(MEDIT_K_LEFT);
    feed(MEDIT_K_LEFT);
    type("e");
    check(!strcmp(medit_text(&ed), "send") && screen_ok(), "edit: insert in the middle");
    feed(MEDIT_K_HOME);
    check(ed.pos == 0 && screen_ok(), "edit: Home");
    feed(MEDIT_K_DEL);
    check(!strcmp(medit_text(&ed), "end") && screen_ok(), "edit: Del");
    feed(MEDIT_K_END);
    check(ed.pos == ed.len && screen_ok(), "edit: End");
    feed(21);                           /* Ctrl-U */
    check(medit_text(&ed)[0] == 0 && screen_ok(), "edit: Ctrl-U clears");
    type("wifi list");
    feed(1);                            /* Ctrl-A */
    feed(11);                           /* Ctrl-K */
    check(medit_text(&ed)[0] == 0 && screen_ok(), "edit: Ctrl-A, Ctrl-K");
    type("one two three");
    feed(23);                           /* Ctrl-W */
    check(!strcmp(medit_text(&ed), "one two ") && screen_ok(), "edit: Ctrl-W deletes a word");
    feed(3);
    check(typed_ev == MEDIT_CANCEL && medit_text(&ed)[0] == 0 && strstr(scr[row - 1], "^C"), "edit: Ctrl-C cancels");
    begin();
    feed(4);
    check(typed_ev == MEDIT_EXIT, "edit: Ctrl-D on an empty line leaves");
    begin();
    type("ab");
    feed(MEDIT_K_LEFT);
    feed(4);
    check(typed_ev == MEDIT_NONE && !strcmp(medit_text(&ed), "a") && screen_ok(), "edit: Ctrl-D in a line is Del");
    feed(MEDIT_K_ESC);
    check(typed_ev == MEDIT_EXIT, "edit: Esc leaves");
    begin();
    type("save");
    feed('\r');
    check(typed_ev == MEDIT_LINE && !strcmp(medit_text(&ed), "save"), "edit: Enter gives the line");
    feed('\n');
    check(typed_ev == MEDIT_NONE, "edit: the \\n of \\r\\n is no Enter");
    begin();
    feed('\n');
    check(typed_ev == MEDIT_LINE, "edit: a lone \\n is an Enter");
    begin();
    begin();
    for (int i = 0; i < 700; i++)
        feed('x');
    check(ed.len == MEDIT_LINE_MAX - 1 && screen_ok(), "edit: the line stops at its limit");
}

static void test_editor_escape(void)
{
    medit_init(&ed, table, term_out);
    begin();
    type("save");
    feed('\r');
    begin();
    type("send");
    feed(0x1B);
    check(medit_in_escape(&ed), "esc: a lone ESC byte may be followed");
    feed('[');
    check(!medit_in_escape(&ed), "esc: ESC [ is a sequence");
    feed('A');                          /* Up, one byte at a time */
    check(!strcmp(medit_text(&ed), "save") && screen_ok(), "esc: ESC [ A, a byte at a time, is Up");
    feed(0x1B);
    feed('[');
    feed('B');
    check(!strcmp(medit_text(&ed), "send") && screen_ok(), "esc: ESC [ B is Down, the line typed comes back");
    feed(0x1B);
    feed('[');
    feed('D');
    feed(0x1B);
    feed('O');
    feed('D');
    check(ed.pos == ed.len - 2 && screen_ok(), "esc: ESC [ D and ESC O D move left");
    feed(0x1B);
    feed('[');
    feed('C');
    check(ed.pos == ed.len - 1, "esc: ESC [ C moves right");
    feed(0x1B);
    feed('[');
    feed('3');
    feed('~');
    check(!strcmp(medit_text(&ed), "sen") && screen_ok(), "esc: ESC [ 3 ~ is Del");
    feed(0x1B);
    feed('[');
    feed('1');
    feed('~');
    check(ed.pos == 0, "esc: ESC [ 1 ~ is Home");
    feed(0x1B);
    feed('[');
    feed('F');
    check(ed.pos == ed.len, "esc: ESC [ F is End");
    feed(0x1B);
    feed('[');
    feed('1');
    feed(';');
    feed('5');
    feed('D');
    check(ed.pos == ed.len - 1, "esc: ESC [ 1 ; 5 D (with a modifier) is a plain left");
    feed(0x1B);
    feed('[');
    feed('6');
    feed('~');
    check(!strcmp(medit_text(&ed), "sen") && typed_ev == MEDIT_NONE, "esc: PgDn is dropped");
    feed(0x1B);
    feed('x');
    check(!strcmp(medit_text(&ed), "sen") && !medit_in_escape(&ed), "esc: Alt + a key is dropped");
    feed(0x1B);
    feed(MEDIT_K_ESC);
    check(typed_ev == MEDIT_EXIT && !medit_in_escape(&ed), "esc: ESC then nothing (the timer): Esc");
    /* a keyboard that gives its arrows as codes */
    begin();
    feed(MEDIT_K_UP);
    check(!strcmp(medit_text(&ed), "save") && screen_ok(), "esc: a key code Up from a USB keyboard");
    /* \r\n after an escape sequence is still one Enter */
    feed('\r');
    feed('\n');
    check(typed_ev == MEDIT_NONE, "esc: \\r\\n is one Enter");
}

static void test_history(void)
{
    medit_init(&ed, table, term_out);
    for (int i = 1; i <= 20; i++) {
        char s[16];
        snprintf(s, sizeof s, "send %d", i);
        begin();
        type(s);
        feed('\r');
    }
    check(ed.hist_n == MEDIT_HIST, "history: holds 16 lines");
    check(!strcmp(medit_history(&ed, 0), "send 20") && !strcmp(medit_history(&ed, 15), "send 5") &&
              medit_history(&ed, 16) == NULL, "history: the newest 16, newest first");
    begin();
    type("abc");
    for (int i = 0; i < 16; i++)
        feed(MEDIT_K_UP);
    check(!strcmp(medit_text(&ed), "send 5") && screen_ok(), "history: Up to the oldest");
    feed(MEDIT_K_UP);
    check(!strcmp(medit_text(&ed), "send 5"), "history: Up at the oldest stays");
    for (int i = 0; i < 16; i++)
        feed(MEDIT_K_DOWN);
    check(!strcmp(medit_text(&ed), "abc") && screen_ok(), "history: Down back to the line typed");
    feed(MEDIT_K_DOWN);
    check(!strcmp(medit_text(&ed), "abc"), "history: Down at the end stays");
    feed(MEDIT_K_UP);
    feed(MEDIT_K_UP);
    type("!");
    feed('\r');
    check(!strcmp(medit_history(&ed, 0), "send 19!"), "history: an old line, edited, is a new line");
    begin();
    type("send 19!");
    feed('\r');
    check(!strcmp(medit_history(&ed, 1), "send 20"), "history: the same line twice in a row is kept once");
    begin();
    feed('\r');
    check(!strcmp(medit_history(&ed, 0), "send 19!"), "history: an empty line is not kept");
}

static void test_tab(void)
{
    medit_init(&ed, table, term_out);
    begin();
    type("wi");
    feed('\t');
    check(!strcmp(medit_text(&ed), "wifi ") && screen_ok(), "tab: a command, unique: completed and a space");
    type("j");
    feed('\t');
    check(!strcmp(medit_text(&ed), "wifi join ") && screen_ok(), "tab: a subcommand");
    begin();
    type("s");
    feed('\t');
    check(!strcmp(medit_text(&ed), "s") && strstr(outlog, "send") && strstr(outlog, "set") && strstr(outlog, "save"),
          "tab: several: the list, the line stays");
    check(strncmp(scr[row], "bm> s", 5) == 0 && screen_ok(), "tab: the prompt and the line again after the list");
    type("e");
    feed('\t');
    check(!strcmp(medit_text(&ed), "se") && strstr(outlog, "send"), "tab: se -> still send / set: list");
    type("n");
    feed('\t');
    check(!strcmp(medit_text(&ed), "send ") && screen_ok(), "tab: se + n -> send");
    begin();
    type("b");
    feed('\t');
    check(!strcmp(medit_text(&ed), "b"), "tab: b -> bt, bench: no common start beyond b");
    type("e");
    feed('\t');
    check(!strcmp(medit_text(&ed), "bench "), "tab: be -> bench");
    begin();
    type("bt pair m");
    feed('\t');
    check(!strcmp(medit_text(&ed), "bt pair mouse "), "tab: two levels");
    begin();
    type("set v");
    feed('\t');
    check(!strcmp(medit_text(&ed), "set volume=") && screen_ok(), "tab: a key of set, no space after the '='");
    begin();
    type("set l");
    feed('\t');
    check(!strcmp(medit_text(&ed), "set l") && strstr(outlog, "layout=") && strstr(outlog, "led="), "tab: set l: layout, led");
    type("a");
    feed('\t');
    check(!strcmp(medit_text(&ed), "set layout="), "tab: set la -> layout=");
    begin();
    type("set layout=i");
    feed('\t');
    check(!strcmp(medit_text(&ed), "set layout=i"), "tab: after the '=' nothing is added");
    begin();
    type("zz");
    feed('\t');
    check(!strcmp(medit_text(&ed), "zz"), "tab: no candidate: nothing happens");
    begin();
    type("b3");
    feed('\t');
    check(!strcmp(medit_text(&ed), "b3d "), "tab: an old word is completed too");
    begin();
    type("b3d t");
    feed('\t');
    check(!strcmp(medit_text(&ed), "b3d t"), "tab: no candidates for the arguments of an old word");
    begin();
    type("wifi join ");
    feed('\t');
    check(!strcmp(medit_text(&ed), "wifi join "), "tab: a command whose arguments are free");
    begin();
    type("send wi");
    feed(MEDIT_K_HOME);
    feed(MEDIT_K_RIGHT);
    feed('\t');
    check(screen_ok(), "tab: in the middle of a line the screen stays right");
    begin();
    type("ex");
    feed(MEDIT_K_LEFT);
    feed(MEDIT_K_LEFT);
    type("wifi ");
    feed('\t');
    check(screen_ok(), "tab: the screen is right after Tab in a line with text after the cursor");
    begin();
    type("\"wi");
    feed('\t');
    check(!strcmp(medit_text(&ed), "\"wi"), "tab: not inside quotes");
    begin();
    type("wifi li");
    feed('\t');
    type("x");
    check(!strcmp(medit_text(&ed), "wifi list x") && screen_ok(), "tab: typing goes on after a completion");
}

/* the line, from the editor to the dispatcher: what the Monitor does */
static void test_end_to_end(void)
{
    medit_init(&ed, table, term_out);
    begin();
    type("wifi jo\tHome \"pass word\"");
    feed('\r');
    check(typed_ev == MEDIT_LINE, "e2e: Enter after a completed, quoted line");
    char line[512];
    snprintf(line, sizeof line, "%s", medit_text(&ed));
    seen[0] = 0;
    check(mcmd_run_line(table, line, &hooks) == 0 && !strcmp(seen, "join:join,Home,pass word;"), "e2e: the line runs");
    begin();
    feed(MEDIT_K_UP);
    check(!strcmp(medit_text(&ed), "wifi join Home \"pass word\""), "e2e: Up brings the line back");
}

int main(void)
{
    test_tokenizer();
    test_dispatch();
    test_help();
    test_editor_basic();
    test_editor_escape();
    test_history();
    test_tab();
    test_end_to_end();
    printf("monitor_cmd: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
