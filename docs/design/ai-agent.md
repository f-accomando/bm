# bm Agent: a cloud AI model on bm

Roadmap line: API-13 ([ROADMAP](../ROADMAP.md)).

bm runs on bare metal. No provider ships an SDK or an agent program for it. A local model does not fit
the Pi Zero W (512 MB) or the RGB30. This page says how bm uses cloud models for free or through
a subscription, and which provider rules apply. Research date: 29 Oct 2026. Check the providers' pages
again before you change this design: their free tiers and terms change often.

## What bm has

- WiFi, lwIP, mbedTLS 3.6 (TLS 1.2, ECDSA P-256 and P-384, RSA) and `http.c` (GET, POST, PUT, extra headers).
- The web API for cartridges (`web_get`): https only, the roots in `bm/ca.pem`, a pause per host, a body cap.
- `ai_ask`, `ai_reply`, `ai_info` (spec 6, [docs/api/system.md](../api/system.md), "A chat model"). The kernel sends
  `POST <url>/chat/completions` in the OpenAI-compatible format. It adds the key itself. Lua never reads the key.
- `bm/ai.txt`: the AI settings (`url=`, `model=`, `key=`), apart from `bm/config.txt`, which stays for the system.
  The file is protected (`src/net/aiconf.c`): no Lua access, the network transfer refuses it, the kernel writes it only
  after its own question (`ai_setup`, `ai_pair_finish`).
- `tools/bm_agent_relay.py`: the same API on the user's PC, answered by the official agent program.
- `carts/agent` (bm Agent): the chat app. It saves a file that the model proposes only after the user says yes.

## Two ways to a model

```
 bm console                          internet / LAN
 ----------                          --------------
 bm Agent --ai_ask--> kernel --HTTPS POST + Bearer ai_key--> free API (Groq, Gemini, OpenRouter, Mistral)
                                 \
                                  --HTTPS POST + Bearer token--> bm_agent_relay.py (your PC)
                                                                   | stdin
                                                                   v
                                                          claude -p / codex exec / gemini
                                                          (official, unmodified, your own login)
```

### 1. A free API key (no PC)

Use a provider with an OpenAI-compatible endpoint and a free tier. In bm Agent, type `/setup` (it starts by itself the
first time), choose the provider and type the key. The kernel asks you, then saves `bm/ai.txt`.

| Provider | `url` | Free tier (Oct 2026, check it) | Notes |
|---|---|---|---|
| Google Gemini | `https://generativelanguage.googleapis.com/v1beta/openai` | Flash models, a few requests a minute, a daily cap; Pro models need billing | Free-tier prompts can train Google's models. Commercial use of the free tier is excluded in the EU/EEA, UK and Switzerland. Key from AI Studio |
| Groq | `https://api.groq.com/openai/v1` | about 30 requests a minute and 1000 a day per model (gpt-oss, Qwen) | No card |
| OpenRouter | `https://openrouter.ai/api/v1` | models whose id ends in `:free`: 20 a minute, 50 a day (1000 after a 10 USD top-up) | The free model list changes. Failed requests count |
| Mistral | `https://api.mistral.ai/v1` | the Experiment plan | You must accept that your data trains their models |

Cerebras has no free tier without a card any more. One source says that GitHub Models has shut down: do not rely on it.

### 2. A subscription through the relay (your PC)

A Claude, ChatGPT or Google subscription has no API key. Its quota works only in the provider's own programs.
Run that program on your own PC or server, signed in with your own account. `bm_agent_relay.py` gives each
request from the console to one run of the program.

1. On the PC: `python3 tools/bm_agent_relay.py cert --ip <the PC's LAN address>`.
2. On the PC: `python3 tools/bm_agent_relay.py serve --ip <the same address> --backend claude` (or `codex`, `gemini`, `cmd`).
   It prints the pairing code, 20 characters.
3. In bm Agent: `/setup`, `5` (Relay), the PC's address, the code. The kernel fetches the relay's CA on the LAN, checks it
   against the code, asks you, and saves the CA in `bm/ca-user.pem` and the settings in `bm/ai.txt`.

The pairing code: the first 12 characters are the CA's fingerprint (60 bits of SHA-256 of its DER, Crockford base32).
Another PC on the LAN cannot answer with its own CA: it would need one with the same fingerprint. The last 8 characters
are the relay's token. The relay refuses every request for 10 minutes after 20 wrong tokens. Updates rewrite
`bm/ca.pem`; they never touch `bm/ca-user.pem`.

The relay runs `claude -p --output-format json --tools "" --no-session-persistence` in an empty folder.
Do not add `--bare`: it reads only `ANTHROPIC_API_KEY`, never the subscription login.

## Provider rules (read them; they decide the design)

- **Anthropic.** OAuth (the Free, Pro and Max sign-in) is for Claude Code and Anthropic's own apps. A third-party
  program must not use those credentials, must not offer the Claude sign-in, and must not route requests through
  a subscription for other users. A developer product uses an API key. The rules allow a user to sign in to the
  unmodified Claude Code program with their own subscription. Subscription limits assume "ordinary, individual usage"
  ([legal and compliance](https://code.claude.com/docs/en/legal-and-compliance)). Since 4 Apr 2026 a subscription
  does not cover third-party tools. So: bm never holds a Claude token. The relay runs the official program, for the
  user only.
- **OpenAI.** A ChatGPT plan works in Codex and in the partners of "Sign in with ChatGPT" (DevDay 2026). The terms for
  a private program that drives `codex exec` are not public. Use the relay's `codex` backend only for your own use.
- **Google.** The free API tier is in AI Studio (an API key). Gemini CLI has its own login and quota. Use it
  through the relay for your own use only.

Rules for this design:

- Keep the relay for one person. Do not share its token, its address or its quota.
- Do not copy a provider's sign-in token to the console or into the relay.
- Do not call a private or reverse-engineered endpoint of a chat website.
- Respect each free tier's limits. The kernel waits 1 s per host. The relay waits 2 s between requests (`--gap`).

## Security

- The key lives in `bm/ai.txt`, not in `bm/config.txt`. No Lua call and no network operation reads or writes the file.
  The kernel copies the key into the request's header lines and wipes the copy when the request ends (`webget.c`, `free_post`).
- A cartridge changes the settings only through `ai_setup` / `ai_pair_finish`, and the kernel asks the player first. A new
  server needs its key in the same call, so a cartridge cannot send the saved key to another server.
- A cartridge cannot choose the server or read the key. A POST follows no redirect, so the key goes to `ai_url`'s host only.
- The relay checks the token in constant time. It keeps a usage log (time, sizes, status), never the text.
- The relay's tools are off (`claude --tools ""`) or read-only in an empty folder (`codex --sandbox read-only`).
  The console's agent writes files itself, into `bm/web` only, after the user says yes.

## Sources

- Anthropic, Claude Code legal and compliance: https://code.claude.com/docs/en/legal-and-compliance
- The Register, 20 Feb 2026, Anthropic clarifies ban on third-party tool access to Claude
- OpenRouter, Free LLM API in 2026: https://openrouter.ai/blog/tutorials/free-llm-apis-compared/
- Gemini API free tier and rate limits: https://aistudio.google.com/rate-limit (per project)
- Neowin, DevDay 2026 (Sign in with ChatGPT partners)
