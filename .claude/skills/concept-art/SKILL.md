---
name: concept-art
description: Generate concept art, key art, HUD/UI mockups and creature reference sheets for the dragon engine using a ChatGPT or Gemini subscription (no API key). Use when asked for concept art, a mood board, an art-direction target, a UI mockup, a creature or model reference sheet, or to visualize how something in the game should look before building it.
---

# Concept art

Art direction for this engine is **generated, not commissioned**. The loop is:
generate a target, look at it, implement toward it, screenshot the engine
(`--headless --frames N --screenshot`), compare.

Tool: `image-use` (0.34.7 on 2026-11-14), the renamed `chatgpt-imagegen`. It is
one self-contained Python file at `~/.local/bin/image-use`; the old name is a
shim. Check health with `image-use doctor --profile relay`: it prints every
backend's readiness and which one `auto` would pick. Read `image-use --help`
before a session, because the flags change between versions.

If it is missing (fresh machine), install the one file and authenticate:

```sh
git clone https://github.com/leeguooooo/chatgpt-imagegen /tmp/cgi
install -m 755 /tmp/cgi/image-use ~/.local/bin/image-use
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
image-use "prompt" --backend web --profile relay -o docs/concept/name.png
image-use "prompt" --backend agy -o docs/concept/name.png
image-use "prompt" --backend codex -o docs/concept/name.png
```

The table's model names are from 2026-09. The `codex` backend now takes
`--image-model` (`gpt-image-2.5-sunburst` for precise edits,
`gpt-image-2.5-flare` for speed) and `--quality`, and its driver model
defaults to `gpt-5.6-luna`. A `gemini` backend exists and needs
`IMAGE_USE_GEMINI_PROFILE`; it was not tested here.

## Codex on the work account, against the web backend (2026-11-14)

Every Codex job runs on the work profile. `image-use` reads
`~/.codex/auth.json` whatever `CODEX_HOME` says, so give it a home
directory whose `.codex` links to the work profile:

```sh
D=/tmp/codex-work-home; mkdir -p $D; ln -sfn ~/.codex-work $D/.codex
HOME=$D image-use "prompt" --backend codex -i ref.png --ref-role subject -o out.png
```

One head plate of the hero dragon was generated three ways from the same
prompt and subject reference (`artifacts/showcase-study/imagegen-backends.jpg`):

| Backend | Time | What came back |
|---|---|---|
| `codex`, `--image-model gpt-image-2.5-flare --quality high` | 33 s | The server ran `gpt-image-2-codex` at quality low. 4,740 tokens |
| `codex`, `--image-model gpt-image-2.5-sunburst --quality high` | 52 s | The server ran `gpt-image-2-codex` at quality medium. 4,834 tokens |
| `web` | 76 s | The ChatGPT conversation's image model |

The three pictures are the same quality to the eye: the same eye, scale
plates, horns and throat. The server replaced the requested image model
and quality in both Codex runs, so those two flags choose less than their
names say. The work profile's weekly window read 54% before and after five
Codex images.

Use the Codex backend for a scripted batch. It needs no browser, it has no
leftover-draft failure, and two runs can go in parallel. Use `web` when the
Codex window is short. Two differences to plan for: asked for a "left side
profile", `web` drew the head facing left and Codex drew it facing right,
and two generations of one view are never the same design, so mirror one
side plate for the other side instead of generating both.

## Reference images

A target that must match an engine frame takes the frame as a reference:

```sh
./build/dragon --headless --frames 240 --hide-ui --size 1920x1080 --screenshot /tmp/clean.bmp
sips -s format png -Z 1536 /tmp/clean.bmp --out /tmp/clean.png
image-use "prompt, with the art style last" --backend web --profile relay \
  --composition-ref /tmp/clean.png --size 1536x864 -o docs/concept/name.png
```

`--composition-ref` keeps the framing and replaces the content.
`--style-ref` keeps a look. `-i` with `--ref-role subject` keeps a subject.
One prompt body with three style endings gave the three comparable targets
of `docs/SHOWCASE.md`, 36 seconds each.

If a `web` run with a reference fails with "ChatGPT composer is not empty;
references not uploaded", the previous run left its prompt as a draft.
Clear the draft, then retry:

```sh
chrome-use open https://chatgpt.com/
chrome-use click 'form div.ProseMirror'
chrome-use press "Meta+a"
chrome-use press Backspace
```

Run this between every two `web` generations in a script. Two of three
failed without it (2026-11-14).

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

**If `web` fails with "composer never appeared" while you ARE signed in**, ChatGPT
changed its UI (Oct 2026: the composer lost its `#prompt-textarea` id and became a
`form div.ProseMirror[contenteditable]`). Versions before 0.30 poll the old id. The
tool's own `update` printed success but wrote nothing (2026-10-04) — reinstall by
hand: clone `leeguooooo/chatgpt-imagegen`, `install -m 755` both `image-use` (the
renamed CLI, 0.30.1 verified) and the `chatgpt-imagegen` shim into `~/.local/bin`.
Debug with `chrome-use open https://chatgpt.com/` + `chrome-use snapshot -i` to see
what the page really shows. Under a sandbox that blocks reading Chrome's profile
directory, `doctor` and `--profile auto` crash (`PermissionError`); pass
`--profile relay` to use the already-open, signed-in Chrome without scanning.

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
