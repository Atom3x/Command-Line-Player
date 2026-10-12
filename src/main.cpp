#include "common/headers.hpp"
#include "player/Player.hpp"

int main() {
    try {
        Player player;
    }
    catch (...) {
        std::cerr << "CLP startup has been failed\n";
        return 1;
    }
    return 0;
}