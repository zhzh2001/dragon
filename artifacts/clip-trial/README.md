# Keyframed brake-bank clip trial (2026-11-04)

`docs/ANIMATION.md`, "A keyframed overlay: the brake-bank trial". The clip
was keyframed by Codex (gpt-6-astra) with `tools/clips/make_brake_bank.py`;
`tools/clips/README.md` has its record. The full-size preview frames (43 MB)
stay on the `clip-trial` branch.

- `brake_bank_l.contact.jpg`, `brake_bank_r.contact.jpg`: Codex's preview,
  the clip alone on the bind pose, eight times through the loop from the
  side, the front and below.
- `brake_bank_l.traces.png`, `brake_bank_r.traces.png`: each joint's offset
  from rest through the loop.
- `check.txt`: `tools/clips/check_clip.py`'s table.
- `braking-turn-clip.gif`: in the game, Embercrest in a held braking turn
  (`--input 0,0.6,0,0,0,1 --inspect 160 16 -4`): the procedural pose with
  limb physics and work on the left, plus the clip on the right.
- `studio-brake-clip-x3.jpg`: the studio brake with the clip at 0 (top) and
  at three times its size (bottom), to see its directions.
