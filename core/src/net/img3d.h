/*
 * A picture becomes a 3D model through an image-to-3D service (M30): the
 * console sends the picture, asks how the job goes, downloads the .glb
 * (glb.c turns it into a model of the MESH section). The services are a
 * table: each one knows its address, the name of its key in bm/config.txt
 * and how its three calls look; Meshy (meshy.ai) is the first. Plain C
 * over http.c: the kernel and the PC tests (a fake service there).
 */
#ifndef IMG3D_H
#define IMG3D_H

#include <stddef.h>
#include <stdint.h>

typedef struct img3d_provider img3d_provider_t;

/* The service by name ("meshy"), or NULL. */
const img3d_provider_t *img3d_provider(const char *name);
/* The names, in order; NULL after the last. */
const char *img3d_provider_name(int i);
/* The name of the key in bm/config.txt (meshy_key). */
const char *img3d_key_name(const img3d_provider_t *p);
/* Tests: the service at another address (http://127.0.0.1:port). */
void img3d_set_base(const img3d_provider_t *p, const char *base);

/* Starts a job on the picture (PNG or JPEG bytes), or on a picture at an
 * https URL (image NULL). polycount: the triangles asked of the service
 * (0: its default). The job's id goes to task. 0, or -1 with err. */
int img3d_start(const img3d_provider_t *p, const char *key, const uint8_t *image, size_t len, const char *url,
                int polycount, char *task, size_t tasklen, char *err, size_t errlen);
/* How the job goes: 0 still running (*progress 0..100), 1 done (the .glb's
 * address in model_url), -1 failed or unreachable (err). */
int img3d_status(const img3d_provider_t *p, const char *key, const char *task, int *progress, char *model_url,
                 size_t urllen, char *err, size_t errlen);
/* The .glb (malloc'd, at most max bytes): 0, or -1 with err. */
int img3d_download(const char *url, size_t max, uint8_t **data, size_t *len, char *err, size_t errlen);

#endif
