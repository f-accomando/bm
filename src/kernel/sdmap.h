#ifndef SDMAP_H
#define SDMAP_H
#include <stdint.h>
#define SDMAP_DIRS  16          /* folders listed (depth <= 2) */
#define SDMAP_FILES 24          /* files the menu cannot show, the largest */
typedef struct { char path[32]; uint32_t files, kb; } sdmap_dir_t;       /* files directly in it; kb = their bytes / 1024 */
typedef struct { char path[64]; uint32_t bytes; } sdmap_file_t;           /* path cut to its last 63 characters */
typedef struct {
    uint64_t total, free, kernel, bm, bme, other;                        /* bytes */
    int ndirs, nfiles;
    sdmap_dir_t dirs[SDMAP_DIRS];
    sdmap_file_t files[SDMAP_FILES];                                      /* largest first */
} sdmap_t;
/* core (sdmap_core.c, no kernel services): */
void sdmap_core_reset(void);                    /* start again from the root */
int  sdmap_core_step(unsigned budget);          /* at most `budget` directory entries (or FAT sectors); 1 while more, 0 done, -1 error */
const sdmap_t *sdmap_core_result(void);         /* the numbers so far */
int  sdmap_core_permille(void);                 /* progress 0..1000 */
#endif
