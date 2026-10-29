/*
 * api_jobs.c: pure jobs for the games (docs/api/jobs.md): job_run, job_poll, job_cancel, job_info.
 * A job is a function of the kernel (kernel/jobs.c). On the RGB30 with cores=4 it runs on a worker core; elsewhere it
 * runs at once on core 0. The answer is the same.
 */
#include "kernel/jobs.h"
#include "rt_internal.h"
#include <string.h>
#include "lauxlib.h"
#include "lua.h"

/* job_run(name, data) -> id, or nil and the reason. data is a string of bytes (string.pack). */
int l_job_run(lua_State *L)
{
    size_t n = 0;
    const char *name = luaL_checkstring(L, 1);
    const char *data = luaL_checklstring(L, 2, &n);
    const int k = jobs_kernel_id(name);
    const int id = k < 0 ? -1 : jobs_start(k, (const uint8_t *)data, n);
    if (id > 0) {
        lua_pushinteger(L, id);
        return 1;
    }
    lua_pushnil(L);
    lua_pushstring(L, id == -1 ? "no such job" : id == -2 ? "the data is too long" : "too many jobs");
    return 2;
}

/* job_poll(id) -> "run" while it works, "done" and the answer string, "fail" (the data was not right), or "none" (no such job:
 * a wrong id, or the answer was read). The call that says "done" or "fail" frees the job. */
int l_job_poll(lua_State *L)
{
    const int id = (int)luaL_checkinteger(L, 1);
    const uint8_t *out = NULL;
    size_t n = 0;
    switch (jobs_poll(id, &out, &n)) {
    case JOB_QUEUED:
    case JOB_RUN:
        lua_pushstring(L, "run");
        return 1;
    case JOB_DONE:
        lua_pushstring(L, "done");
        lua_pushlstring(L, (const char *)out, n);
        return 2;
    case JOB_FAIL:
        lua_pushstring(L, "fail");
        return 1;
    default:
        lua_pushstring(L, "none");
        return 1;
    }
}

/* job_cancel(id): the answer is not wanted */
int l_job_cancel(lua_State *L)
{
    jobs_cancel((int)luaL_checkinteger(L, 1));
    return 0;
}

/* job_info() -> the worker cores that can take a job (0: a job runs at once on core 0), the jobs that ran on a worker so far */
int l_job_info(lua_State *L)
{
    lua_pushinteger(L, jobs_workers());
    lua_pushinteger(L, (lua_Integer)jobs_on_workers());
    return 2;
}
