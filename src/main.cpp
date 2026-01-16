//
// Created by Jonathan on 25-Sep-25.
//

#define DISCORDPP_IMPLEMENTATION

#include "musicpp.h"
#include <spdlog/spdlog.h>

using namespace std;

int WINAPI WinMain(const HINSTANCE hInstance, const HINSTANCE hPrevInstance, const LPSTR lpCmdLine,
                   const int nCmdShow) {
    auto start = setup::setup();
    if (holds_alternative<int>(start)) {
        return std::get<int>(start);
    }
    AppContext ctx = std::move(std::get<AppContext>(start));

    return loop::loop(ctx, hInstance);
}
