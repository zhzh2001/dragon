DRAGON
======

A dragon flight game: an energy flight model, a hoard-run roguelite down
generated valleys, and rival dragons that fly the same physics you do.
Source, docs and issues: https://github.com/zhzh2001/dragon

MADE WITH AI. Most of the code was written by AI coding agents (Claude,
OpenAI Codex). Every dragon model was generated with Tencent Hunyuan 3D,
and the concept art with GPT Image 2 and Nano Banana 2. One person directed
it and playtested it. The details are in AI_DISCLOSURE.md. The sound is
synthesized in code.


RUNNING IT (Windows XP SP3 or later, 32-bit)
---------------------------------------------
This is the retro build: Direct3D 9, for the graphics cards of 2002 to
2008. Unzip the Dragon folder anywhere and run dragon.exe. It needs:

  - Windows XP with Service Pack 3 (it runs on later Windows too);
  - a processor with SSE2 (Pentium 4, Athlon 64 or later);
  - DirectX 9.0c, which XP SP2 and SP3 already carry;
  - a Direct3D 9 graphics card. It picks the content to fit the card:
    shader model 3 (GeForce 6 and 7, Radeon X1000), shader model 2
    (Radeon 9500 to X850, GeForce FX), or fixed function for older cards.

A gamepad needs the DirectX End-User Runtime (June 2010) for XInput; the
keyboard and mouse need nothing.

Records and saved tuning go to Application Data\Paleshell\Dragon in your
profile. Double-clicking starts a hoard run on a fresh valley. From a
command prompt:

  dragon.exe --run              a hoard run
  dragon.exe --demo             the game playing itself
  dragon.exe --preset minimal   the lightest settings, for the oldest cards


CONTROLS (gamepad recommended)
------------------------------
  Left stick / W S A D      pitch and roll (on the ground: walk and turn)
  A / Space                 flap; on the ground, leap
  RT / Shift                tuck the wings and dive
  LT / Ctrl                 flare and brake; hold it low to land
  LB / F or left mouse      breath (hold)
  RB / G                    fireball; hold for a charged shot once grown
  B / C                     bite; claw and tail strikes up close
  X / X                     boost
  D-pad left/right / Z      dodge roll
  D-pad up / B              flip, to face a chaser
  Y / U                     second breath, once adult
  R-stick click / T         lock the next target
  Right stick / right-drag  free look
  1 2 3 / V                 camera presets / first person
  P                         let the demo pilot fly, and take it back
  R / Enter                 restart the valley / next valley
  F1                        hide the tuning panels
  Esc                       quit


LICENCES
--------
The game is MIT (LICENSE). The libraries keep their own licences
(THIRD_PARTY_NOTICES.md). ATTRIBUTION.md says what everything is and where
it came from.
