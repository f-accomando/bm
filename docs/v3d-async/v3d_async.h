#ifndef V3D_ASYNC_H
#define V3D_ASYNC_H

#include <stdint.h>
#include <stdbool.h>

#define V3D_BASE  0x20C00000

#define V3D_INTCTL   (V3D_BASE + 0x030)
#define V3D_INTENA   (V3D_BASE + 0x034)
#define V3D_INTDIS   (V3D_BASE + 0x038)
#define V3D_CT0CS    (V3D_BASE + 0x100)
#define V3D_CT1CS    (V3D_BASE + 0x104)
#define V3D_CT0EA    (V3D_BASE + 0x108)
#define V3D_CT1EA    (V3D_BASE + 0x10c)
#define V3D_CT0CA    (V3D_BASE + 0x110)
#define V3D_CT1CA    (V3D_BASE + 0x114)
#define V3D_PCS      (V3D_BASE + 0x130)
#define V3D_BFC      (V3D_BASE + 0x134)
#define V3D_RFC      (V3D_BASE + 0x138)

#define V3D_INT_FRDONE   (1u << 0)
#define V3D_INT_FLDONE   (1u << 1)
#define V3D_INT_OUTOMEM  (1u << 2)
#define V3D_INT_SPILLUSE (1u << 3)
#define V3D_CTRUN        (1u << 5)
#define V3D_CTERR        (1u << 3)

typedef enum {
    JOB_IDLE = 0,
    JOB_BINNING,
    JOB_RENDERING,
    JOB_DONE,
    JOB_ERROR
} v3d_job_state_t;

typedef struct {
    uint32_t bin_start;
    uint32_t bin_end;
    uint32_t render_start;
    uint32_t render_end;
    v3d_job_state_t state;
    volatile bool completed;
} v3d_job_t;

void v3d_async_init(void);
void v3d_submit(v3d_job_t *job);
void v3d_irq_handler(void);
void v3d_wait(v3d_job_t *job);
bool v3d_is_done(v3d_job_t *job);

#endif
