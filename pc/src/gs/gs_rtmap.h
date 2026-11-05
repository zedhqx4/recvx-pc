/* PC port, GS emulator: which part of a render target a GS-memory read actually consumes.
 *
 * gsFlushLocalToLocal() copies rendered pixels back into GS memory before a local->local or
 * local->host transfer reads them. Copying the whole render target for every transfer is
 * very expensive (effects such as the water ripple copy the screen in many small pieces), so when
 * the address mapping between the transfer's source buffer and the render target can be proven
 * to be a plain translation, only the requested rectangle is copied.
 *
 * Pure function: no GL, no GS memory, so it can be tested on the host (pc/tests). */
#pragma once

#define GS_RTMAP_NONE    0   /* the rectangle consumes no pixel of this render target */
#define GS_RTMAP_REGION  1   /* out[] = x0, y0, x1, y1 in render target pixels */
#define GS_RTMAP_FULL    2   /* mapping not provable here: copy everything that was drawn */

/* rt_*: render target (width in pixels, rows drawn so far, FBP in pages, FBW in 64 px units,
 * pixel format). bp/bw/psm/x/y/w/h: the GS read (bp in 256-byte blocks). */
static int gsRtReadRegion(int rt_w, int rows, unsigned int rt_fbp, unsigned int rt_fbw, int rt_psm_ct32,
                          unsigned int bp, unsigned int bw, int psm_ct32,
                          unsigned int x, unsigned int y, unsigned int w, unsigned int h, int out[4])
{
    if (psm_ct32 && rt_psm_ct32 && (bw != 0) && (bw == rt_fbw) && (rt_w == (int)(bw * 64u)) && ((bp & 31u) == 0) &&
        ((unsigned long long)x + (unsigned long long)w <= (unsigned long long)bw * 64ULL))
    {
        int page_delta = (int)rt_fbp - (int)(bp / 32u);

        if ((page_delta % (int)bw) == 0)
        {
            int rt_source_y = (page_delta / (int)bw) * 32;
            int req_x0 = (int)x, req_x1 = (int)(x + w);
            int req_y0 = (int)y, req_y1 = (int)(y + h);
            int clip_y0 = (req_y0 > rt_source_y) ? req_y0 : rt_source_y;
            int clip_y1 = (req_y1 < rt_source_y + rows) ? req_y1 : rt_source_y + rows;
            int x0 = (req_x0 > 0) ? req_x0 : 0;
            int x1 = (req_x1 < rt_w) ? req_x1 : rt_w;

            if ((x0 >= x1) || (clip_y0 >= clip_y1))
            {
                return GS_RTMAP_NONE;
            }

            out[0] = x0;
            out[1] = clip_y0 - rt_source_y;
            out[2] = x1;
            out[3] = clip_y1 - rt_source_y;

            return GS_RTMAP_REGION;
        }
    }

    return GS_RTMAP_FULL;
}
