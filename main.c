#include "raylib/src/raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui/src/raygui.h"

typedef enum {
    SIGNIN_EMAIL, SIGNIN_PASSWORD, ATTEMPT_SIGNIN, MAIN
} State;

int main() {
    InitWindow(500, 400, "seedhunt.org");
    SetTargetFPS(60);

    bool showMessageBox = false;

    bool show_email = true;
    bool show_password = false;

    State current_state = SIGNIN_EMAIL;

    char email[1024] = "\0";
    char password[1024] = "\0";
    bool y = true;
    while (!WindowShouldClose()) {
        BeginDrawing(); {
            ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));

            switch (current_state) {
                case SIGNIN_EMAIL: {
                    if (GuiTextInputBox((Rectangle){ (500 - 200) / 2, (400 - 150) / 2, 200, 150 }, "Sign in", "Email", "Enter", email, 100, NULL) == 1) {
                        current_state = SIGNIN_PASSWORD;
                    }
                    break;
                }
                case SIGNIN_PASSWORD: {
                    if (GuiTextInputBox((Rectangle){ (500 - 200) / 2, (400 - 150) / 2, 200, 150 }, "Sign in", "Password", "Enter", password, 100, NULL) == 1) {
                        if (strlen(password) > 0) {
                            current_state = ATTEMPT_SIGNIN;
                        }
                    }
                    break;
                } 
                case ATTEMPT_SIGNIN: {
                    GuiDrawText("Attemping Login...", (Rectangle){ (500 - 200) / 2, (400 - 150) / 2, 200, 150 }, 1, RED);     // Gui draw text using default font
                    break; 
                }
            }
        }
        EndDrawing();

    }

    CloseWindow();
    return 0;
}