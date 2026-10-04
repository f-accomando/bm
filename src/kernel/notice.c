#include "notice.h"
#include "net/netxfer.h"
#include "lib/printf.h"

int notice_now(char *title, char *detail, int *progress)
{
    uint32_t got = 0, size = 0;
    int secs = 0;
    switch (netxfer_kernel_state(&got, &size, &secs)) {
    case NETXFER_K_RECEIVING:
        ksnprintf(title, NOTICE_LEN, "Receiving a new kernel");
        if (size && got >= size)
            ksnprintf(detail, NOTICE_LEN, "Writing it on the SD card...");
        else
            ksnprintf(detail, NOTICE_LEN, "%lu of %lu KiB", got / 1024, size / 1024);
        *progress = size ? (int)((uint64_t)got * 1000 / size) : 0;
        return 1;
    case NETXFER_K_RESTART:
        if (secs)
            ksnprintf(title, NOTICE_LEN, "Restarting in %d", secs);
        else
            ksnprintf(title, NOTICE_LEN, "Restarting...");
        ksnprintf(detail, NOTICE_LEN, "The new kernel is on the SD card");
        *progress = -1;
        return 1;
    }
    return 0;
}
