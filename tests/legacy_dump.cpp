/* Drives the RELEASED engine (git tag v0.9.2) like the 0.9 wrapper did and prints the chunk it would have saved (the "state" text).
 * The sample library is made at a fixed path (argv[1]) so the host test can make the same files again. tests/make_legacy_chunk.sh. */
#include <stdio.h>
#include <string.h>
#include <string>
#include <thread>
#include <chrono>
extern "C" {
#include "engine.h"
}
int main(int argc, char **argv) {
    if (argc < 3) return 2;
    const mpc_engine_t *E = mpc_engine();
    void *h = E->create(argv[2]);
    char b[8192];
    for (int t = 0; t < 500; t++) { b[0] = 0; E->get_param(h, "lib_info", b, sizeof b); if (strstr(b, " samples")) break; std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    E->set_param(h, "generate", "1.000000");
    E->set_param(h, "pad1_lock", "1.000000");
    E->set_param(h, "pad2_gain", "150");
    E->set_param(h, "sel_pad", "3");
    E->set_param(h, "sel_cat_bass", "1.000000");      /* pad 3 also draws bass */
    E->set_param(h, "follow", "0");
    E->set_param(h, "max_size", "2");
    E->get_param(h, "state", b, sizeof b);
    fwrite(b, 1, strlen(b) + 1, stdout);           /* the wrapper saved the text with its NUL */
    E->destroy(h);
    return 0;
}
