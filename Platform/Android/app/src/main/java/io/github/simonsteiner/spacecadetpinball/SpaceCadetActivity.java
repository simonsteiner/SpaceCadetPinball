package io.github.simonsteiner.spacecadetpinball;

import android.os.Build;
import android.os.Bundle;
import android.view.InputDevice;
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

    // Registers the back gesture's callback where the system has one (API 33 and later)
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

    // Before API 33, SDL's view takes the back key itself and hands the game SDLK_AC_BACK, which it doesn't use, so
    // onBackPressed never runs; the activity sees the key first and makes it Escape. A mouse's back button stays SDL's.
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK
            && (event.getSource() & InputDevice.SOURCE_MOUSE) != InputDevice.SOURCE_MOUSE) {
            if (event.getAction() == KeyEvent.ACTION_UP && !event.isCanceled()) {
                sendEscape();
            }
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    // Presses and releases Escape in the game, unless its libraries failed to load
    private void sendEscape() {
        if (!SDLActivity.mBrokenLibraries) {
            SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_ESCAPE);
            SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_ESCAPE);
        }
    }
}
