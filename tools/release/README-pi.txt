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


RUNNING IT (Raspberry Pi, 32-bit Raspberry Pi OS)
-------------------------------------------------
This is the smallest build: OpenGL ES 2.0 at the lowest content, made for
the Raspberry Pi 1 Model B (700 MHz, 512 MB), where it runs at 24 to 30
frames a second on a monitor. It needs:

  - 32-bit Raspberry Pi OS Bullseye or later (glibc 2.31). Any Pi runs
    the 32-bit system, from the Pi 1 and Zero to the Pi 4;
  - the KMS display driver (dtoverlay=vc4-kms-v3d in /boot/config.txt,
    the default since Bullseye) and the GLES library:
      sudo apt install libgles2
  - a text console, not the desktop. The game takes the screen itself
    (KMS/DRM); it has no X11 or Wayland support. Boot to the console
    (raspi-config, System Options, Boot), or press Ctrl+Alt+F2;
  - a keyboard or a gamepad. Your user must be in the "input" and
    "video" groups (the first user the system makes is).

Unpack and run:

  tar xzf Dragon-*-pi.tar.gz
  cd Dragon && ./dragon

The first start takes about 15 seconds. It carries one dragon,
Embercrest. Sound goes to ALSA's default device.

Records and saved tuning go to ~/.local/share/Paleshell/Dragon.
From the console:

  ./dragon --run        a hoard run
  ./dragon --demo       the game playing itself


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
