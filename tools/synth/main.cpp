// aion2meter-synth: writes the tests' made-up boss fight (tests/synthetic_session.hpp)
// as a pcapng file, to try aion2meter-replay or the overlay without the game.
// Every name in it is invented, so the file is safe to keep and share.

#include "synthetic_session.hpp"

#include <cstdio>
#include <fstream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: aion2meter-synth <out.pcapng>\n");
        return 2;
    }
    const auto session = aion2::test::synthetic::boss_session();
    std::ofstream out(argv[1], std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(session.pcapng.data()), static_cast<std::streamsize>(session.pcapng.size()));
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("wrote %s (%zu bytes)\n", argv[1], session.pcapng.size());
    return 0;
}
