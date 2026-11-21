#include "../../../ps2/veronica/prog/ps2_NaMath.h"
#include "../../../ps2/veronica/prog/main.h"

static float SinTable[16384];

// 100% matching! 
void _Make_SinTable()
{ 
    int i;

    for (i = 0; i < 16384; i++) 
    { 
        SinTable[i] = sinf(0.0000958738f * i); 
    } 
}  

// 100% matching!
Float	njSin(Angle n)
{
    float ret;
    
    {
        int a = n;
        int i = a & 16383;

        ret = (a & 0x4000) ? SinTable[16383 - i] : SinTable[i];

        if (a & 0x8000)
        {
            ret = -ret;
        }
    }
    return ret;
}

// 100% matching!
Float	njCos(Angle n)
{
	float ret;

	{
	    int a = n;
	    int i = a & 16383;

	    if (a & 0x4000)
	    {
	        ret = SinTable[i];

	        if (!(a & 0x8000))
	        {
	            ret = -ret;
	        }
	    }
	    else
	    {
	        ret = SinTable[16383 - i];

	        if (a & 0x8000)
	        {
	            ret = -ret;
	        }
	    }
	}
    return ret;
}

// 100% matching!
void njSinCos(int lAngle, float* sin, float* cos)
{
    {
        int a = lAngle;
        int i = a & 16383;
        float s, c;

        if (a & 0x4000)
        {
            s = SinTable[16383 - i];
            c = SinTable[i];

            if (a & 0x8000)
            {
                s = -s;
            }
            else
            {
                c = -c;
            }
        }
        else
        {
            s = SinTable[i];
            c = SinTable[16383 - i];

            if (a & 0x8000)
            {
                s = -s;
                c = -c;
            }
        }

        *sin = s;
        *cos = c;
    }
}

// 100% matching! 
Float	njFraction  (Float n) 
{ 
    return n - floorf(n); 
} 

// 100% matching! 
Float	njSqrt(Float n)
{
	float ret;

	ret = 0;
    
    ret = sqrtf(fabsf(n));

    return ret;
}

// 100% matching! 
Float	njInvertSqrt(Float n)
{
    float ret;

    ret = 0;
    
    ret = pcVu0Rsqrt(n);

    return ret;
}

// 100% matching! 
void	njLinear(Float *idata, Float *odata, NJS_SPLINE *attr, Float frame)
{
	odata[0] = (idata[3] - idata[0]) * frame + idata[0];
	odata[1] = (idata[4] - idata[1]) * frame + idata[1];
	odata[2] = (idata[5] - idata[2]) * frame + idata[2];
}

// 100% matching! 
void	njOverhauserSpline(Float *idata, Float *odata, NJS_SPLINE *attr, Float frame)
{
	float ftmp; 

	ftmp = 0.5f;

	{
	    float t = frame;
	    float t2 = t * t;
	    float t3 = t2 * t;
	    float w0 = ((t2 + t2) - t3) - t;
	    float w1 = ((t3 * 3.0f) - (t2 * 5.0f)) + 2.0f;
	    float w2 = ((t2 * 4.0f) + t) + (t3 * -3.0f);
	    float w3 = t3 - t2;
	    int k;

	    for (k = 0; k < 3; k++)
	    {
	        odata[k] = (((idata[k] * w0 + idata[3 + k] * w1) + idata[6 + k] * w2) + idata[9 + k] * w3) * ftmp;
	    }
	}
}

// 100% matching! 
void	njBezierSpline(Float *idata, Float *odata, NJS_SPLINE *attr, Float frame) 
{
    unsigned int ulCnt; 
    unsigned int ulMax; 
    float fFactMax;     
    float fResult;      
    
    odata[0] = 0;
    odata[1] = 0;
    odata[2] = 0;
    
    ulMax = *attr->iparam - 1;
    
    fResult = njFactorial(ulMax);

    for (ulCnt = 0; ulCnt <= ulMax; ulCnt++)  
    {            
        fFactMax = (powf(frame, ulCnt) * (fResult / (njFactorial(ulCnt) * njFactorial(ulMax - ulCnt)))) * powf(1.0f - frame, ulMax - ulCnt);
        
        odata[0] += fFactMax * *idata++;
        odata[1] += fFactMax * *idata++;
        odata[2] += fFactMax * *idata++; 
    }
}

// 100% matching! 
unsigned int njFactorial(unsigned int ulN) 
{ 
    unsigned int ulResult;

    ulResult = 1;
    
    for ( ; ulN != 0; ulN--)
    { 
        ulResult *= ulN; 
    }
    
    return ulResult; 
}
