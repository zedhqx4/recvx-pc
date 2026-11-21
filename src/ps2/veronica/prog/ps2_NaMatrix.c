#include "../../../ps2/veronica/prog/ps2_NaMatrix.h"
#include "../../../ps2/veronica/prog/ps2_NaFog.h"
#include "../../../ps2/veronica/prog/ps2_NaView.h"
#include "../../../ps2/veronica/prog/main.h"
#include "pc_clip.h"

int lNaMatIsUnitMatrix;
int lNaMatMatrixStuckMax;
int lNaMatMatrixStuckCnt;
NJS_MATRIX* pNaMatMatrixStuckPtr;
NJS_MATRIX* pNaMatMatrixStuckTop;
/* unused below */
/*NJS_MATRIX TempMatrix0;
NJS_MATRIX TempMatrix1;*/

extern void VU0_INIT_CALC_PROCESS() __attribute__((section(".vutext")));

// 98.85% matching
void	njInitMatrix(NJS_MATRIX *m, Sint32 n, Int flag)
{
    register float pi;

    pNaMatMatrixStuckTop = m;
    pNaMatMatrixStuckPtr = m; 
    
    lNaMatMatrixStuckCnt = 0; 
    lNaMatMatrixStuckMax = n; 
    
    lNaMatIsUnitMatrix = flag; 
    
    pi = 3.141592f;
    
    pcClipResetHistory(); /* VU0_INIT_CALC_PROCESS */
}

// 100% matching
void	njCalcPoints(NJS_MATRIX *m, NJS_POINT3 *ps, NJS_POINT3 *pd, Int num)
{
	if (m == NULL)
    {
        m = pNaMatMatrixStuckPtr;
    }

    {
        float* fm = (float*)m;
        int n = num;

        do
        {
            pcVu0XformPoint(fm, (float*)ps, pcVu0Last);

            pd->x = pcVu0Last[0];
            pd->y = pcVu0Last[1];
            pd->z = pcVu0Last[2];

            ps++;
            pd++;
        } while (--n > 0);
    }
}

// 100% matching! 
void    njGetTranslation(NJS_MATRIX *m, NJS_POINT3 *p)
{
    if (m == NULL)
    {
        m = pNaMatMatrixStuckPtr;
    }

    p->x = (*m)[12];
    p->y = (*m)[13];
    p->z = (*m)[14];
}

// 100% matching!
void    njUnitTransPortion(NJS_MATRIX *m)
{
	if (m == NULL)
    {
        m = pNaMatMatrixStuckPtr;
    }

    (*m)[12] = 0;
    (*m)[13] = 0;
    (*m)[14] = 0;
    (*m)[15] = 1.0f;
}

// 100% matching! 
void    njUnitRotPortion(NJS_MATRIX *m)
{
    if (m == NULL)
    {
        m = pNaMatMatrixStuckPtr;
    }

    {
        float* fm = (float*)m;
        int i;

        for (i = 0; i < 12; i++)
        {
            fm[i] = ((i % 5) == 0) ? 1.0f : 0;
        }
    }
}

// 100% matching!
void    njClearMatrix()
{
    lNaMatMatrixStuckCnt = 0;

    pNaMatMatrixStuckPtr = pNaMatMatrixStuckTop;
    
    njSetMatrix(NULL, &NaViwViewMatrix);
}

// 100% matching!
Bool	njPushMatrix(NJS_MATRIX *m)
{
	NJS_MATRIX* fpSrc;
    NJS_MATRIX* fpDst;

    if (lNaMatMatrixStuckMax <= lNaMatMatrixStuckCnt) 
    {
        return FALSE;
    }
    
    lNaMatMatrixStuckCnt++;

    if (m == NULL) 
    {
        fpSrc = pNaMatMatrixStuckPtr;
    }
    else 
    {
        fpSrc = m;
    }

    pNaMatMatrixStuckPtr++;
    
    fpDst = pNaMatMatrixStuckPtr;
    
    pcCopyMatrix((float*)fpDst, (const float*)fpSrc);

    return TRUE;
}

// 100% matching!
Bool	njPopMatrix(Uint32 n)
{
    int lNumber;

    lNumber = lNaMatMatrixStuckCnt - n;
    
    if (lNumber < 0) 
    {
        return FALSE;
    }
    
    lNaMatMatrixStuckCnt = lNumber;
    
    pNaMatMatrixStuckPtr -= n;
    
    return TRUE;
}

// 100% matching!
void	njUnitMatrix(register NJS_MATRIX *m)
{
    if (m == NULL) 
    {
        m = pNaMatMatrixStuckPtr;
    }
    
    {
        float* fm = (float*)m;
        int i;

        for (i = 0; i < 16; i++)
        {
            fm[i] = ((i % 5) == 0) ? 1.0f : 0;
        }
    }
}

// 100% matching!
void	njSetMatrix(NJS_MATRIX *md, NJS_MATRIX *ms)
{
    register NJS_MATRIX* fpSrc; 
    register NJS_MATRIX* fpDst; 

    fpSrc = ms;
    fpDst = md ? md : pNaMatMatrixStuckPtr;

    pcCopyMatrix((float*)fpDst, (const float*)fpSrc);
}

// 100% matching!
void njSetMatrixCN(NJS_MATRIX* pMat)
{
    pcCopyMatrix((float*)pcVu0CN, (const float*)pMat);
}

// 100% matching!
void	njGetMatrix(NJS_MATRIX *m)
{
    register NJS_MATRIX* fpSrc; 
    register NJS_MATRIX* fpDst; 

    fpSrc = pNaMatMatrixStuckPtr;
    fpDst = m; 
    
    pcCopyMatrix((float*)fpDst, (const float*)fpSrc);
}

// 100% matching!
void	njMultiMatrix(NJS_MATRIX *md, NJS_MATRIX *ms)
{
    if (md == NULL)
    {
        md = pNaMatMatrixStuckPtr;
    }

    {
        float* d = (float*)md;
        float* s = (float*)ms;
        float r[16];
        int i;

        for (i = 0; i < 3; i++)
        {
            pcVu0XformVector(d, &s[i * 4], &r[i * 4]);
            r[i * 4 + 3] = s[i * 4 + 3];
        }

        pcVu0XformPoint(d, &s[12], &r[12]);
        r[15] = s[15];

        pcCopyMatrix(d, r);
    }
}

// 100% matching!
void	njTranslate(NJS_MATRIX *m, Float x, Float y, Float z)
{
    if (m == NULL)
    {
        m = pNaMatMatrixStuckPtr;
    }

    {
        float* fm = (float*)m;
        float v[3];

        v[0] = x;
        v[1] = y;
        v[2] = z;

        pcVu0XformPoint(fm, v, &fm[12]);
    }
}

// 100% matching!
void	njTranslateV(NJS_MATRIX *m, NJS_VECTOR *v)
{
	if (m == NULL)
    {
        m = pNaMatMatrixStuckPtr;
    }

    {
        float* fm = (float*)m;

        pcVu0XformPoint(fm, (float*)v, &fm[12]);
    }
}

// 100% matching!
void	njRotateX(NJS_MATRIX *m, Angle ang)
{
    float fSin;
    float fCos;

    ang &= 0xFFFF;
    
    njSinCos(ang, &fSin, &fCos);
    
    if (m == NULL)
    {
        m = pNaMatMatrixStuckPtr;
    }

    pcRotRows(&(*m)[4], &(*m)[8], fSin, fCos);
}

// 100% matching!
void	njRotateY(NJS_MATRIX *m, Angle ang)
{
    float fSin;
    float fCos;

    ang &= 0xFFFF;
    
    njSinCos(ang, &fSin, &fCos);
    
    if (m == NULL)
    {
        m = pNaMatMatrixStuckPtr;
    }

    pcRotRows(&(*m)[0], &(*m)[8], -fSin, fCos);
}

// 100% matching!
void	njRotateZ(NJS_MATRIX *m, Angle ang)
{
    float fSin;
    float fCos;

    ang &= 0xFFFF;
    
    njSinCos(ang, &fSin, &fCos);
    
    if (m == NULL)
    {
        m = pNaMatMatrixStuckPtr;
    }

    pcRotRows(&(*m)[0], &(*m)[4], fSin, fCos);
}

// 100% matching!
void	njRotateXYZ(NJS_MATRIX *m, Angle angx, Angle angy, Angle angz)
{
    if (m == NULL) 
    {
        m = pNaMatMatrixStuckPtr;
    }
    
    njRotateZ(m, angz);
    njRotateY(m, angy);
    njRotateX(m, angx);
}

// 100% matching!
void njRotXYZ(NJS_MATRIX* pMatrix, int lAngleX, int lAngleY, int lAngleZ)
{
    float fSin;
    float fCos;

    lAngleX &= 0xFFFF;
    
    njSinCos(lAngleX, &fSin, &fCos);
    
    pcRotRows(&(*pMatrix)[4], &(*pMatrix)[8], fSin, fCos);

    lAngleY &= 0xFFFF;
    
    njSinCos(lAngleY, &fSin, &fCos);
    
    pcRotRows(&(*pMatrix)[0], &(*pMatrix)[8], -fSin, fCos);

    lAngleZ &= 0xFFFF;
    
    njSinCos(lAngleZ, &fSin, &fCos);
    
    pcRotRows(&(*pMatrix)[0], &(*pMatrix)[4], fSin, fCos);
}

// 100% matching!
void	njRotate(NJS_MATRIX *m, NJS_VECTOR *v, Angle ang)
{
    float fSin;
	float fCos;

    if (m == NULL)
    {
        m = pNaMatMatrixStuckPtr;
    }

    ang &= 0xFFFF;
	ang /= 2;
    
    njSinCos(ang, &fSin, &fCos);

    {
        float* fm = (float*)m;
        float x = v->x, y = v->y, z = v->z;
        float q = pcVu0Rsqrt((x * x + y * y) + z * z) * fSin;
        float w = fCos;
        float w2, r[9];
        int i;

        x *= q;
        y *= q;
        z *= q;

        w2 = w * w;

        r[0] = ((w2 + x * x) - y * y) - z * z;
        r[1] = (x * y + w * z) * 2;
        r[2] = (x * z - w * y) * 2;

        r[3] = (x * y - w * z) * 2;
        r[4] = ((w2 - x * x) + y * y) - z * z;
        r[5] = (y * z + w * x) * 2;

        r[6] = (x * z + w * y) * 2;
        r[7] = (y * z - w * x) * 2;
        r[8] = ((w2 - x * x) - y * y) + z * z;

        {
            float o[12];

            for (i = 0; i < 3; i++)
            {
                pcVu0XformVector(fm, &r[i * 3], &o[i * 4]);
            }

            for (i = 0; i < 3; i++)
            {
                fm[i * 4 + 0] = o[i * 4 + 0];
                fm[i * 4 + 1] = o[i * 4 + 1];
                fm[i * 4 + 2] = o[i * 4 + 2];
            }
        }
    }
}

// 100% matching!
void	njScale(NJS_MATRIX *m, Float sx, Float sy, Float sz)
{
    if (m == NULL)
    {
        m = pNaMatMatrixStuckPtr;
    }

    {
        float* fm = (float*)m;
        int i;

        for (i = 0; i < 3; i++)
        {
            fm[0 + i] *= sx;
            fm[4 + i] *= sy;
            fm[8 + i] *= sz;
        }
    }
}

// 100% matching!
void	njScaleV(NJS_MATRIX *m, NJS_VECTOR *v)
{
	if (m == NULL)
    {
        m = pNaMatMatrixStuckPtr;
    }

    {
        float* fm = (float*)m;
        int i;

        for (i = 0; i < 3; i++)
        {
            fm[0 + i] *= v->x;
            fm[4 + i] *= v->y;
            fm[8 + i] *= v->z;
        }
    }
}

// 100% matching!
Bool	njInvertMatrix(NJS_MATRIX *m)
{
	if (m == NULL)
    {
        m = pNaMatMatrixStuckPtr;
    }

    {
        float* fm = (float*)m;
        float r[16];
        int i, j;

        for (i = 0; i < 3; i++)
        {
            for (j = 0; j < 3; j++)
            {
                r[i * 4 + j] = fm[j * 4 + i];
            }

            r[i * 4 + 3] = 0;
        }

        pcVu0XformVector(r, &fm[12], &r[12]);

        r[12] = 0 - r[12];
        r[13] = 0 - r[13];
        r[14] = 0 - r[14];
        r[15] = fm[15];

        pcCopyMatrix(fm, r);
    }

    return TRUE;
}

// 100% matching!
void	njTransposeMatrix(NJS_MATRIX *m)
{
	if (m == NULL)
    {
        m = pNaMatMatrixStuckPtr;
    }

    {
        float* fm = (float*)m;
        float t;

        t = fm[1]; fm[1] = fm[4]; fm[4] = t;
        t = fm[2]; fm[2] = fm[8]; fm[8] = t;
        t = fm[6]; fm[6] = fm[9]; fm[9] = t;
    }
}

// 100% matching!
static float njAtan2b(float a, float b)
{
    return atan2(a, b);
}

// 100% matching!
void	njMirror(NJS_MATRIX *m,NJS_PLANE *pl)
{
    NJS_MATRIX mat; 
    int lAngleX;   
    int lAngleZ;   
    
    if (m == NULL)
    {
        m = pNaMatMatrixStuckPtr;
    }

    {
        float vx = pl->vx, vy = pl->vy, vz = pl->vz;
        float inv = pcVu0Rsqrt((vx * vx + vy * vy) + vz * vz);
        float d = (vx * inv) * pl->px + (vy * inv) * pl->py + (vz * inv) * pl->pz;

        mat[12] = vx * (d * 2);
        mat[13] = vy * (d * 2);
        mat[14] = vz * (d * 2);
        mat[15] = 1.0f;

        lAngleX = (int)((njAtan2b(vz, vy) * 32768.0f) / 3.141592f);
        lAngleZ = -(int)((njAtan2b(vx, sqrtf(vy * vy + vz * vz)) * 32768.0f) / 3.141592f);
    }

    l_002D7450:
    njUnitRotPortion(&mat);
    
    njRotateX(&mat, lAngleX);
    njRotateZ(&mat, lAngleZ);
    
    njScale(&mat, 1.0f, -1.0f, 1.0f);
    
    njRotateZ(&mat, -lAngleZ);
    njRotateX(&mat, -lAngleX);
    
    njMultiMatrix(m, &mat);
}

// 100% matching!
void	njCalcPoint(NJS_MATRIX *m, NJS_POINT3 *ps, NJS_POINT3 *pd)
{
    if (m == NULL)
    {
        m = pNaMatMatrixStuckPtr;
    }

    pcVu0XformPoint((float*)m, (float*)ps, pcVu0Last);

    pd->x = pcVu0Last[0];
    pd->y = pcVu0Last[1];
    pd->z = pcVu0Last[2];
}

// 100% matching!
void njCalcPoint4(NJS_MATRIX* pMatrix, NJS_POINT4* pSrcPoint, NJS_POINT4* pDstPoint)
{
    if (pMatrix == NULL)
    {
        pMatrix = pNaMatMatrixStuckPtr;
    }

    pcVu0XformPoint((float*)pMatrix, (float*)pSrcPoint, pcVu0Last);

    pDstPoint->x = pcVu0Last[0];
    pDstPoint->y = pcVu0Last[1];
    pDstPoint->z = pcVu0Last[2];
    pDstPoint->w = pSrcPoint->w;
}

// 100% matching!
void njCalcPointCN(NJS_POINT3* pSrcPoint, NJS_POINT3* pDstPoint)
{
    pcVu0XformPoint(pcVu0CN, (float*)pSrcPoint, pcVu0Last);

    pDstPoint->x = pcVu0Last[0];
    pDstPoint->y = pcVu0Last[1];
    pDstPoint->z = pcVu0Last[2];
}

// 100% matching!
void	njAddVector(NJS_VECTOR *vd, NJS_VECTOR *vs)
{
	vd->x = vd->x + vs->x;
	vd->y = vd->y + vs->y;
	vd->z = vd->z + vs->z;
}

// 100% matching!
void	njSubVector(NJS_VECTOR *vd, NJS_VECTOR *vs)
{
	vd->x = vd->x - vs->x;
	vd->y = vd->y - vs->y;
	vd->z = vd->z - vs->z;
}

// 100% matching!
void	njCalcVector(NJS_MATRIX *m, NJS_VECTOR *vs, NJS_VECTOR *vd)
{
    if (m == NULL)
    {
        m = pNaMatMatrixStuckPtr;
    }

    {
        float r[3];

        pcVu0XformVector((float*)m, (float*)vs, r);

        vd->x = r[0];
        vd->y = r[1];
        vd->z = r[2];
    }
}

// 100% matching!
Float	njUnitVector(NJS_VECTOR *v)
{
	float ret;

    {
        float l2 = (v->x * v->x + v->y * v->y) + v->z * v->z;
        float q = pcVu0Rsqrt(l2);

        v->x *= q;
        v->y *= q;
        v->z *= q;

        ret = l2 * q;
    }

    return ret;
}

// 100% matching!
Float	njScalor(NJS_VECTOR *v)
{
	float ret;

    ret = sqrtf((v->x * v->x + v->y * v->y) + v->z * v->z);

    return ret;
}

// 100% matching!
Float	njScalor2(NJS_VECTOR *v)
{
	float ret;

    ret = (v->x * v->x + v->y * v->y) + v->z * v->z;

    return ret;
}

// 100% matching!
void	njProjectScreen(NJS_MATRIX *m, NJS_POINT3 *p3, NJS_POINT2 *p2)
{
    NJS_POINT3 Point;
    
    if (m == NULL)
    {
        m = pNaMatMatrixStuckPtr;
    }
    
    njMulMatrixCN(&NaViewScreenMatrix, m);
    
    njCalcPointCN(p3, &Point);
    
    {
        float q = pcVu0Div(_nj_screen_.dist, pcVu0Last[2]);

        p2->x = pcVu0Last[0] * q + fNaViwOffsetX;
        p2->y = pcVu0Last[1] * q + fNaViwOffsetY;
    }
}

// 100% matching! 
Float	njOuterProduct(NJS_VECTOR *v1, NJS_VECTOR *v2, NJS_VECTOR *ov)
{
	float ret;

	{
	    float x = v1->y * v2->z - v2->y * v1->z;
	    float y = v1->z * v2->x - v2->z * v1->x;
	    float z = v1->x * v2->y - v2->x * v1->y;

	    ov->x = x;
	    ov->y = y;
	    ov->z = z;

	    ret = sqrtf((x * x + y * y) + z * z);
	}

    return ret;
}

// 100% matching! 
Float	njInnerProduct(NJS_VECTOR *v1, NJS_VECTOR *v2)
{
	float ret;

	ret = (v1->x * v2->x + v1->y * v2->y) + v1->z * v2->z;

    return ret;
}

// 100% matching! 
void njTranslateEx(NJS_VECTOR *v)
{
    pcCopyMatrix(pcVu0CN, (float*)pNaMatMatrixStuckPtr);
    pcVu0XformPoint(pcVu0CN, (float*)v, &pcVu0CN[12]);

    (*pNaMatMatrixStuckPtr)[12] = pcVu0CN[12];
    (*pNaMatMatrixStuckPtr)[13] = pcVu0CN[13];
    (*pNaMatMatrixStuckPtr)[14] = pcVu0CN[14];
}

// 100% matching! 
void njRotateEx( Angle *ang, Sint32 lv )
{
	if (lv != 0) 
    {
        njRotateY(NULL, ang[1]);
        njRotateX(NULL, ang[0]);
        njRotateZ(NULL, ang[2]);
    }
    else
    {
        njRotateZ(NULL, ang[2]);
        njRotateY(NULL, ang[1]);
        njRotateX(NULL, ang[0]);
    }
}

// 100% matching! 
void njScaleEx(NJS_VECTOR *v)
{
    {
        float* fm = (float*)pNaMatMatrixStuckPtr;
        int i;

        for (i = 0; i < 4; i++)
        {
            pcVu0CN[0 + i] = fm[0 + i] = fm[0 + i] * v->x;
            pcVu0CN[4 + i] = fm[4 + i] = fm[4 + i] * v->y;
            pcVu0CN[8 + i] = fm[8 + i] = fm[8 + i] * v->z;
        }
    }
}

// 100% matching!
Bool njPushMatrixEx( void )
{
    njPushMatrix(NULL);
    return 0;
}

// 100% matching!
Bool njPopMatrixEx( void )
{
    njPopMatrix(1);
    return 0;
}

// 100% matching! 
void njRotTransPers(NJS_POINT3* pPoint, NJS_SCRVECTOR* pScreen)
{
	njMulMatrixCN(&NaViewScreenMatrix, NULL);

    njCalcPointCN(pPoint, (NJS_POINT3*)&pScreen->x);

    {
        float q = pcVu0Div(1.0f, pcVu0Last[2]);
        float s = _nj_screen_.dist * q;

        pScreen->x = pcVu0Last[0] * s + fNaViwOffsetX;
        pScreen->y = pcVu0Last[1] * s + fNaViwOffsetY;
        pScreen->iz = q;
    }

    pScreen->fog = njCalcFogPower(pScreen->z);
}

// 100% matching!
void njRotTrans(NJS_POINT3* pPoint, NJS_POINT3* pOut)
{
	njMulMatrixCN(&NaViewScreenMatrix, NULL);
    
    njCalcPointCN(pPoint, pOut);
}

// 100% matching!
void njPers(NJS_SCRVECTOR* pScreen)
{
    {
        float q = pcVu0Div(1.0f, pScreen->z);
        float s = _nj_screen_.dist * q;

        pcVu0Last[0] = pScreen->x;
        pcVu0Last[1] = pScreen->y;
        pcVu0Last[2] = pScreen->z;

        pScreen->x = pcVu0Last[0] * s + fNaViwOffsetX;
        pScreen->y = pcVu0Last[1] * s + fNaViwOffsetY;
        pScreen->iz = q;
    }
}

// 100% matching!
void njCopyMatrix(NJS_MATRIX* pDstMat, NJS_MATRIX* pSrcMat) 
{
    pcCopyMatrix((float*)pDstMat, (const float*)pSrcMat);
}

// 100% matching!
void njMulMatrixCN(NJS_MATRIX* pSrcMat1, NJS_MATRIX* pSrcMat2)
{
    if (pSrcMat2 == NULL)
    {
        pSrcMat2 = pNaMatMatrixStuckPtr;
    }

    {
        float* a = (float*)pSrcMat1;
        float* b = (float*)pSrcMat2;
        int i, j;

        for (i = 0; i < 4; i++)
        {
            for (j = 0; j < 4; j++)
            {
                float v = (a[j] * b[i * 4 + 0] + a[4 + j] * b[i * 4 + 1]) + a[8 + j] * b[i * 4 + 2];

                pcVu0CN[i * 4 + j] = (i == 3) ? v + a[12 + j] : v;
            }
        }
    }
}
