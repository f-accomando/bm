#ifndef SDMAP_H
#define SDMAP_H
#include <stdint.h>
#define SDMAP_DIRS  16          /* folders listed (depth <= 2) */
#define SDMAP_FILES 24          /* files the menu cannot show, the largest */
typedef struct { char path[32]; uint32_t files, kb; } sdmap_dir_t;       /* files directly in it; kb = their bytes / 1024 */
typedef struct { char path[64]; uint32_t bytes; } sdmap_file_t;           /* path cut to its last 63 characters */
typedef struct {
    uint64_t total, free, kernel, bm, bme, glb, bmm, other;                        /* bytes */
    int ndirs, nfiles;
    sdmap_dir_t dirs[SDMAP_DIRS];
    sdmap_file_t files[SDMAP_FILES];                                      /* largest first */
} sdmap_t;
/* core (sdmap_core.c, no kernel services): */
void sdmap_core_reset(void);                    /* start again from the root */
int  sdmap_core_step(unsigned budget);          /* at most `budget` directory entries (or FAT sectors); 1 while more, 0 done, -1 error */
const sdmap_t *sdmap_core_result(void);         /* the numbers so far */
int  sdmap_core_permille(void);                 /* progress 0..1000 */

/* service (sdmap.c): the scan as a background job of the menu */
enum { SDMAP_NONE, SDMAP_CACHED, SDMAP_SCANNING, SDMAP_DONE };
void sdmap_request(void);                       /* start (or keep going) if not done this boot; cheap to call every frame */
void sdmap_tick(uint32_t until);                /* the job's slice: runs until timer_ticks() reaches `until`, back at once if idle */
void sdmap_stop(void);                          /* before a game or a tool starts: the job ends, the numbers stay */
int  sdmap_state(void);
const sdmap_t *sdmap_result(void);              /* the numbers (empty until the first scan or the cache) */
int  sdmap_permille(void);                      /* progress 0..1000 */
#endif
