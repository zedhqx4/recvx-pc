/* PC port: movie decoding for pc_movie.c with pl_mpeg (MPEG-1 video + MP2 audio).
 *
 * The disc's MOVIE/MV_*.PSS files are converted once by pc/tools/convert_movies.py into
 * MV_*.MPG next to them. Video frames are picked by wall-clock time since the movie started
 * (minus pauses) so picture and sound stay together even if the game drops a frame; audio is
 * kept a little ahead in its own mixer voice.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PL_MPEG_IMPLEMENTATION
#include "../third_party/pl_mpeg.h"

#include "pc_audio.h"
#include "pc_os.h"
#include "pc_platform.h"

static plm_t* plm;
static int voice = -1;
static double start_time, pause_start, paused_total;
static int paused;
static int volume;              /* 0 .. -960 (0.1 dB) */
static double video_time;       /* time of the newest decoded frame, from the first frame */
static double base_time;
static int ended;
static double audio_fed;        /* seconds of sound queued for the open movie */
static unsigned int video_frames, video_decode_calls, video_waits;
static int decoder_report_second = -1;

/* Small, deterministic sample of the decoded pixels; enough to tell whether frames keep changing
 * without hashing hundreds of kilobytes on every frame. */
static unsigned int frame_sample_hash(const unsigned char* rgba, size_t bytes)
{
    unsigned int hash = 2166136261u, i;

    if (bytes == 0)
    {
        return 0;
    }

    for (i = 0; i < 32; i++)
    {
        size_t at = (bytes - 1) * i / 31;
        hash = (hash ^ rgba[at]) * 16777619u;
    }

    return hash;
}

void pcMpegClose(void);

int pcMpegOpen(const char* path)
{
    pcMpegClose();

    plm = plm_create_with_filename(path);

    if (plm == NULL)
    {
        return 0;
    }

    plm_set_loop(plm, 0);
    plm_set_audio_enabled(plm, plm_get_num_audio_streams(plm) > 0);

    if (plm_get_num_audio_streams(plm) > 0)
    {
        voice = pcAudioOpenVoice();
    }

    start_time = pcOsTime();
    paused = 0;
    paused_total = 0;
    video_time = -1;
    ended = 0;
    audio_fed = 0;
    video_frames = 0;
    video_decode_calls = 0;
    video_waits = 0;
    decoder_report_second = -1;

    /* each movie starts at full volume, as with a new Sofdec player on the PS2; the game only
     * lowers it while fading a skipped movie out, and that must not carry over to the next one */
    volume = 0;

    pcLog("movie: %s (%dx%d, %.1f s)", path, plm_get_width(plm), plm_get_height(plm), plm_get_duration(plm));
    pcLog("movie: %d audio stream(s), mixer voice %d, volume %d", plm_get_num_audio_streams(plm), voice, volume);

    return 1;
}

/* frame size of the open movie; 0 when none is open */
int pcMpegSize(int* w, int* h)
{
    if (plm == NULL)
    {
        return 0;
    }

    *w = plm_get_width(plm);
    *h = plm_get_height(plm);
    return 1;
}

void pcMpegClose(void)
{
    if (plm != NULL)
    {
        pcLog("movie: closed after %.1f s, %.1f s of sound queued, volume %d", pcOsTime() - start_time - paused_total,
              audio_fed, volume);
    }

    if (voice >= 0)
    {
        pcAudioCloseVoice(voice);
        voice = -1;
    }

    if (plm != NULL)
    {
        plm_destroy(plm);
        plm = NULL;
    }
}

void pcMpegPause(int on)
{
    if (on && !paused)
    {
        pause_start = pcOsTime();
    }
    else if (!on && paused)
    {
        paused_total += pcOsTime() - pause_start;
    }

    paused = on;
}

void pcMpegSetVolume(int vol)
{
    volume = vol;
}

static void feed_audio(double ahead)
{
    static short left[1152 * 4], right[1152 * 4];
    int rate = plm_get_samplerate(plm);

    while ((voice >= 0) && (pcAudioQueuedSamples(voice) < (int)(rate * ahead)))
    {
        plm_samples_t* s = plm_decode_audio(plm);
        unsigned int i;

        if (s == NULL)
        {
            break;
        }

        for (i = 0; i < s->count; i++)
        {
            float l = s->interleaved[i * 2] * 32767.0f, r = s->interleaved[i * 2 + 1] * 32767.0f;

            left[i] = (short)((l > 32767) ? 32767 : (l < -32768) ? -32768 : l);
            right[i] = (short)((r > 32767) ? 32767 : (r < -32768) ? -32768 : r);
        }

        pcAudioQueue(voice, left, right, (int)s->count, rate, volume, -15, 15);
        audio_fed += (double)s->count / rate;
    }
}

/* Advance to the frame due now. Returns 1 if rgba (w*h*4, alpha 0x80) holds a new frame,
 * 0 if the current frame stays, -1 when the movie has ended. */
int pcMpegUpdate(unsigned char* rgba, int max_bytes, int* w, int* h)
{
    double now;
    int decoded_this_call = 0;
    plm_frame_t* frame = NULL;

    if (plm == NULL || ended)
    {
        return -1;
    }

    if (paused)
    {
        return 0;
    }

    feed_audio(0.25);

    now = pcOsTime() - start_time - paused_total;

    /* decode until the newest frame is the one due now (always at least the first frame) */
    while ((video_time < 0) || (video_time + 1.0 / plm_get_framerate(plm) <= now))
    {
        plm_frame_t* f;

        video_decode_calls++;
        f = plm_decode_video(plm);

        if (f == NULL)
        {
            /* pl_mpeg can return NULL when the current packet does not yet contain a complete
             * picture. Only plm_has_ended() distinguishes that from a real end of the stream. */
            if (!plm_has_ended(plm))
            {
                video_waits++;
                break;
            }

            ended = 1;
            pcLog("movie: video decoder EOF at %.3f s (last PTS %.3f, decoded %u frames, %u waits)",
                  now, video_time, video_frames, video_waits);

            if (frame == NULL)
            {
                return -1;
            }

            break;
        }

        if (video_time < 0)
        {
            base_time = f->time;
        }

        frame = f;
        video_time = f->time - base_time;
        video_frames++;
        decoded_this_call++;
    }

    if (frame == NULL)
    {
        if ((int)now != decoder_report_second)
        {
            decoder_report_second = (int)now;
            pcLog("movie: decoder hold t=%.2f s, last PTS %.3f, decoded %u frames / %u decode calls / %u waits, audio %.2f s queued",
                  now, video_time, video_frames, video_decode_calls, video_waits, audio_fed);
        }

        return 0;
    }

    if ((int)(frame->width * frame->height * 4) > max_bytes)
    {
        pcLog("movie: decoded frame %ux%u exceeds buffer (%d bytes); frame not presented",
              frame->width, frame->height, max_bytes);
        return 0;
    }

    plm_frame_to_rgba(frame, rgba, frame->width * 4);

    {
        unsigned int i, n = frame->width * frame->height;

        for (i = 0; i < n; i++)
        {
            rgba[i * 4 + 3] = 0x80;
        }
    }

    *w = (int)frame->width;
    *h = (int)frame->height;

    if ((int)now != decoder_report_second)
    {
        decoder_report_second = (int)now;
        pcLog("movie: decoder t=%.2f s PTS=%.3f fps=%.3f frames=%u decode_calls=%u waits=%u (+%d frames this call) hash=%08x audio=%.2f s queued_samples=%d",
              now, video_time, plm_get_framerate(plm), video_frames, video_decode_calls, video_waits, decoded_this_call,
              frame_sample_hash(rgba, (size_t)frame->width * frame->height * 4),
              audio_fed, (voice >= 0) ? pcAudioQueuedSamples(voice) : 0);
    }

    return 1;
}
