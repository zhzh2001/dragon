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


RUNNING IT (macOS 11 or later, Apple silicon or Intel)
------------------------------------------------------
The app is not notarised, so macOS stops it on first launch.

  1. Drag Dragon.app wherever you like and double-click it.
  2. When macOS says it cannot be opened, open System Settings >
     Privacy & Security, scroll down, and click "Open Anyway".

Or, in Terminal:   xattr -dr com.apple.quarantine Dragon.app

Records and saved tuning go to ~/Library/Application Support/Paleshell/Dragon.
Double-clicking starts a hoard run on a fresh valley. From Terminal:

  Dragon.app/Contents/MacOS/dragon --run        a hoard run
  Dragon.app/Contents/MacOS/dragon --demo       the game playing itself


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
