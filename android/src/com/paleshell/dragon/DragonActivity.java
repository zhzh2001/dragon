package com.paleshell.dragon;

import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

import java.util.ArrayList;
import java.util.List;

import org.libsdl.app.SDLActivity;

// SDL's activity, with the two things it leaves to the game: which library
// holds SDL_main (SDL is linked statically into libmain.so, so there is no
// libSDL3.so to load first), and the command line. The arguments come from
// the launch intent, so a headless capture is driven from the Mac exactly as
// on the desktop (tools/android/run.sh):
//
//   adb shell am start -n com.paleshell.dragon/.DragonActivity \
//       --es args "--headless --frames 40 --screenshot shot.bmp"
//
// Leading NAME=value words are the environment instead (DRAGON_FRAME_PROFILE,
// DRAGON_GPU_DEBUG...), set before the game starts, as a shell would.
//
// A launch that carries arguments is a scripted one, and the phone is
// usually locked when it comes: SDL only starts the game once it has a
// surface, which a paused activity behind the keyguard never gets. Such a
// launch shows over the lock screen and wakes the display, without unlocking
// anything; one from the launcher is untouched.
public class DragonActivity extends SDLActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        if (getIntent() != null && getIntent().hasExtra("args")) {
            setShowWhenLocked(true);
            setTurnScreenOn(true);
        }
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {"main"};
    }

    @Override
    protected String[] getArguments() {
        String args = getIntent() != null ? getIntent().getStringExtra("args") : null;
        if (args == null || args.trim().isEmpty()) return new String[0];
        List<String> out = new ArrayList<>();
        boolean leading = true;
        for (String word : args.trim().split("\\s+")) {
            int eq = word.indexOf('=');
            if (leading && eq > 0 && word.substring(0, eq).matches("[A-Z_][A-Z0-9_]*")) {
                try {
                    Os.setenv(word.substring(0, eq), word.substring(eq + 1), true);
                } catch (ErrnoException e) {
                    Log.w("Dragon", "setenv " + word + ": " + e);
                }
                continue;
            }
            leading = false;
            out.add(word);
        }
        return out.toArray(new String[0]);
    }
}
