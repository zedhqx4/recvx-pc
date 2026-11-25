/* Volcado y tabla de correcciones de UV de piezas 2D. Ver pc/include/pc_uvtable.h. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc_platform.h"
#include "pc_uvtable.h"

extern int pcRendererBurstDiag(void);

typedef struct {
    uint64_t hash;
    int n;
    float uv[2 * PC_UIPIECE_MAX];
} UvEntry;

static UvEntry* entries;
static int entry_count;
static int table_loaded;   /* 0: sin cargar */

/* FNV-1a de 64 bits sobre los enteros de la pieza, cada uno como 4 bytes little endian. Mismo orden que pc/tools/uiuv_common.py. */
static uint64_t fnv_u32(uint64_t h, uint32_t v)
{
    int i;
    for (i = 0; i < 4; i++) {
        h ^= (uint64_t)((v >> (8 * i)) & 0xFFu);
        h *= 1099511628211ULL;
    }
    return h;
}

uint64_t pcUiPieceHash(const PcUiPiece* p)
{
    uint64_t h = 14695981039346656037ULL;
    int i;

    h = fnv_u32(h, p->gindex);
    h = fnv_u32(h, (uint32_t)p->tex_w);
    h = fnv_u32(h, (uint32_t)p->tex_h);
    h = fnv_u32(h, (uint32_t)p->topology);
    h = fnv_u32(h, (uint32_t)p->fst);
    h = fnv_u32(h, (uint32_t)p->n);
    for (i = 0; i < p->n; i++) {
        h = fnv_u32(h, (uint32_t)p->x16[i]);
        h = fnv_u32(h, (uint32_t)p->y16[i]);
        h = fnv_u32(h, (uint32_t)p->u16[i]);
        h = fnv_u32(h, (uint32_t)p->v16[i]);
    }
    return h;
}

static int entry_cmp(const void* a, const void* b)
{
    const uint64_t x = ((const UvEntry*)a)->hash, y = ((const UvEntry*)b)->hash;
    return (x > y) - (x < y);
}

static void table_load(void)
{
    const char* path = getenv("RECVX_UI_UV_TABLE");
    FILE* f;
    char line[4096];
    int cap = 0, lineno = 0, bad = 0;

    table_loaded = 1;
    if (path == NULL) path = "ui_uv_table.txt";
    if (pcIniGetInt("ui_uv_table", 1) == 0) return;
    f = fopen(path, "r");
    if (f == NULL) return;

    while (fgets(line, sizeof(line), f) != NULL) {
        char* hash_end;
        char* p;
        unsigned long long hash;
        int n, k;
        UvEntry e;

        lineno++;
        p = strchr(line, '#');
        if (p != NULL) *p = 0;
        p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != 'P') continue;
        p++;
        hash = strtoull(p, &hash_end, 16);
        if (hash_end == p) { bad++; continue; }
        p = hash_end;
        n = (int)strtol(p, &hash_end, 10);
        if (hash_end == p || n < 2 || n > PC_UIPIECE_MAX) { bad++; continue; }
        p = hash_end;
        e.hash = (uint64_t)hash;
        e.n = n;
        for (k = 0; k < 2 * n; k++) {
            e.uv[k] = (float)strtod(p, &hash_end);
            if (hash_end == p) break;
            p = hash_end;
        }
        if (k < 2 * n) { bad++; continue; }
        if (entry_count == cap) {
            cap = cap ? cap * 2 : 256;
            entries = (UvEntry*)realloc(entries, (size_t)cap * sizeof(UvEntry));
            if (entries == NULL) { entry_count = 0; fclose(f); return; }
        }
        entries[entry_count++] = e;
    }
    fclose(f);
    if (entry_count > 1) qsort(entries, (size_t)entry_count, sizeof(UvEntry), entry_cmp);
    pcLog("UI UV table: %d piezas desde %s (%d lineas descartadas)", entry_count, path, bad);
}

int pcUiUvWanted(void)
{
    if (!table_loaded) table_load();
    return entry_count > 0 || pcRendererBurstDiag();
}

int pcUiUvTableLookup(const PcUiPiece* piece, float* uv)
{
    UvEntry key;
    const UvEntry* hit;

    if (!table_loaded) table_load();
    if (entry_count == 0) return 0;
    key.hash = pcUiPieceHash(piece);
    hit = (const UvEntry*)bsearch(&key, entries, (size_t)entry_count, sizeof(UvEntry), entry_cmp);
    if (hit == NULL || hit->n != piece->n) return 0;
    memcpy(uv, hit->uv, sizeof(float) * 2u * (size_t)piece->n);
    return 1;
}

void pcUiUvCapture(const PcUiPiece* piece, const float* default_uv)
{
    static uint64_t seen[16384];
    static int seen_count;
    static int announced;
    const char* name = "recvx_uiuv.tsv";
    uint64_t h;
    FILE* f;
    int i;

    if (!pcRendererBurstDiag()) return;
    h = pcUiPieceHash(piece);
    for (i = 0; i < seen_count; i++)
        if (seen[i] == h) return;
    if (seen_count >= (int)(sizeof(seen) / sizeof(seen[0]))) return;
    seen[seen_count++] = h;

    f = fopen(name, "ab");
    if (f == NULL) return;
    fseek(f, 0, SEEK_END);
    if (ftell(f) == 0)
        fprintf(f, "# recvx ui uv dump v1: P hash gindex tex_w tex_h topology fst n, n*(x16 y16 u16 v16), n*(u v) por defecto\n");
    fprintf(f, "P\t%016llx\t%u\t%d\t%d\t%d\t%d\t%d", (unsigned long long)h, piece->gindex, piece->tex_w, piece->tex_h,
            piece->topology, piece->fst, piece->n);
    for (i = 0; i < piece->n; i++)
        fprintf(f, "\t%d\t%d\t%d\t%d", piece->x16[i], piece->y16[i], piece->u16[i], piece->v16[i]);
    for (i = 0; i < 2 * piece->n; i++)
        fprintf(f, "\t%.4f", (double)default_uv[i]);
    fputc('\n', f);
    fclose(f);
    if (!announced) {
        announced = 1;
        pcLog("UI UV dump: escribiendo %s (una linea por pieza 2D distinta durante la rafaga F8)", name);
    }
}
