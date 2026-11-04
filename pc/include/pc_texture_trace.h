#ifndef PC_TEXTURE_TRACE_H
#define PC_TEXTURE_TRACE_H

#ifdef TARGET_PC
void pcTextureTraceEvent(const char* kind, const char* fmt, ...);
void pcTextureTraceCamera(const char* source, int console_if_changed,
                          int stg, int rom, int rcase, int pos,
                          int ncut, int ocut, int camver, unsigned int camflg,
                          int evc_no, int keyf_no, int mode0,
                          float px, float py, float pz,
                          int ax, int ay, int az, int pers, float ln);
void pcTextureTraceModelBegin(const void* model, const void* vlist,
                              const void* plist, float radius);
void pcTextureTraceModelEnd(int result, const char* reason);
#endif

#endif
