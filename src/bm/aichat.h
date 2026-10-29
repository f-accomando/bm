/*
 * ai_ask for cartridges: the request and the answer of a chat model, in the format that the
 * OpenAI-compatible endpoints share ("POST <ai_url>/chat/completions"): Gemini's compatible
 * endpoint, Groq, OpenRouter, Mistral, Cloudflare Workers AI and tools/bm_agent_relay.py.
 * Plain C, no kernel calls: the kernel and the PC tests use the same code. The request runs
 * in webget.c (web_start_post); the key comes from bm/ai.txt (src/net/aiconf.c) and never reaches Lua.
 */
#ifndef AICHAT_H
#define AICHAT_H

#include <stddef.h>

#define AI_MSGS_MAX         64              /* messages in one request */
#define AI_TEXT_MAX         (192u * 1024u)  /* all the message text of one request */
#define AI_REPLY_MAX        (1024u * 1024u) /* the answer's body cap */
#define AI_TOKENS_DEFAULT   1024
#define AI_TOKENS_LIMIT     32768
#define AI_TIMEOUT_DEFAULT  60000u          /* ms for one read: a model thinks before its first byte */
#define AI_TIMEOUT_LIMIT    180000u         /* the relay runs a whole agent turn */
#define AI_MODEL_MAX        96

typedef struct {
    const char *role;           /* "system", "user" or "assistant" */
    const char *content;
    size_t len;
} ai_msg_t;

typedef struct {
    char *text;                 /* malloc'd, NUL-terminated */
    char finish[24];            /* "stop", "length", ... or "" */
    int prompt_tokens, completion_tokens;   /* -1 when the server did not say */
} ai_reply_t;

/* 0 if role is one a cartridge may send. */
int ai_role_ok(const char *role);

/* 0 if name is a model name the kernel sends: 1 to AI_MODEL_MAX of letters, digits and "-_.:/@". */
int ai_model_ok(const char *name);

/* The endpoint from the url of bm/ai.txt: "<base>/chat/completions" (a base that already
 * ends in "/chat/completions" is kept as it is, a "/" at the end is dropped). 0, or -1 and the
 * reason in err. The https rule is checked by the caller (web_url_check). */
int ai_endpoint(const char *base, char *out, size_t n, char *err, size_t en);

/* The request body (malloc'd JSON, *len bytes): model, the messages in order, max_tokens and,
 * when temperature >= 0, temperature. NULL and the reason in err. */
char *ai_request_json(const char *model, const ai_msg_t *m, int n, int max_tokens, double temperature,
                      size_t *len, char *err, size_t en);

/* The answer: the text of choices[0].message.content, the finish reason and the tokens. For a
 * status that is not 2xx, or a body without that text, -1 and the server's own message in err
 * (error.message, as OpenAI and the compatible APIs write it). ai_reply_free() it. */
int ai_reply_parse(int status, const char *body, size_t len, ai_reply_t *out, char *err, size_t en);
void ai_reply_free(ai_reply_t *r);

#endif
