#!/usr/bin/env python3
"""Read the Codex usage windows, so a codex task can be costed before it runs.

    tools/codex_usage.py            # human-readable
    tools/codex_usage.py --json     # machine-readable

One substantive rigging task costs ~85 k tokens, which is roughly 45% of the
5-hour window (see the delegation rule in CLAUDE.md), so "is there room" is a
real question and the answer is not free to guess. Nothing local knows it:
`codex exec` prints token counts but no rate-limit data, nothing under
`~/.codex` carries a live figure, and even a do-nothing `codex exec` burns
7.3 k tokens of prompt overhead. The only source is the account page, so this
reads it out of the logged-in browser through `chrome-use`.

Requires Chrome running and signed in to ChatGPT. The page renders in whatever
language the account is set to, so the labels are matched in English *and*
Chinese, and the percentages are located structurally -- the number follows its
heading -- rather than by scraping a fixed layout.
"""
import argparse
import json
import re
import subprocess
import sys

URL = 'https://chatgpt.com/codex/cloud/settings/analytics'
SESSION = 'cxusage'

# The page lays each window out as heading, then the number, then a word for
# "remaining", then the reset time. Match the heading in either language.
FIVE_HOUR = re.compile(r'(5\s*小时|5[-\s]?hour)', re.I)
WEEKLY = re.compile(r'(每周|weekly)', re.I)
PERCENT = re.compile(r'^(\d+(?:\.\d+)?)%$')
RESET = re.compile(r'(?:重置时间|resets?)\s*[:：]?\s*(.+)', re.I)


def cu(*args, timeout=240):
    r = subprocess.run(['chrome-use', *args, '--session', SESSION],
                       capture_output=True, text=True, timeout=timeout)
    if r.returncode != 0:
        raise SystemExit(f'chrome-use {args[0]} failed: {r.stderr.strip() or r.stdout.strip()}')
    return r.stdout


def page_text():
    cu('open', URL)
    # The figures arrive with a client-side fetch; poll rather than sleep once.
    for _ in range(12):
        out = cu('eval', '(() => JSON.stringify(document.body.innerText))()')
        line = out.strip().splitlines()[-1]
        try:
            text = json.loads(json.loads(line))
        except (json.JSONDecodeError, TypeError):
            text = ''
        if FIVE_HOUR.search(text) and PERCENT.search('\n'.join(
                l.strip() for l in text.splitlines())) is not None:
            return text
        if FIVE_HOUR.search(text) and '%' in text:
            return text
    raise SystemExit('usage figures never appeared; is Chrome signed in to ChatGPT?')


def after(lines, index, pattern, span=6):
    """First line within `span` after `index` matching `pattern`."""
    for line in lines[index + 1:index + 1 + span]:
        m = pattern.match(line) or pattern.search(line)
        if m:
            return m.group(1)
    return None


def parse(text):
    lines = [l.strip() for l in text.splitlines() if l.strip()]
    result = {'five_hour_remaining_pct': None, 'five_hour_reset': None,
              'weekly_remaining_pct': None, 'weekly_reset': None}
    for i, line in enumerate(lines):
        if FIVE_HOUR.search(line) and result['five_hour_remaining_pct'] is None:
            pct = after(lines, i, PERCENT)
            if pct is not None:
                result['five_hour_remaining_pct'] = float(pct)
                result['five_hour_reset'] = after(lines, i, RESET, span=8)
        elif WEEKLY.search(line) and result['weekly_remaining_pct'] is None:
            pct = after(lines, i, PERCENT)
            if pct is not None:
                result['weekly_remaining_pct'] = float(pct)
                result['weekly_reset'] = after(lines, i, RESET, span=8)
    return result


ap = argparse.ArgumentParser()
ap.add_argument('--json', action='store_true', help='print JSON instead of prose')
# ~85 k tokens a task against a window measured at ~45% per task.
ap.add_argument('--task-cost-pct', type=float, default=45.0,
                help='share of the 5-hour window one rig task costs (default 45)')
args = ap.parse_args()

usage = parse(page_text())
if usage['five_hour_remaining_pct'] is None:
    raise SystemExit('could not find the 5-hour figure on the page; layout may have changed')

five, week = usage['five_hour_remaining_pct'], usage['weekly_remaining_pct']
usage['tasks_left_in_window'] = int(five // args.task_cost_pct)

if args.json:
    print(json.dumps(usage, indent=2, ensure_ascii=False))
    sys.exit(0)

print(f"5-hour window : {five:5.1f}% remaining   (resets {usage['five_hour_reset'] or '?'})")
if week is not None:
    print(f"weekly window : {week:5.1f}% remaining   (resets {usage['weekly_reset'] or '?'})")
print(f"\nAt ~{args.task_cost_pct:.0f}% of the 5-hour window per rigging task, that is "
      f"{usage['tasks_left_in_window']} task(s) before the 5-hour window resets.")
if usage['tasks_left_in_window'] < 1:
    print('=> Not enough 5-hour headroom for a task. Wait for the reset, or use Fable.')
