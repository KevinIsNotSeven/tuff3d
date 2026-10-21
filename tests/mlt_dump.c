/* Load a Maya LT scene and write what tuff3d rebuilds as OBJ:
   cc -Isrc tests/mlt_dump.c src/mlt.c src/mesh.c -lm -o mlt_dump && ./mlt_dump scene.mlt out.obj */
#include <stdio.h>
#include <stdlib.h>
#include "mesh.h"

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s scene.mlt [out.obj]\n", argv[0]); return 2; }
    Mesh *ms;
    char (*names)[32];
    char report[600];
    int n = mlt_load(argv[1], &ms, &names, report, sizeof report);
    printf("%s\n", report);
    if (n < 0) return 1;
    for (int i = 0; i < n; i++) printf("%s: %d verts, %d faces, %d edges\n", names[i], ms[i].nv, ms[i].nf, ms[i].ne);
    if (argc > 2) {
        const Mesh **mp = malloc((n + 1) * sizeof *mp);
        M4 *mats = malloc((n + 1) * sizeof *mats);
        const char **nm = malloc((n + 1) * sizeof *nm);
        for (int i = 0; i < n; i++) { mp[i] = &ms[i]; mats[i] = m4_id(); nm[i] = names[i]; }
        if (!obj_save(argv[2], n, mp, mats, nm)) return 1;
    }
    return 0;
}
