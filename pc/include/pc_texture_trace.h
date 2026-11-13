#pragma once

#ifdef TARGET_PC
#include <stdint.h>

/* Compact tuple written once per contiguous source-context group:
 * seq_first,seq_last,model_id,model_ptr,chunk_ptr,chunk_ordinal,chunk_type,
 * chunk_bits,chunk_size,strip,primitive_count,vertex_count,face_first,
 * face_last,material_event. */
typedef struct PcTexTraceSourceGroup {
    uint64_t sequence_first;
    uint64_t sequence_last;
    uint64_t model_id;
    const void* model;
    const void* chunk;
    unsigned int chunk_ordinal;
    unsigned int chunk_type;
    unsigned int chunk_bits;
    unsigned int chunk_size;
    unsigned int strip_index;
    unsigned int primitive_count;
    unsigned int vertex_count;
    uint64_t face_first;
    uint64_t face_last;
    uint64_t material_event;
} PcTexTraceSourceGroup;

typedef struct PcTexTraceDrawContext {
    uint64_t model_id;
    const void* model;
    const void* chunk;
    unsigned int chunk_ordinal;
    unsigned int chunk_type;
    unsigned int chunk_bits;
    unsigned int chunk_size;
    unsigned int strip_index;
    uint64_t face_first;
    uint64_t face_last;
    uint64_t material_event;
    uint64_t queued_primitive_first;
    uint64_t queued_primitive_last;
    unsigned int queued_primitive_count;
    int tracing_enabled;
    const PcTexTraceSourceGroup* queued_primitives;
    unsigned int queued_primitive_detail_count;
} PcTexTraceDrawContext;

int pcTexTraceEnabled(void);
int pcTexTraceToggle(void);
int pcTexTraceDumpsEnabled(void);
void pcTexTraceSnapshotDrawContext(PcTexTraceDrawContext* out);
void pcTexTraceSetFrame(uint64_t frame);
void pcTexTraceSetResource(const char* kind, const char* name,
                           int stage, int room, int room_case,
                           const void* resource_base, const void* data,
                           unsigned int resource_index, int resource_offset);
void pcTexTraceClearResource(void);
void pcTexTraceTextureBlock(const void* texlist, const void* data, unsigned int texture_offset);

void pcTexTraceDumpTim2(const char* role, const void* data, unsigned int size,
                       unsigned int gindex, unsigned int bank,
                       unsigned int pict_format, unsigned int image_type,
                       unsigned int clut_type, unsigned int width, unsigned int height);
void pcTexTraceTextureRegistration(const char* operation, const void* texlist,
                                   unsigned int list_index, const void* texname,
                                   const void* source_info, const void* source_tim2,
                                   const void* entry, const void* resident_tim2,
                                   unsigned int gindex, unsigned int bank,
                                   unsigned int count, unsigned int tp_flag);
void pcTexTraceTextureUse(const char* operation, unsigned int requested_index,
                          const void* texlist, unsigned int list_count,
                          const void* entry, const void* resident_tim2,
                          unsigned int gindex, unsigned int bank,
                          unsigned int ref_count, unsigned int tp_flag);

uint64_t pcTexTraceModelBegin(const void* model, const void* vlist, const void* plist);
void pcTexTraceModelEnd(uint64_t model_id);
void pcTexTraceModelChunk(const char* phase, const void* chunk,
                          unsigned int ordinal, unsigned int type,
                          unsigned int head_bits, unsigned int size);
void pcTexTraceStrip(unsigned int strip_index, unsigned int vertex_count);
void pcTexTraceFace(const char* kind, const void* chunk,
                    unsigned int strip_index, unsigned int face_index,
                    unsigned int vertex0, unsigned int vertex1, unsigned int vertex2);

void pcTexTraceGsRegister(unsigned int reg, uint64_t old_value, uint64_t new_value);
void pcTexTraceGsTransferStart(uint64_t bitbltbuf, uint64_t trxpos,
                               uint64_t trxreg, uint64_t trxdir);
void pcTexTraceGsTransferData(const char* phase, const void* data, unsigned int bytes,
                              unsigned int bp, unsigned int bw, unsigned int psm,
                              unsigned int x, unsigned int y,
                              unsigned int width, unsigned int height);
void pcTexTraceGsTransferDone(unsigned int bp, unsigned int bw, unsigned int psm,
                              unsigned int x, unsigned int y,
                              unsigned int width, unsigned int height, int changed);
uint64_t pcTexTraceGsPrimitiveQueued(void);
void pcTexTraceGsDraw(uint64_t tex0, uint64_t texa, uint64_t texclut,
                      unsigned int asset_id, uint64_t asset_hash, int asset_valid,
                      unsigned int prim_type, int textured, int vertex_count,
                      unsigned int fbp, unsigned int fbw, unsigned int fpsm,
                      const PcTexTraceDrawContext* context);
void pcTexTraceGsTextureResolution(const char* resolution,
                                   unsigned int cache_slot, unsigned int gl_texture,
                                   unsigned int index_slot, unsigned int palette_texture,
                                   int hd, int indexed, uint64_t key0, uint64_t key1,
                                   uint64_t key2, uint64_t generation,
                                   unsigned int asset_id, uint64_t asset_hash,
                                   int width, int height,
                                   const PcTexTraceDrawContext* context);

int pcTexTraceGsDumpNeeded(uint64_t tex0, uint64_t texa, uint64_t texclut,
                           uint64_t generation, uint64_t clut_hash, int width, int height);
void pcTexTraceGsImage(uint64_t tex0, uint64_t texa, uint64_t texclut,
                       uint64_t generation, uint64_t clut_hash,
                       int width, int height, const unsigned int* rgba);
#endif
