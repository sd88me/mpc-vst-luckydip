/* Dev tool: scan a folder, fill a kit at random (seeded), and export it like the plugin does.
 *   export_tool <sample root> <export dir> <name> <copy|link> [seed]
 * Used to make test kits on a device without driving the plugin UI (build for armhf with the engine's compiler). */
#include <stdio.h>
#include <stdlib.h>
#include "ld_audio.h"
#include "ld_core.h"
#include "ld_xpm.h"
using namespace ld;
int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: %s <root> <export dir> <name> <copy|link> [seed]\n", argv[0]); return 2; }
    Classifier cl; ScanOpts so; so.roots.push_back(argv[1]);
    Library lib; ScanStats st;
    scan_library(so, cl, lib.recs, st);
    lib.rebuild();
    printf("%zu samples\n", lib.recs.size());
    Pad pads[NPADS];
    AssignResult r = assign_kit(pads, lib, argc > 5 ? (uint32_t)atoi(argv[5]) : 1234);
    printf("unresolved %d\n", r.unresolved);
    ExportResult x = export_xpm(argv[2], argv[3], pads, std::string(argv[4]) == "link");
    printf("%s: %s pads=%d gathered=%d\n", x.ok ? "ok" : "FAILED", x.ok ? x.path.c_str() : x.error.c_str(), x.pads, x.gathered);
    for (size_t i = 0; i < x.warnings.size(); i++) printf("warning: %s\n", x.warnings[i].c_str());
    return x.ok ? 0 : 1;
}
