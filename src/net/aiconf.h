/*
 * The AI settings of ai_ask (bm Agent): bm/ai.txt, apart from bm/config.txt (the system's settings).
 *   url=<the OpenAI-compatible base or endpoint>
 *   model=<the model>
 *   key=<the provider's key or the relay's token>
 * The file is protected: no Lua call reads or writes it, and the network transfer (netxfer.c) refuses it.
 * The kernel writes it only after the player says yes to its own question (ai_setup, ai_pair_finish);
 * the user may also write it on the SD card from a PC. Keys ai_url, ai_model and ai_key found in
 * bm/config.txt (the first version) move here at the first read.
 */
#ifndef AICONF_H
#define AICONF_H

#include "aipair.h"                     /* AI_USER_CA */

#include <strings.h>

#define AI_CONF_FILE "ai.txt"            /* in /bm */

/* "url", "model" or "key": the value, or NULL when it is not set. */
const char *ai_conf_get(const char *name);

/* Saves the three values (NULL keeps a value as it is). 0, or -1 if the card was not written. */
int ai_conf_set(const char *url, const char *model, const char *key);

/* 1 if path names a file that only the console's own questions may write: bm/ai.txt or bm/ca-user.pem
 * (any folder, any case: the transfer refuses the name). Inline: netxfer.c and its PC test use this code. */
static inline int ai_protected_name(const char *path)
{
    const char *last = path;
    for (const char *c = path; *c; c++)
        if (*c == '/' || *c == '\\')
            last = c + 1;
    return strcasecmp(last, AI_CONF_FILE) == 0 || strcasecmp(last, AI_USER_CA) == 0;
}

#endif
