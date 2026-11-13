/*
 * Hybrid symbolic code synthesizer (bm_lisp -> .b / Lua).
 * Combines assist.c intent + slot extraction with Lisp AST composition.
 */
#ifndef AI_SYNTH_H
#define AI_SYNTH_H

#include <stddef.h>

/* Synthesize code for `target` ("bml" or "lua", or NULL to auto-detect).
 * `query` can be natural language (IT/EN) or an S-expression starting with '('.
 * `entry_id` is an optional best-hit id from ai_ask() (may be NULL or "").
 * Returns 0 on success (code written to `code_out`, Lisp goal to `goal_out`),
 * -1 if no rule matched or on Lisp error (message in `err_out`). */
int bm_synth_run(const char *query, const char *entry_id, const char *target,
                 char *code_out, size_t code_cap,
                 char *goal_out, size_t goal_cap,
                 char *err_out, size_t err_cap);

#endif /* AI_SYNTH_H */
