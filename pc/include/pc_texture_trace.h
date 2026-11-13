#pragma once

#ifdef TARGET_PC
#include <stdint.h>

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
void pcTexTraceGsTransferData(const void* data, unsigned int bytes,
                              unsigned int bp, unsigned int bw, unsigned int psm,
                              unsigned int x, unsigned int y,
                              unsigned int width, unsigned int height);
void pcTexTraceGsTransferDone(unsigned int bp, unsigned int bw, unsigned int psm,
                              unsigned int x, unsigned int y,
                              unsigned int width, unsigned int height, int changed);
void pcTexTraceGsDraw(uint64_t tex0, uint64_t texa, uint64_t texclut,
                      unsigned int asset_id, uint64_t asset_hash, int asset_valid,
                      unsigned int prim_type, int textured, int vertex_count,
                      unsigned int fbp, unsigned int fbw, unsigned int fpsm);

int pcTexTraceGsDumpNeeded(uint64_t tex0, uint64_t texa, uint64_t texclut,
                           uint64_t generation, uint64_t clut_hash, int width, int height);
void pcTexTraceGsImage(uint64_t tex0, uint64_t texa, uint64_t texclut,
                       uint64_t generation, uint64_t clut_hash,
                       int width, int height, const unsigned int* rgba);
#endif
