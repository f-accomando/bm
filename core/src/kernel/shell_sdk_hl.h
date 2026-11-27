/*
 * Shell SDK, Code tab: the colours of the source, bm C and the assembly of the 6502 and the 65C816. Pure model:
 * the tests run on the PC.
 */
#ifndef SHELL_SDK_HL_H
#define SHELL_SDK_HL_H

#include <stdint.h>

enum { SDK_HL_NONE, SDK_HL_ASM, SDK_HL_C };

/* What a character is. */
enum { HL_TEXT, HL_COMMENT, HL_WORD, HL_TYPE, HL_NUMBER, HL_STRING, HL_NAME, HL_MARK, HL_CLASSES };

/* The class of the n characters of a line into cls (the first max of them). inside: the line starts inside a C
 * comment. Returns 1 when the next line does. cls may be NULL (max 0): only the state is wanted. */
int sdk_hl_line(int lang, const char *s, int n, int inside, uint8_t *cls, int max);

#endif /* SHELL_SDK_HL_H */
