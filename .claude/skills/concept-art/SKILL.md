---
name: concept-art
description: Generate concept art, key art, HUD/UI mockups and creature reference sheets for the dragon engine using a ChatGPT or Gemini subscription (no API key). Use when asked for concept art, a mood board, an art-direction target, a UI mockup, a creature or model reference sheet, or to visualize how something in the game should look before building it.
---

# Concept art

Art direction for this engine is **generated, not commissioned**. The loop is:
generate a target, look at it, implement toward it, screenshot the engine
(`--headless --frames N --screenshot`), compare.

Tool: `chatgpt-imagegen` — a single self-contained Python file, installed at
`~/.local/bin/chatgpt-imagegen` on the machine this was set up on. Check health
with `chatgpt-imagegen doctor`: it prints every backend's readiness and which
one `auto` would pick.

If it is missing (fresh machine), install the one file and authenticate:

```sh
git clone https://github.com/leeguooooo/chatgpt-imagegen /tmp/cgi
install -m 755 /tmp/cgi/chatgpt-imagegen ~/.local/bin/chatgpt-imagegen
codex login          # OAuth, writes ~/.codex/auth.json — no API key involved
```

`--backend agy` additionally needs the Antigravity CLI (`agy`), and
`--backend web` needs `chrome-use` plus its Chrome extension — see the blast
radius warning below before installing that one. `doctor` names what is absent.

## Which backend, and why it matters

Three backends, **three independent quotas**. Pick by what the image is *for*,
not by which is "better" — they lose to each other in different ways.

| Want | Use | Model |
|---|---|---|
| Key art, mood, lighting/atmosphere target | `--backend web` | GPT Image 2 |
| HUD layout, flat diagram, orthographic reference sheet | `--backend agy` | Nano Banana 2 |
| GPT Image 2 when `web` is rate-limited | `--backend codex --model gpt-5.6-luna` | GPT Image 2 |

```sh
chatgpt-imagegen "prompt" --backend web -o docs/concept/name.png
chatgpt-imagegen "prompt" --backend agy -o docs/concept/name.png
chatgpt-imagegen "prompt" --backend codex --model gpt-5.6-luna -o docs/concept/name.png
```

**GPT Image 2 ignores style specifications it considers a downgrade.** Asked for
"clean flat vector style" for a HUD mockup it returned a photoreal cinematic
marketing shot with invented lore (kingdom names, a tagline) — beautiful, and
not a UI spec. Nano Banana 2 obeyed the same instruction literally. So GPT is
for how the game should *feel* and NB2 for anything you need to *build from*.
Text rendering is flawless in both; NB2's reference sheet came back with correct
side/front/top views and a metre scale bar, which is what makes it modelable.

GPT renders larger (1672x941 vs 1376x768) and much richer, but invents scenery —
it will add castles and waterfalls to a valley that has none. NB2 is cooler and
more restrained, and happens to sit closer to what this engine actually renders.
NB2 also watermarks text-to-image output and treats `--size` as a hint only.

**`--backend web` is still the default GPT path**, because it reaches the cheap
ChatGPT conversation bucket (~40-50 prompts per rolling 3h on Plus) and costs no
Codex usage at all. `--backend codex` bills metered Codex usage and OpenAI's own
docs say image generation burns those limits **3-5x faster** than a normal turn,
so it is the *second* GPT path, for when `web` is rate-limited or its Chrome
relay is down — not the first.

When you do reach for it, **pass `--model gpt-5.6-luna`**. The tool's `--model`
default is `gpt-5.5`, and the flag selects which Codex model orchestrates the
`image_generation` tool call — the image itself is GPT Image 2 either way, so
the cheapest listed model is enough and Luna is the cheapest. Only `gpt-5.6-*`,
`gpt-6-astra` and `gpt-5.5` exist as slugs; check
`~/.codex/models_cache.json` if a name is rejected. The backend also rejects
models when the `version` header looks stale, which the tool works around by
never sending a version below its own floor.

## Quotas

Don't try to precompute them. The 429 is the meter — it names the model and its
reset time. Roughly: `web` ~40-50 prompts/3h; `agy` compute-based on a Google AI
subscription (no published image count, and Nano Banana **Pro** is not reachable
from the CLI — there is no model flag, and only NB2 is offered); `codex` draws
on the shared 5-hour and weekly Codex windows, which `codex` itself reports in
its status line, so spend it in single images rather than in sweeps.

## The browser dependency, and its blast radius

`--backend web` requires `chrome-use` — a fork of vercel-labs/agent-browser by
the *same author* as chatgpt-imagegen, so tool and dependency are one trust
domain. The official upstream is **not** a drop-in: it has no `daemon status`
(the relay probe) and only `click <sel>`, no coordinate clicking. Both are
load-bearing here. Verified, don't re-litigate it.

It drives your logged-in Chrome through a native-messaging extension, which
means access to **every authenticated session in that profile**. Prefer running
it against a Chrome profile signed into nothing but ChatGPT. `--backend agy` and
`--backend codex` need none of this.

Each `web` run auto-deletes its ChatGPT conversation, so it leaves no history.
If it warns `none of ['Instant','Auto'] in the picker`, the model picker labels
differ on this account — pin one via `CHATGPT_IMAGEGEN_WEB_MODEL`.

## Conventions

Generated art belongs in `docs/concept/`, named for what it is
(`hud-layout.png`, `valley-dawn.png`), not `output1.png`. These are 1.5-2.5 MB
each — commit the few that actually set direction, not every experiment.

Both repos hit undocumented internal endpoints that OpenAI or Google can change
without notice. Fine for private iteration; never make anything in the build
depend on one.
