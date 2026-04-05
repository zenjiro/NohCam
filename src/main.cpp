#include <cstdlib>

#include "app/Application.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    nohcam::Application application(instance);
    if (!application.Initialize(show_command)) {
        return EXIT_FAILURE;
    }

    const int exit_code = application.Run();
    application.Shutdown();
    return exit_code;
}
