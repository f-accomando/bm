#include "v3d_async.h"

static inline uint32_t read32(uint32_t addr) {
    return *(volatile uint32_t *)addr;
}
static inline void write32(uint32_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}

static v3d_job_t *current_job = NULL;

void v3d_async_init(void) {
    write32(V3D_INTDIS, 0xFFFFFFFF);
    write32(V3D_INTCTL, 0xFFFFFFFF);
    write32(V3D_INTENA, V3D_INT_FLDONE | V3D_INT_FRDONE | V3D_INT_OUTOMEM);
    current_job = NULL;
}

void v3d_submit(v3d_job_t *job) {
    if (!job) return;
    job->state = JOB_BINNING;
    job->completed = false;
    current_job = job;
    write32(V3D_CT0CA, job->bin_start);
    write32(V3D_CT0EA, job->bin_end);
}

void v3d_irq_handler(void) {
    uint32_t status = read32(V3D_INTCTL);
    write32(V3D_INTCTL, status);
    if (!current_job) return;

    if (status & V3D_INT_OUTOMEM) {
        current_job->state = JOB_ERROR;
        return;
    }
    if (status & V3D_INT_FLDONE) {
        if (current_job->state == JOB_BINNING) {
            current_job->state = JOB_RENDERING;
            write32(V3D_CT1CA, current_job->render_start);
            write32(V3D_CT1EA, current_job->render_end);
        }
    }
    if (status & V3D_INT_FRDONE) {
        if (current_job->state == JOB_RENDERING) {
            current_job->state = JOB_DONE;
            current_job->completed = true;
            current_job = NULL;
        }
    }
    if (read32(V3D_CT0CS) & V3D_CTERR || read32(V3D_CT1CS) & V3D_CTERR) {
        if (current_job) {
            current_job->state = JOB_ERROR;
            current_job->completed = true;
            current_job = NULL;
        }
    }
}

void v3d_wait(v3d_job_t *job) {
    if (!job) return;
    while (!job->completed) {
        __asm__ volatile("wfi");
    }
}

bool v3d_is_done(v3d_job_t *job) {
    return job && job->completed;
}
