/* Dev tool: time each engine action as the host would call it (build for armhf, run on the device). */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <string>
extern "C" {
#include "engine.h"
}
static double now() { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
int main(int argc, char **argv) {
    const mpc_engine_t *E = mpc_engine();
    void *h = E->create(argc > 1 ? argv[1] : "/tmp/luckydip_t");
    char b[256];
    for (int i = 0; i < 200; i++) { E->get_param(h, "lib_info", b, sizeof b); if (strstr(b, "samples")) break; usleep(100000); }
    printf("library: %s\n", b);
    const char *acts[] = {"generate", "pad3_reroll", "normalise", "rescan", "export", "clear_all", "unlock_all", "src_loc_next", "sel_play"};
    for (size_t a = 0; a < sizeof acts / sizeof *acts; a++) {
        double worst = 0, sum = 0; int n = 20;
        for (int i = 0; i < n; i++) { double t = now(); E->set_param(h, acts[a], "1.0"); double d = now() - t; sum += d; if (d > worst) worst = d; usleep(30000); }
        printf("%-14s mean %.3f ms  worst %.3f ms\n", acts[a], sum / n, worst);
    }
    double t = now(); for (int i = 0; i < 100; i++) E->get_param(h, "pad1_name", b, sizeof b); printf("get_param x100: %.3f ms\n", now() - t);
    E->destroy(h);
}
