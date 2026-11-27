/*
 * Shell SDK: the colours of the source (see shell_sdk_hl.h). Original code.
 */
#include "shell_sdk_hl.h"

#include <string.h>

static int is_name(int c) { return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_digit(int c) { return c >= '0' && c <= '9'; }

/* 1 when the word of len characters is in the list of words separated by spaces; fold: any case. */
static int in_list(const char *list, const char *w, int len, int fold)
{
    for (const char *p = list; *p;) {
        const char *e = strchr(p, ' ');
        const int n = e ? (int)(e - p) : (int)strlen(p);
        if (n == len) {
            int i = 0;
            while (i < n && p[i] == (fold && w[i] >= 'A' && w[i] <= 'Z' ? w[i] + 32 : w[i]))
                i++;
            if (i == n)
                return 1;
        }
        p += n + (e != NULL);
    }
    return 0;
}

static const char c_words[] = "if else while do for break continue return asm tiles true false";
static const char c_types[] = "void byte sbyte word int char bool unsigned signed short const static";
static const char c_names[] =
    "start update main wait_frame held pressed background background_palette sprite_palette sprite sprite_style "
    "hide_sprite random peek poke frame pad pad_new blank A B SELECT START UP DOWN LEFT RIGHT BLACK DARK_GRAY GRAY "
    "WHITE RED ORANGE YELLOW GREEN LIME CYAN SKY BLUE NAVY PURPLE PINK BROWN FLIP_X FLIP_Y BEHIND";
static const char mnemonics[] =
    "adc and asl bcc bcs beq bit bmi bne bpl brk bvc bvs clc cld cli clv cmp cpx cpy dec dex dey eor inc inx iny "
    "jmp jsr lda ldx ldy lsr nop ora pha php pla plp rol ror rti rts sbc sec sed sei sta stx sty tax tay tsx txa "
    "txs tya bra brl cop jml jsl mvn mvp pea pei per phb phd phk phx phy plb pld plx ply rep rtl sep stp stz tcd "
    "tcs tdc trb tsb tsc txy tyx wai wdm xba xce";

static void mark(uint8_t *cls, int max, int from, int to, int c)
{
    for (int i = from; i < to && i < max; i++)
        cls[i] = (uint8_t)c;
}

int sdk_hl_line(int lang, const char *s, int n, int inside, uint8_t *cls, int max)
{
    if (!cls)
        max = 0;
    mark(cls, max, 0, n, HL_TEXT);
    if (lang == SDK_HL_NONE)
        return 0;
    int i = 0, first = 1;                       /* first: nothing but spaces so far */
    while (i < n) {
        const char c = s[i];
        const char d = i + 1 < n ? s[i + 1] : 0;
        if (inside) {
            const int from = i;
            while (i < n && !(s[i] == '*' && i + 1 < n && s[i + 1] == '/'))
                i++;
            if (i < n) {
                i += 2;
                inside = 0;
            }
            mark(cls, max, from, i, HL_COMMENT);
            continue;
        }
        if (c == ' ' || c == '\t') {
            i++;
            continue;
        }
        if ((lang == SDK_HL_ASM && c == ';') || (lang == SDK_HL_C && c == '/' && d == '/')) {
            mark(cls, max, i, n, HL_COMMENT);
            break;
        }
        if (lang == SDK_HL_C && c == '/' && d == '*') {
            mark(cls, max, i, i + 2, HL_COMMENT);
            i += 2;
            inside = 1;
            continue;
        }
        const int from = i;
        int what = HL_TEXT;
        if (c == '"' || c == '\'') {
            for (i++; i < n && s[i] != c; i++)
                i += s[i] == '\\';
            i = i < n ? i + 1 : n;
            what = HL_STRING;
        } else if (lang == SDK_HL_ASM && (c == '$' || c == '%' || c == '#')) {
            for (i++; i < n && (is_name(s[i]) || is_digit(s[i]) || s[i] == '$' || s[i] == '%'); i++)
                ;
            what = HL_NUMBER;
        } else if (is_digit(c)) {
            while (i < n && (is_digit(s[i]) || is_name(s[i])))
                i++;
            what = HL_NUMBER;
        } else if (lang == SDK_HL_ASM && (c == '.' || c == '@') && is_name(d)) {
            for (i++; i < n && (is_name(s[i]) || is_digit(s[i])); i++)
                ;
            what = c == '.' && !(i < n && s[i] == ':') ? HL_WORD : HL_MARK;
        } else if (is_name(c)) {
            while (i < n && (is_name(s[i]) || is_digit(s[i])))
                i++;
            const int len = i - from;
            if (lang == SDK_HL_C)
                what = in_list(c_words, s + from, len, 0) ? HL_WORD : in_list(c_types, s + from, len, 0) ? HL_TYPE :
                       in_list(c_names, s + from, len, 0) ? HL_NAME : HL_TEXT;
            else if (first && i < n && s[i] == ':')
                what = HL_MARK;
            else if (in_list(mnemonics, s + from, len, 1))
                what = HL_TYPE;
        } else if (lang == SDK_HL_C && c == '#') {
            for (i++; i < n && is_name(s[i]); i++)
                ;
            what = HL_MARK;
        } else
            i++;
        mark(cls, max, from, i, what);
        first = 0;
    }
    return lang == SDK_HL_C ? inside : 0;
}
