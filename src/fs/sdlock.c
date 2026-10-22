/* The SD writer lock: see sdlock.h. */
#include "sdlock.h"

#include "fs/fat.h"
#include "kernel/fiber.h"

#include <stddef.h>
#include <string.h>

static const char *holder;
static void (*holder_pause)(void);

int sd_writer_begin(const char *who, void (*pause)(void))
{
    if (holder) {
        if (fiber_current())            /* the pause hook runs fibers: only from the main loop */
            return -1;
        if (!holder_pause || holder_pause == pause)
            return -1;
        void (*stop)(void) = holder_pause;
        holder = NULL;                  /* the hook may end its own write: the card is free */
        holder_pause = NULL;
        stop();
    }
    holder = who;
    holder_pause = pause;
    return 0;
}

void sd_writer_end(const char *who)
{
    if (!holder || strcmp(holder, who) != 0)
        return;
    holder = NULL;
    holder_pause = NULL;
}

int sd_write_file(const char *who, const char *dir, const char *name, const void *data, size_t len)
{
    const int locked = sd_writer_begin(who, NULL) == 0;
    const int rc = fat_mkdirs(dir) == 0 && fat_write_file(dir, name, data, len) == 0 ? 0 : -1;
    if (locked)
        sd_writer_end(who);
    return rc;
}

const char *sd_writer_who(void)
{
    return holder;
}
