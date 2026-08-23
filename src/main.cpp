#include <SDL3/SDL_main.h>

#include "app.h"

int main(int argc, char** argv) {
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);

    app::Options options = app::parse_options(argc, argv);
    app::App application;
    if (!application.init(options)) {
        application.shutdown();
        return 1;
    }
    application.run();
    application.shutdown();
    return 0;
}
