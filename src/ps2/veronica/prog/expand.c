#include "../../../ps2/veronica/prog/expand.h"

EXPAND_CTRL_BUF ExpandCtrlBuf;

// 100% matching!
void Init_Expand() 
{ 
    ExpandCtrlBuf.flag.abort = 0; 
}

/* C version of Expand() provided by Clownacy (see the comment at the end of this file),
 * checked against the MIPS routine below. */
typedef struct Expand_State
{
    const unsigned char* source;
    unsigned char descriptor_field;
    unsigned char descriptor_bits_remaining;
} Expand_State;

static void Expand_RefreshDescriptorField(Expand_State* const state)
{
    state->descriptor_field = *state->source++;
    state->descriptor_bits_remaining = 8;
}

static int Expand_GetDescriptorBit(Expand_State* const state)
{
    int bit;

    if (--state->descriptor_bits_remaining == 0)
        Expand_RefreshDescriptorField(state);

    bit = state->descriptor_field & 1;
    state->descriptor_field >>= 1;

    return bit;
}

int Expand(char* s, unsigned char* d)
{
    unsigned char* destination = d;
    Expand_State state;

    state.source = (unsigned char*)s;
    Expand_RefreshDescriptorField(&state);

    ++state.descriptor_bits_remaining;

    for (;;)
    {
        if (Expand_GetDescriptorBit(&state))
        {
            *destination++ = *state.source++;
        }
        else
        {
            unsigned int length = 0;
            int offset;

            if (!Expand_GetDescriptorBit(&state))
            {
                if (Expand_GetDescriptorBit(&state))
                    length += 2;

                if (Expand_GetDescriptorBit(&state))
                    ++length;

                ++length;

                offset = -0x100 + *state.source++;
            }
            else
            {
                const unsigned int lower = *state.source++;
                const unsigned int upper = *state.source++;
                const unsigned int whole = lower | (upper << 8);

                if (whole == 0)
                    break;

                offset = -0x2000 + (whole >> 3);
                length = whole & 7;

                if (length != 0)
                    ++length;
                else
                    length = *state.source++;
            }

            do
            {
                *destination = destination[offset];
                ++destination;
            } while (length-- != 0);
        }
    }

    return destination - d;
}

/* Following is a C version of Expand() provided by Clownacy */
/*typedef struct Expand_State
{
    const unsigned char *source;
    unsigned char descriptor_field;
    unsigned char descriptor_bits_remaining;
} Expand_State;
 
static void Expand_RefreshDescriptorField(Expand_State* const state)
{
    state->descriptor_field = *state->source++;
    state->descriptor_bits_remaining = 8;
}
 
static bool Expand_GetDescriptorBit(Expand_State* const state)
{
    bool bit;
 
    if (--state->descriptor_bits_remaining == 0)
        Expand_RefreshDescriptorField(state);
 
    bit = state->descriptor_field & 1;
    state->descriptor_field >>= 1;
 
    return bit;
}
 
int Expand(register char* s, register unsigned char* d)
{
    unsigned char *destination = d;
    Expand_State state;
 
    state.source = (unsigned char*)s;
    Expand_RefreshDescriptorField(&state);
 
    // This seems like a bug, but the game's data will not decompress correctly without it.
    ++state.descriptor_bits_remaining;
 
    for (;;)
    {
        if (Expand_GetDescriptorBit(&state))
        {
            *destination++ = *state.source++;
        }
        else
        {
            unsigned int length = 0;
            int offset;
 
            if (!Expand_GetDescriptorBit(&state))
            {
                if (Expand_GetDescriptorBit(&state))
                    length += 2;
 
                if (Expand_GetDescriptorBit(&state))
                    ++length;
 
                ++length;
 
                offset = -0x100 + *state.source++;
            }
            else
            {
                const unsigned int lower = *state.source++;
                const unsigned int upper = *state.source++;
                const unsigned int whole = lower | (upper << 8);
 
                if (whole == 0)
                    break;
 
                offset = -0x2000 + (whole >> 3);
                length = whole & 7;
 
                if (length != 0)
                    ++length;
                else
                    length = *state.source++;
            }
 
            do
            {
                *destination = destination[offset];
                ++destination;
            } while (length-- != 0);
        }
    }
 
    return destination - d;
}*/
