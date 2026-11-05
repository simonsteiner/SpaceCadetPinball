package io.github.simonsteiner.spacecadetpinball;

import android.os.Build;
import android.os.Bundle;
import android.view.KeyEvent;
import android.window.OnBackInvokedDispatcher;

import org.libsdl.app.SDLActivity;

/**
 * The game's activity: SDL's, with SDL linked statically into libmain.so and the back gesture as Escape.
 */
public class SpaceCadetActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        // SDL3 and SDL3_mixer are static in the game's library
        return new String[] {"main"};
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Android 16 delivers the back gesture only to a callback, for an app that targets it; the game hears it as
        // Escape, which opens its pause screen, so the gesture never closes the app before the game knows
        if (Build.VERSION.SDK_INT >= 33) {
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                OnBackInvokedDispatcher.PRIORITY_DEFAULT, this::sendEscape);
        }
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        sendEscape();
    }

    private void sendEscape() {
        if (!SDLActivity.mBrokenLibraries) {
            SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_ESCAPE);
            SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_ESCAPE);
        }
    }
}
