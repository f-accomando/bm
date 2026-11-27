/* sdk_music_expand: an assembly source of the Shell SDK as the builder gives it to the assembler, with the sound
 * driver in the place of the music block.   sdk_music_expand main.s full.s      (make test-sdk-templates) */
#include "kernel/shell_sdk_music.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 3) {
        puts("usage: sdk_music_expand in.s out.s [snes]");
        return 2;
    }
    static char src[256 * 1024];
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        puts("cannot open the source");
        return 2;
    }
    const size_t n = fread(src, 1, sizeof src - 1, f);
    fclose(f);
    sdk_music_src_t s;
    char err[160];
    if ((argc > 3 ? sdk_music_expand_snes : sdk_music_expand)(src, n, &s, err, sizeof err) != 0) {
        puts(err);
        return 1;
    }
    f = fopen(argv[2], "wb");
    if (!f) {
        puts("cannot write");
        return 2;
    }
    fwrite(s.text ? s.text : src, 1, s.text ? s.len : n, f);
    fclose(f);
    free(s.text);
    return 0;
}
