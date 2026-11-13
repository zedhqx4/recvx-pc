/* Opt-in, passive texture provenance trace for the OpenGL 3.3 diagnostic branch. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "pc_texture_trace.h"
#include "pc_platform.h"

#define TRACE_MODEL_STACK 16
#define TRACE_MODEL_STRIPS 16384
#define TRACE_SOURCE_DUMPS 2048
#define TRACE_GS_DUMPS 4096
#define TRACE_MAX_DUMP_BYTES (64u * 1024u * 1024u)

typedef struct {
    uint64_t id;
    const void* model;
    const void* vlist;
    const void* plist;
    const void* chunk;
    unsigned int chunk_ordinal;
    unsigned int chunk_type;
    unsigned int chunk_bits;
    unsigned int chunk_size;
    unsigned int strip_index;
    uint64_t face_first;
    uint64_t face_last;
    uint64_t strip_face_first[TRACE_MODEL_STRIPS];
    uint64_t strip_face_last[TRACE_MODEL_STRIPS];
    uint64_t material_event;
} TraceModel;

typedef struct {
    uint64_t hash;
    unsigned int size;
    unsigned int gindex;
    unsigned int bank;
    unsigned int role;
} TraceSourceDump;

typedef struct {
    uint64_t tex0;
    uint64_t texa;
    uint64_t texclut;
    uint64_t generation;
    uint64_t clut_hash;
    int width;
    int height;
} TraceGsDump;

#define TRACE_MAX_LOG_BYTES (128u * 1024u * 1024u)
#define TRACE_TRANSFER_PHASES 4
#define TRACE_IMAGE_DUMPS 4096
#define TRACE_IMAGE_ALIASES 8192

typedef struct {
    char phase[40];
    uint64_t bytes;
    uint64_t chunks;
    uint64_t hash_state;
    int hash_valid;
    unsigned int bp, bw, psm, x, y, width, height;
    char dump_path[160];
} TraceTransferPhase;

typedef struct {
    uint64_t hash;
    unsigned int width, height;
    char path[160];
} TraceImageDump;

typedef struct {
    uint64_t hash;
    unsigned int tbp, psm, width, height;
} TraceImageAlias;

static int trace_state; /* 0=off, 1=on */
static int trace_sessions;
static int trace_atexit_registered;
static int trace_topology_state = -1;
static int trace_dump_state = -1;
static uint64_t primitive_sequence;
static FILE* trace_file;
static uint64_t trace_sequence;
static uint64_t trace_frame;
static uint64_t model_sequence;
static uint64_t face_sequence;
static uint64_t transfer_sequence;
static uint64_t trace_session_start;
static TraceTransferPhase transfer_phases[TRACE_TRANSFER_PHASES];
static unsigned int transfer_phase_count;
static TraceImageDump image_dumps[TRACE_IMAGE_DUMPS];
static unsigned int image_dump_count;
static TraceImageAlias image_aliases[TRACE_IMAGE_ALIASES];
static unsigned int image_alias_count;
static TraceModel model_stack[TRACE_MODEL_STACK];
static unsigned int model_depth;
static TraceSourceDump source_dumps[TRACE_SOURCE_DUMPS];
static unsigned int source_dump_count;
static TraceGsDump gs_dumps[TRACE_GS_DUMPS];
static unsigned int gs_dump_count;
static char resource_kind[32];
static char resource_name[192];
static const void* resource_base;
static const void* resource_data;
static int resource_stage = -1;
static int resource_room = -1;
static int resource_case = -1;
static unsigned int resource_index;
static int resource_offset = -1;

static int trace_topology_enabled(void)
{
    const char* value;
    if (trace_topology_state < 0) {
        value = getenv("RECVX_TEX_TRACE_TOPOLOGY");
        trace_topology_state = (value != NULL && strcmp(value, "1") == 0) ? 1 : 0;
    }
    return trace_topology_state;
}

static int trace_dumps_enabled(void)
{
    const char* value;
    if (trace_dump_state < 0) {
        value = getenv("RECVX_TEX_TRACE_DUMPS");
        trace_dump_state = (value != NULL && strcmp(value, "1") == 0) ? 1 : 0;
    }
    return trace_dump_state;
}

static uint64_t trace_hash(const void* data, size_t size)
{
    const unsigned char* p = (const unsigned char*)data;
    uint64_t h = 1469598103934665603ULL;
    size_t i;
    for (i = 0; i < size; i++)
        h = (h ^ p[i]) * 1099511628211ULL;
    return h;
}

static void trace_json_string(const char* text)
{
    const unsigned char* p = (const unsigned char*)(text ? text : "");
    fputc('"', trace_file);
    while (*p) {
        unsigned char c = *p++;
        if (c == '"' || c == '\\') {
            fputc('\\', trace_file);
            fputc(c, trace_file);
        } else if (c == '\n') {
            fputs("\\n", trace_file);
        } else if (c == '\r') {
            fputs("\\r", trace_file);
        } else if (c == '\t') {
            fputs("\\t", trace_file);
        } else if (c < 0x20) {
            fprintf(trace_file, "\\u%04x", (unsigned int)c);
        } else {
            fputc(c, trace_file);
        }
    }
    fputc('"', trace_file);
}

static void trace_close(void)
{
    if (trace_file != NULL) {
        fprintf(trace_file, "{\"event\":\"trace_stop\",\"seq\":%llu}\n",
                (unsigned long long)trace_sequence);
        fflush(trace_file);
        fclose(trace_file);
        trace_file = NULL;
    }
}

static int trace_open(void)
{
    if (trace_state != 1)
        return 0;
    if (trace_file != NULL)
        return 1;
#ifdef _WIN32
    _mkdir("texture_trace");
#else
    mkdir("texture_trace", 0777);
#endif
    trace_file = fopen("texture_trace/events.jsonl", trace_sessions == 0 ? "wb" : "ab");
    if (trace_file == NULL) {
        trace_state = 0;
        pcLog("texture trace: cannot open texture_trace/events.jsonl");
        return 0;
    }
    setvbuf(trace_file, NULL, _IOFBF, 1024u * 1024u);
    if (!trace_atexit_registered) {
        atexit(trace_close);
        trace_atexit_registered = 1;
    }
    trace_sessions++;
    transfer_phase_count = 0;
    fprintf(trace_file, "{\"event\":\"trace_start\",\"version\":3,\"session\":%d,"
                        "\"mode\":\"compact\",\"topology_detail\":%s,\"binary_dumps\":%s,"
                        "\"source_groups_format\":\"seq_first,seq_last,model_id,model_ptr,chunk_ptr,chunk_ordinal,chunk_type,chunk_bits,chunk_size,strip,primitive_count,vertex_count,face_first,face_last,material_event\","
                        "\"stage\":%d,\"room\":%d,\"case\":%d,\"resource_kind\":",
            trace_sessions, trace_topology_enabled() ? "true" : "false",
            trace_dumps_enabled() ? "true" : "false",
            resource_stage, resource_room, resource_case);
    trace_json_string(resource_kind[0] ? resource_kind : "UNKNOWN");
    fputs(",\"resource_name\":", trace_file);
    trace_json_string(resource_name);
    fputs("}\n", trace_file);
    fflush(trace_file);
    trace_session_start = (uint64_t)ftell(trace_file);
    pcLog("texture trace: capture started; press F5 to stop; output: texture_trace/");
    return 1;
}

static int trace_set_enabled(int enabled)
{
    if (enabled) {
        if (trace_state == 1 && trace_file != NULL)
            return 1;
        trace_state = 1;
        if (trace_open())
            return 1;
        trace_state = 0;
        return -1;
    }
    if (trace_state == 1) {
        trace_close();
        trace_state = 0;
    }
    return 0;
}

static TraceModel* trace_model(void)
{
    return model_depth ? &model_stack[model_depth - 1] : NULL;
}

static void trace_begin(const char* event)
{
    TraceModel* m;
    if (!trace_open())
        return;
    trace_sequence++;
    m = trace_model();
    fprintf(trace_file, "{\"seq\":%llu,\"frame\":%llu,\"event\":",
            (unsigned long long)trace_sequence, (unsigned long long)trace_frame);
    trace_json_string(event);
    fprintf(trace_file, ",\"stage\":%d,\"room\":%d,\"case\":%d,\"resource_kind\":",
            resource_stage, resource_room, resource_case);
    trace_json_string(resource_kind[0] ? resource_kind : "UNKNOWN");
    fputs(",\"resource_name\":", trace_file);
    trace_json_string(resource_name);
    fprintf(trace_file, ",\"resource_base\":\"%p\",\"resource_data\":\"%p\",\"resource_index\":%u,\"resource_offset\":%d",
            resource_base, resource_data, resource_index, resource_offset);
    if (m != NULL) {
        fprintf(trace_file, ",\"model_id\":%llu,\"model\":\"%p\",\"vlist\":\"%p\",\"plist\":\"%p\",\"chunk\":\"%p\",\"chunk_ordinal\":%u,\"chunk_type\":%u,\"chunk_bits\":%u,\"chunk_size\":%u,\"strip\":%u,\"face_first\":%llu,\"face_last\":%llu,\"material_seq\":%llu",
                (unsigned long long)m->id, m->model, m->vlist, m->plist, m->chunk,
                m->chunk_ordinal, m->chunk_type, m->chunk_bits, m->chunk_size,
                m->strip_index, (unsigned long long)m->face_first,
                (unsigned long long)m->face_last, (unsigned long long)m->material_event);
    } else {
        fputs(",\"model_id\":0,\"model\":null,\"chunk\":null", trace_file);
    }
}

static void trace_end(void)
{
    if (trace_file != NULL)
        fputs("}\n", trace_file);
}

static int trace_dump_seen(const char* role, uint64_t hash, unsigned int size,
                           unsigned int gindex, unsigned int bank)
{
    unsigned int i, role_id = (role && strcmp(role, "resident") == 0) ? 2u : 1u;
    for (i = 0; i < source_dump_count; i++) {
        TraceSourceDump* d = &source_dumps[i];
        if (d->hash == hash && d->size == size && d->gindex == gindex &&
            d->bank == bank && d->role == role_id)
            return 1;
    }
    if (source_dump_count < TRACE_SOURCE_DUMPS) {
        TraceSourceDump* d = &source_dumps[source_dump_count++];
        d->hash = hash; d->size = size; d->gindex = gindex; d->bank = bank; d->role = role_id;
    }
    return 0;
}

static int trace_gs_seen(uint64_t tex0, uint64_t texa, uint64_t texclut,
                         uint64_t generation, uint64_t clut_hash, int width, int height)
{
    unsigned int i;
    for (i = 0; i < gs_dump_count; i++) {
        TraceGsDump* d = &gs_dumps[i];
        if (d->tex0 == tex0 && d->texa == texa && d->texclut == texclut &&
            d->generation == generation && d->clut_hash == clut_hash &&
            d->width == width && d->height == height)
            return 1;
    }
    if (gs_dump_count < TRACE_GS_DUMPS) {
        TraceGsDump* d = &gs_dumps[gs_dump_count++];
        d->tex0 = tex0; d->texa = texa; d->texclut = texclut;
        d->generation = generation; d->clut_hash = clut_hash;
        d->width = width; d->height = height;
    }
    return 0;
}

int pcTexTraceEnabled(void)
{
    return trace_state == 1;
}

int pcTexTraceToggle(void)
{
    return trace_set_enabled(trace_state != 1);
}

int pcTexTraceDumpsEnabled(void)
{
    return pcTexTraceEnabled() && trace_dumps_enabled();
}

void pcTexTraceSnapshotDrawContext(PcTexTraceDrawContext* out)
{
    TraceModel* m;
    if (out == NULL)
        return;
    memset(out, 0, sizeof(*out));
    if (!trace_open())
        return;
    out->tracing_enabled = 1;
    m = trace_model();
    if (m == NULL)
        return;
    out->model_id = m->id;
    out->model = m->model;
    out->chunk = m->chunk;
    out->chunk_ordinal = m->chunk_ordinal;
    out->chunk_type = m->chunk_type;
    out->chunk_bits = m->chunk_bits;
    out->chunk_size = m->chunk_size;
    out->strip_index = m->strip_index;
    out->face_first = m->face_first;
    out->face_last = m->face_last;
    out->material_event = m->material_event;
}

void pcTexTraceSetFrame(uint64_t frame)
{
    if (trace_open()) {
        trace_frame = frame;
        /* Bound crash-loss to about two seconds without flushing every draw/event. */
        if ((frame % 60u) == 0) {
            long pos;
            fflush(trace_file);
            pos = ftell(trace_file);
            if (pos >= 0 && (uint64_t)pos >= trace_session_start + TRACE_MAX_LOG_BYTES) {
                fprintf(trace_file, "{\"event\":\"trace_limit_reached\",\"limit_bytes\":%u}\n",
                        (unsigned int)TRACE_MAX_LOG_BYTES);
                fflush(trace_file);
                fclose(trace_file);
                trace_file = NULL;
                trace_state = 0;
                pcLog("texture trace stopped at 128 MiB safety limit; press F5 to start another capture");
            }
        }
    }
}

void pcTexTraceSetResource(const char* kind, const char* name,
                           int stage, int room, int room_case,
                           const void* base, const void* data,
                           unsigned int index, int offset)
{
    /* Keep the current room/resource identity even before F5 is pressed. The capture
     * may be enabled after the room has already loaded. */
    snprintf(resource_kind, sizeof(resource_kind), "%s", kind ? kind : "UNKNOWN");
    snprintf(resource_name, sizeof(resource_name), "%s", name ? name : "");
    resource_stage = stage;
    resource_room = room;
    resource_case = room_case;
    resource_base = base;
    resource_data = data;
    resource_index = index;
    resource_offset = offset;
    if (!trace_open())
        return;
    trace_begin("resource_context");
    fprintf(trace_file, ",\"data_offset_from_base\":");
    if (base != NULL && data != NULL)
        fprintf(trace_file, "%lld", (long long)((intptr_t)(uintptr_t)data - (intptr_t)(uintptr_t)base));
    else
        fputs("null", trace_file);
    trace_end();
}

void pcTexTraceClearResource(void)
{
    /* The RDT/block pointers are temporary, but its room identity remains useful
     * when F5 is pressed after loading has finished. */
    resource_base = NULL;
    resource_data = NULL;
}

void pcTexTraceTextureBlock(const void* texlist, const void* data, unsigned int texture_offset)
{
    if (!trace_open())
        return;
    trace_begin("texture_resource_block");
    fprintf(trace_file, ",\"texlist\":\"%p\",\"data\":\"%p\",\"texture_offset\":%u", texlist, data, texture_offset);
    if (resource_base != NULL && data != NULL)
        fprintf(trace_file, ",\"data_offset_from_base\":%lld", (long long)((const char*)data - (const char*)resource_base));
    trace_end();
}

void pcTexTraceDumpTim2(const char* role, const void* data, unsigned int size,
                        unsigned int gindex, unsigned int bank,
                        unsigned int pict_format, unsigned int image_type,
                        unsigned int clut_type, unsigned int width, unsigned int height)
{
    uint64_t hash;
    char path[256];
    FILE* f;
    if (!trace_open() || data == NULL || size < 144 || size > TRACE_MAX_DUMP_BYTES)
        return;
    {
        const unsigned char* bytes = (const unsigned char*)data;
        uint32_t total_size, clut_size, image_size, header_size;
        uint64_t image_hash = 0, clut_hash = 0;
        size_t image_offset, clut_offset;
        memcpy(&total_size, bytes + 128, sizeof(total_size));
        memcpy(&clut_size, bytes + 132, sizeof(clut_size));
        memcpy(&image_size, bytes + 136, sizeof(image_size));
        memcpy(&header_size, bytes + 140, sizeof(header_size));
        if (total_size > size - 128 || header_size < 0x50 ||
            header_size > total_size || image_size > total_size - header_size ||
            clut_size > total_size - header_size - image_size)
            return;
        image_offset = 128u + (size_t)header_size;
        clut_offset = image_offset + (size_t)image_size;
        if (image_size != 0)
            image_hash = trace_hash(bytes + image_offset, image_size);
        if (clut_size != 0)
            clut_hash = trace_hash(bytes + clut_offset, clut_size);
        hash = trace_hash(data, size);
        trace_begin("tim2_dump");
    fprintf(trace_file, ",\"role\":");
    trace_json_string(role ? role : "unknown");
    fprintf(trace_file, ",\"pointer\":\"%p\",\"bytes\":%u,\"hash\":\"%016llx\",\"image_hash\":\"%016llx\",\"clut_hash\":\"%016llx\",\"gindex\":%u,\"bank\":%u,\"pict_format\":%u,\"image_type\":%u,\"clut_type\":%u,\"width\":%u,\"height\":%u",
            data, size, (unsigned long long)hash, (unsigned long long)image_hash,
            (unsigned long long)clut_hash, gindex, bank, pict_format, image_type, clut_type, width, height);
    }
    if (!trace_dumps_enabled()) {
        fputs(",\"dump_skipped\":\"set RECVX_TEX_TRACE_DUMPS=1\"", trace_file);
    } else if (!trace_dump_seen(role, hash, size, gindex, bank)) {
        snprintf(path, sizeof(path), "texture_trace/%s_g%08x_b%02x_%016llx.bin",
                 (role && strcmp(role, "resident") == 0) ? "resident" : "source",
                 gindex, bank & 0xFFu, (unsigned long long)hash);
        f = fopen(path, "wb");
        if (f != NULL) {
            fwrite(data, 1, size, f);
            fclose(f);
            fprintf(trace_file, ",\"dump\":");
            trace_json_string(path);
        } else {
            fputs(",\"dump_error\":true", trace_file);
        }
    } else {
        fputs(",\"dump_deduplicated\":true", trace_file);
    }
    trace_end();
}

void pcTexTraceTextureRegistration(const char* operation, const void* texlist,
                                   unsigned int list_index, const void* texname,
                                   const void* source_info, const void* source_tim2,
                                   const void* entry, const void* resident_tim2,
                                   unsigned int gindex, unsigned int bank,
                                   unsigned int count, unsigned int tp_flag)
{
    if (!trace_open())
        return;
    trace_begin("texture_registration");
    fputs(",\"operation\":", trace_file); trace_json_string(operation);
    fprintf(trace_file, ",\"texlist\":\"%p\",\"list_index\":%u,\"texname\":\"%p\",\"source_info\":\"%p\",\"source_tim2\":\"%p\",\"entry\":\"%p\",\"resident_tim2\":\"%p\",\"gindex\":%u,\"bank\":%u,\"ref_count\":%u,\"tp_flag\":%u",
            texlist, list_index, texname, source_info, source_tim2, entry, resident_tim2,
            gindex, bank, count, tp_flag);
    trace_end();
}

void pcTexTraceTextureUse(const char* operation, unsigned int requested_index,
                          const void* texlist, unsigned int list_count,
                          const void* entry, const void* resident_tim2,
                          unsigned int gindex, unsigned int bank,
                          unsigned int ref_count, unsigned int tp_flag)
{
    TraceModel* m;
    if (!trace_open())
        return;
    trace_begin("texture_use");
    fputs(",\"operation\":", trace_file); trace_json_string(operation);
    fprintf(trace_file, ",\"requested_index\":%u,\"texlist\":\"%p\",\"list_count\":%u,\"entry\":\"%p\",\"resident_tim2\":\"%p\",\"gindex\":%u,\"bank\":%u,\"ref_count\":%u,\"tp_flag\":%u",
            requested_index, texlist, list_count, entry, resident_tim2, gindex, bank, ref_count, tp_flag);
    m = trace_model();
    if (m != NULL && m->chunk != NULL)
        fprintf(trace_file, ",\"ctid_chunk\":\"%p\",\"ctid_chunk_ordinal\":%u", m->chunk, m->chunk_ordinal);
    if (m != NULL) {
        m->material_event = trace_sequence;
    }
    trace_end();
}

uint64_t pcTexTraceModelBegin(const void* model, const void* vlist, const void* plist)
{
    TraceModel* m;
    if (!trace_open())
        return 0;
    if (model_depth >= TRACE_MODEL_STACK) {
        trace_begin("model_stack_overflow");
        fprintf(trace_file, ",\"model\":\"%p\"", model);
        trace_end();
        return 0;
    }
    m = &model_stack[model_depth++];
    memset(m, 0, sizeof(*m));
    m->id = ++model_sequence;
    m->model = model;
    m->vlist = vlist;
    m->plist = plist;
    trace_begin("model_begin");
    fprintf(trace_file, ",\"model_id_new\":%llu,\"model_ptr\":\"%p\",\"model_vlist\":\"%p\",\"model_plist\":\"%p\"",
            (unsigned long long)m->id, model, vlist, plist);
    trace_end();
    return m->id;
}

void pcTexTraceModelEnd(uint64_t model_id)
{
    TraceModel* m;
    if (!trace_open() || model_depth == 0 || model_id == 0)
        return;
    m = &model_stack[model_depth - 1];
    trace_begin("model_end");
    fprintf(trace_file, ",\"model_id_end\":%llu,\"model_ptr\":\"%p\"", (unsigned long long)model_id, m->model);
    trace_end();
    if (m->id == model_id)
        model_depth--;
}

void pcTexTraceModelChunk(const char* phase, const void* chunk,
                          unsigned int ordinal, unsigned int type,
                          unsigned int head_bits, unsigned int size)
{
    TraceModel* m;
    if (!trace_open())
        return;
    m = trace_model();
    if (m != NULL) {
        m->chunk = chunk;
        m->chunk_ordinal = ordinal;
        m->chunk_type = type;
        m->chunk_bits = head_bits;
        m->chunk_size = size;
        m->strip_index = 0;
        m->face_first = m->face_last = 0;
        memset(m->strip_face_first, 0, sizeof(m->strip_face_first));
        memset(m->strip_face_last, 0, sizeof(m->strip_face_last));
    }
    trace_begin("model_chunk");
    fputs(",\"phase\":", trace_file); trace_json_string(phase);
    fprintf(trace_file, ",\"chunk_ptr\":\"%p\",\"ordinal\":%u,\"type\":%u,\"head_bits\":%u,\"size\":%u",
            chunk, ordinal, type, head_bits, size);
    trace_end();
}

void pcTexTraceStrip(unsigned int strip_index, unsigned int vertex_count)
{
    TraceModel* m;
    if (!trace_open())
        return;
    m = trace_model();
    if (m != NULL) {
        m->strip_index = strip_index;
        if (strip_index < TRACE_MODEL_STRIPS) {
            m->face_first = m->strip_face_first[strip_index];
            m->face_last = m->strip_face_last[strip_index];
        } else {
            m->face_first = 0;
            m->face_last = 0;
        }
    }
    trace_begin("geometry_strip");
    fprintf(trace_file, ",\"strip_index\":%u,\"vertex_count\":%u", strip_index, vertex_count);
    trace_end();
}

void pcTexTraceFace(const char* kind, const void* chunk,
                    unsigned int strip_index, unsigned int face_index,
                    unsigned int vertex0, unsigned int vertex1, unsigned int vertex2)
{
    TraceModel* m;
    uint64_t id;
    if (!trace_open())
        return;
    m = trace_model();
    id = ++face_sequence;
    if (m != NULL) {
        if (m->face_first == 0)
            m->face_first = id;
        m->face_last = id;
        m->strip_index = strip_index;
        if (strip_index < TRACE_MODEL_STRIPS) {
            if (m->strip_face_first[strip_index] == 0)
                m->strip_face_first[strip_index] = id;
            m->strip_face_last[strip_index] = id;
        }
    }
    if (trace_topology_enabled()) {
        trace_begin("source_face");
        fputs(",\"kind\":", trace_file); trace_json_string(kind);
        fprintf(trace_file, ",\"face_id\":%llu,\"geometry_chunk\":\"%p\",\"strip_index\":%u,\"face_index\":%u,\"source_vertex_indices\":[%u,%u,%u]",
                (unsigned long long)id, chunk, strip_index, face_index, vertex0, vertex1, vertex2);
        trace_end();
    }
}

void pcTexTraceGsRegister(unsigned int reg, uint64_t old_value, uint64_t new_value)
{
    if (!trace_open())
        return;
    if (old_value == new_value)
        return;
    switch (reg) {
    /* TEX0/TEX2, palette and transfer registers only. PRIM, CLAMP and TEX1
     * are represented by the per-draw snapshot and would otherwise flood JSONL. */
    case 0x06: case 0x07: case 0x16: case 0x17: case 0x1c:
    case 0x3b: case 0x50: case 0x51: case 0x52: case 0x53:
        break;
    default:
        return;
    }
    trace_begin("gs_register");
    fprintf(trace_file, ",\"register\":%u,\"old\":\"%016llx\",\"value\":\"%016llx\"",
            reg, (unsigned long long)old_value, (unsigned long long)new_value);
    trace_end();
}

void pcTexTraceGsTransferStart(uint64_t bitbltbuf, uint64_t trxpos,
                               uint64_t trxreg, uint64_t trxdir)
{
    unsigned int dir;
    if (!trace_open())
        return;
    dir = (unsigned int)(trxdir & 3u);
    transfer_sequence++;
    trace_begin("gs_transfer_start");
    fprintf(trace_file, ",\"transfer_id\":%llu,\"direction\":%u,\"BITBLTBUF\":\"%016llx\",\"TRXPOS\":\"%016llx\",\"TRXREG\":\"%016llx\",\"TRXDIR\":\"%016llx\"",
            (unsigned long long)transfer_sequence, dir,
            (unsigned long long)bitbltbuf, (unsigned long long)trxpos,
            (unsigned long long)trxreg, (unsigned long long)trxdir);
    trace_end();
}

static TraceTransferPhase* trace_transfer_phase(const char* phase)
{
    unsigned int i;
    const char* name = phase ? phase : "unknown";
    for (i = 0; i < transfer_phase_count; ++i) {
        if (strcmp(transfer_phases[i].phase, name) == 0)
            return &transfer_phases[i];
    }
    if (transfer_phase_count >= TRACE_TRANSFER_PHASES)
        return NULL;
    {
        TraceTransferPhase* p = &transfer_phases[transfer_phase_count++];
        memset(p, 0, sizeof(*p));
        strncpy(p->phase, name, sizeof(p->phase) - 1);
        p->hash_state = 1469598103934665603ULL;
        return p;
    }
}

static uint64_t trace_hash_update(uint64_t hash, const void* data, size_t bytes)
{
    const unsigned char* p = (const unsigned char*)data;
    size_t i;
    for (i = 0; i < bytes; ++i)
        hash = (hash ^ p[i]) * 1099511628211ULL;
    return hash;
}

void pcTexTraceGsTransferData(const char* phase, const void* data, unsigned int bytes,
                              unsigned int bp, unsigned int bw, unsigned int psm,
                              unsigned int x, unsigned int y,
                              unsigned int width, unsigned int height)
{
    TraceTransferPhase* p;
    if (!trace_open())
        return;
    p = trace_transfer_phase(phase);
    if (p == NULL)
        return;
    p->bytes += bytes;
    p->chunks++;
    p->bp = bp; p->bw = bw; p->psm = psm;
    p->x = x; p->y = y; p->width = width; p->height = height;

    /* Exact hashes and payload files are opt-in; compact mode only counts bytes/chunks. */
    if (trace_dumps_enabled() && data != NULL && bytes != 0) {
        FILE* f;
        char path[160];
        p->hash_state = trace_hash_update(p->hash_state, data, bytes);
        p->hash_valid = 1;
        if (p->dump_path[0] == '\0') {
            snprintf(p->dump_path, sizeof(p->dump_path),
                     "texture_trace/transfer_%03d_%06llu_%s.bin",
                     trace_sessions, (unsigned long long)transfer_sequence, p->phase);
        }
        strncpy(path, p->dump_path, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        f = fopen(path, "ab");
        if (f != NULL) {
            fwrite(data, 1, bytes, f);
            fclose(f);
        }
    }
}

void pcTexTraceGsTransferDone(unsigned int bp, unsigned int bw, unsigned int psm,
                              unsigned int x, unsigned int y,
                              unsigned int width, unsigned int height, int changed)
{
    unsigned int i;
    if (!trace_open())
        return;
    trace_begin("gs_transfer_done");
    fprintf(trace_file, ",\"transfer_id\":%llu,\"bp\":%u,\"bw\":%u,\"psm\":%u,"
                        "\"x\":%u,\"y\":%u,\"width\":%u,\"height\":%u,\"changed\":%s,\"phases\":[",
            (unsigned long long)transfer_sequence, bp, bw, psm, x, y, width, height,
            changed ? "true" : "false");
    for (i = 0; i < transfer_phase_count; ++i) {
        TraceTransferPhase* p = &transfer_phases[i];
        if (i != 0) fputc(',', trace_file);
        fputs("{\"phase\":", trace_file); trace_json_string(p->phase);
        fprintf(trace_file, ",\"bytes\":%llu,\"chunks\":%llu,\"bp\":%u,\"bw\":%u,"
                            "\"psm\":%u,\"x\":%u,\"y\":%u,\"width\":%u,\"height\":%u",
                (unsigned long long)p->bytes, (unsigned long long)p->chunks,
                p->bp, p->bw, p->psm, p->x, p->y, p->width, p->height);
        if (p->hash_valid)
            fprintf(trace_file, ",\"data_hash\":\"%016llx\"", (unsigned long long)p->hash_state);
        if (p->dump_path[0] != '\0') {
            fputs(",\"dump\":", trace_file); trace_json_string(p->dump_path);
        }
        fputc('}', trace_file);
    }
    fputs("]", trace_file);
    trace_end();
    memset(transfer_phases, 0, sizeof(transfer_phases));
    transfer_phase_count = 0;
}

uint64_t pcTexTraceGsPrimitiveQueued(void)
{
    if (!trace_open())
        return 0;
    return ++primitive_sequence;
}

void pcTexTraceGsDraw(uint64_t tex0, uint64_t texa, uint64_t texclut,
                      unsigned int asset_id, uint64_t asset_hash, int asset_valid,
                      unsigned int prim_type, int textured, int vertex_count,
                      unsigned int fbp, unsigned int fbw, unsigned int fpsm,
                      const PcTexTraceDrawContext* context)
{
    if (!trace_open() || !textured)
        return;
    trace_begin("gs_draw");
    fprintf(trace_file, ",\"primitive_type\":%u,\"textured\":%s,\"vertices\":%d,\"TEX0\":\"%016llx\",\"TEXA\":\"%016llx\",\"TEXCLUT\":\"%016llx\",\"TBP0\":%u,\"PSM\":%u,\"CLUT_base\":%u,\"Gindex_hint\":%u,\"asset_hash\":\"%016llx\",\"asset_valid\":%s,\"FBP\":%u,\"FBW\":%u,\"FPSM\":%u",
            prim_type, textured ? "true" : "false", vertex_count,
            (unsigned long long)tex0, (unsigned long long)texa, (unsigned long long)texclut,
            (unsigned int)(tex0 & 0x3FFFu), (unsigned int)((tex0 >> 20) & 0x3Fu),
            (unsigned int)((tex0 >> 37) & 0x3FFFu), asset_id,
            (unsigned long long)asset_hash, asset_valid ? "true" : "false",
            fbp, fbw, fpsm);
    if (context != NULL) {
        fprintf(trace_file, ",\"source_model_id\":%llu,\"source_model\":\"%p\",\"source_chunk\":\"%p\",\"source_chunk_ordinal\":%u,\"source_chunk_type\":%u,\"source_chunk_bits\":%u,\"source_chunk_size\":%u,\"source_strip\":%u,\"source_face_first\":%llu,\"source_face_last\":%llu,\"source_material_seq\":%llu",
                (unsigned long long)context->model_id, context->model, context->chunk,
                context->chunk_ordinal, context->chunk_type, context->chunk_bits,
                context->chunk_size, context->strip_index,
                (unsigned long long)context->face_first,
                (unsigned long long)context->face_last,
                (unsigned long long)context->material_event);
        fprintf(trace_file, ",\"queued_primitive_first\":%llu,\"queued_primitive_last\":%llu,\"queued_primitive_count\":%u",
                (unsigned long long)context->queued_primitive_first,
                (unsigned long long)context->queued_primitive_last,
                context->queued_primitive_count);
        if (context->queued_primitives != NULL && context->queued_primitive_detail_count != 0) {
            unsigned int i;
            fputs(",\"source_groups\":[", trace_file);
            for (i = 0; i < context->queued_primitive_detail_count; ++i) {
                const PcTexTraceSourceGroup* g = &context->queued_primitives[i];
                if (i != 0) fputc(',', trace_file);
                fprintf(trace_file,
                        "[%llu,%llu,%llu,\"%p\",\"%p\",%u,%u,%u,%u,%u,%u,%u,%llu,%llu,%llu]",
                        (unsigned long long)g->sequence_first,
                        (unsigned long long)g->sequence_last,
                        (unsigned long long)g->model_id,
                        g->model, g->chunk, g->chunk_ordinal, g->chunk_type,
                        g->chunk_bits, g->chunk_size, g->strip_index,
                        g->primitive_count, g->vertex_count,
                        (unsigned long long)g->face_first,
                        (unsigned long long)g->face_last,
                        (unsigned long long)g->material_event);
            }
            fputc(']', trace_file);
        }
    }
    trace_end();
}

void pcTexTraceGsTextureResolution(const char* resolution,
                                   unsigned int cache_slot, unsigned int gl_texture,
                                   unsigned int index_slot, unsigned int palette_texture,
                                   int hd, int indexed, uint64_t key0, uint64_t key1,
                                   uint64_t key2, uint64_t generation,
                                   unsigned int asset_id, uint64_t asset_hash,
                                   int width, int height,
                                   const PcTexTraceDrawContext* context)
{
    if (!trace_open())
        return;
    trace_begin("gs_texture_resolution");
    fputs(",\"resolution\":", trace_file); trace_json_string(resolution);
    fprintf(trace_file, ",\"cache_slot\":%u,\"gl_texture\":%u,\"index_slot\":%u,\"palette_texture\":%u,\"hd\":%s,\"indexed\":%s,\"key0\":\"%016llx\",\"key1\":\"%016llx\",\"key2\":\"%016llx\",\"generation\":\"%016llx\",\"asset_id\":%u,\"asset_hash\":\"%016llx\",\"width\":%d,\"height\":%d",
            cache_slot, gl_texture, index_slot, palette_texture, hd ? "true" : "false",
            indexed ? "true" : "false", (unsigned long long)key0,
            (unsigned long long)key1, (unsigned long long)key2,
            (unsigned long long)generation, asset_id, (unsigned long long)asset_hash,
            width, height);
    if (context != NULL) {
        fprintf(trace_file, ",\"source_model_id\":%llu,\"source_chunk\":\"%p\",\"source_chunk_ordinal\":%u,\"source_strip\":%u,\"source_face_first\":%llu,\"source_face_last\":%llu,\"source_material_seq\":%llu",
                (unsigned long long)context->model_id, context->chunk,
                context->chunk_ordinal, context->strip_index,
                (unsigned long long)context->face_first,
                (unsigned long long)context->face_last,
                (unsigned long long)context->material_event);
        fprintf(trace_file, ",\"queued_primitive_first\":%llu,\"queued_primitive_last\":%llu,\"queued_primitive_count\":%u",
                (unsigned long long)context->queued_primitive_first,
                (unsigned long long)context->queued_primitive_last,
                context->queued_primitive_count);
    }
    trace_end();
}

int pcTexTraceGsDumpNeeded(uint64_t tex0, uint64_t texa, uint64_t texclut,
                           uint64_t generation, uint64_t clut_hash, int width, int height)
{
    unsigned int i;
    if (!trace_open() || !trace_dumps_enabled())
        return 0;
    return trace_gs_seen(tex0, texa, texclut, generation, clut_hash, width, height) ? 0 : 1;
}

static void trace_write_u16(FILE* f, unsigned int value)
{
    unsigned char b[2];
    b[0] = (unsigned char)value; b[1] = (unsigned char)(value >> 8);
    fwrite(b, 1, 2, f);
}

static void trace_write_u32(FILE* f, unsigned int value)
{
    unsigned char b[4];
    b[0] = (unsigned char)value; b[1] = (unsigned char)(value >> 8);
    b[2] = (unsigned char)(value >> 16); b[3] = (unsigned char)(value >> 24);
    fwrite(b, 1, 4, f);
}

static const char* trace_image_path(uint64_t hash, unsigned int width, unsigned int height,
                                    int* is_new)
{
    unsigned int i;
    for (i = 0; i < image_dump_count; ++i) {
        if (image_dumps[i].hash == hash && image_dumps[i].width == width &&
            image_dumps[i].height == height) {
            *is_new = 0;
            return image_dumps[i].path;
        }
    }
    if (image_dump_count >= TRACE_IMAGE_DUMPS) {
        *is_new = 0;
        return NULL;
    }
    {
        TraceImageDump* d = &image_dumps[image_dump_count++];
        d->hash = hash; d->width = width; d->height = height;
        snprintf(d->path, sizeof(d->path), "texture_trace/gs_%016llx_%ux%u.bmp",
                 (unsigned long long)hash, width, height);
        *is_new = 1;
        return d->path;
    }
}

static int trace_image_alias_seen(uint64_t hash, unsigned int tbp, unsigned int psm,
                                  unsigned int width, unsigned int height)
{
    unsigned int i;
    for (i = 0; i < image_alias_count; ++i) {
        TraceImageAlias* a = &image_aliases[i];
        if (a->hash == hash && a->tbp == tbp && a->psm == psm &&
            a->width == width && a->height == height)
            return 1;
    }
    if (image_alias_count < TRACE_IMAGE_ALIASES) {
        TraceImageAlias* a = &image_aliases[image_alias_count++];
        a->hash = hash; a->tbp = tbp; a->psm = psm;
        a->width = width; a->height = height;
    }
    return 0;
}

void pcTexTraceGsImage(uint64_t tex0, uint64_t texa, uint64_t texclut,
                       uint64_t generation, uint64_t clut_hash,
                       int width, int height, const unsigned int* rgba)
{
    const char* path;
    FILE* f;
    unsigned int y, x, row_bytes, pad, file_size;
    unsigned int tbp = (unsigned int)(tex0 & 0x3FFFu);
    unsigned int psm = (unsigned int)((tex0 >> 20) & 0x3Fu);
    uint64_t hash;
    int is_new;

    if (!trace_open() || !trace_dumps_enabled() || rgba == NULL || width <= 0 || height <= 0 ||
        width > 1024 || height > 1024)
        return;

    hash = trace_hash(rgba, (size_t)width * (size_t)height * 4u);
    path = trace_image_path(hash, (unsigned int)width, (unsigned int)height, &is_new);
    if (path == NULL)
        return;

    if (is_new) {
        f = fopen(path, "wb");
        if (f == NULL)
            return;
        row_bytes = (unsigned int)width * 3u;
        pad = (4u - (row_bytes & 3u)) & 3u;
        file_size = 54u + (row_bytes + pad) * (unsigned int)height;
        fputc('B', f); fputc('M', f); trace_write_u32(f, file_size); trace_write_u32(f, 0);
        trace_write_u32(f, 54); trace_write_u32(f, 40);
        trace_write_u32(f, (unsigned int)width); trace_write_u32(f, (unsigned int)height);
        trace_write_u16(f, 1); trace_write_u16(f, 24);
        trace_write_u32(f, 0); trace_write_u32(f, file_size - 54);
        trace_write_u32(f, 2835); trace_write_u32(f, 2835);
        trace_write_u32(f, 0); trace_write_u32(f, 0);
        for (y = (unsigned int)height; y-- > 0;) {
            unsigned char zero[3] = { 0, 0, 0 };
            for (x = 0; x < (unsigned int)width; x++) {
                unsigned int p = rgba[(size_t)y * (size_t)width + x];
                unsigned char bgr[3];
                bgr[0] = (unsigned char)(p >> 16);
                bgr[1] = (unsigned char)(p >> 8);
                bgr[2] = (unsigned char)p;
                fwrite(bgr, 1, 3, f);
            }
            if (pad) fwrite(zero, 1, pad, f);
        }
        fclose(f);
    }

    /* A shared image may be referenced from several TBP addresses. Log each unique
     * address alias once, all pointing at the same BMP. */
    if (!trace_image_alias_seen(hash, tbp, psm, (unsigned int)width, (unsigned int)height))
        return;
    trace_begin("gs_texture_dump");
    fprintf(trace_file, ",\"TEX0\":\"%016llx\",\"TEXA\":\"%016llx\",\"TEXCLUT\":\"%016llx\","
                        "\"generation\":\"%016llx\",\"clut_hash\":\"%016llx\","
                        "\"tbp\":%u,\"psm\":%u,\"width\":%d,\"height\":%d,\"rgba_hash\":\"%016llx\",\"duplicate_content\":%s,\"dump\":",
            (unsigned long long)tex0, (unsigned long long)texa,
            (unsigned long long)texclut, (unsigned long long)generation,
            (unsigned long long)clut_hash, tbp, psm, width, height,
            (unsigned long long)hash, is_new ? "false" : "true");
    trace_json_string(path);
    trace_end();
}

}
