/*
 * BrewTube - A Homebrew YouTube App for the Wii
 * Copyright (C) 2026  ReviveMii Project
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <malloc.h>
#include <string.h>

#include "gxyuv.h"

#define MAX_TEX_WIDTH 1024

enum {
	MP_CSP_DEFAULT,
	MP_CSP_BT_601,
	MP_CSP_BT_709,
	MP_CSP_SMPTE_240M,
	MP_CSP_EBU,
	MP_CSP_COUNT
};

typedef struct {
	guVector pos;
	guVector up;
	guVector view;
} Camera;

static const Camera cam = {
	{ 0.0f, 0.0f, 352.0f },
	{ 0.0f, 0.5f, 0.0f },
	{ 0.0f, 0.0f, -0.5f }
};

static int colorspace = MP_CSP_DEFAULT;
static const int levelconv = 1;
static float g_brightness = 0.0f;
static float g_contrast = 0.0f;

static GXRModeObj *mode;
static int screenWidth, screenHeight;
static u8 *drawY, *drawU, *drawV, *blackY;
static u32 Ysize, UVsize;
static GXTexObj YltexObj, YrtexObj, UtexObj, VtexObj;
static u16 Ylwidth, Yrwidth, Ywidth, Yheight, UVwidth, UVheight;
static int wl, wr;

static s16 square[12] ATTRIBUTE_ALIGN(32);

static GXColor colors[] ATTRIBUTE_ALIGN(32) = {
	{0, 255, 0, 255}
};

static f32 texcoordsY[16] ATTRIBUTE_ALIGN(32) = {
	0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
	0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f
};

static f32 texcoordsUV[8] ATTRIBUTE_ALIGN(32) = {
	0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f
};

static void drawSetup(void)
{

	GX_SetNumChans(1);
	GX_SetNumTexGens(4);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetTexCoordGen(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, GX_IDENTITY);
	GX_SetTexCoordGen(GX_TEXCOORD2, GX_TG_MTX2x4, GX_TG_TEX2, GX_IDENTITY);

	GX_SetNumTevStages(15);
	GX_SetTevKColor(GX_KCOLOR0, (GXColor){255,	 0,   0, levelconv ? 18 : 0});
	GX_SetTevKColor(GX_KCOLOR1, (GXColor){	0,	 0, 255, levelconv ? 41 : 0});

	static const GXColor uv_coeffs[MP_CSP_COUNT][2] = {
		[MP_CSP_DEFAULT] = {
			{203, 103,	 0, 255},
			{  0,  24, 128, 255}
		},
		[MP_CSP_BT_601] = {
			{179,  90,	 0, 255},
			{  0,  21, 112, 255}
		},
		[MP_CSP_BT_709] = {
			{200,  59,	 0, 255},
			{  0,  11, 118, 255}
		},
		[MP_CSP_SMPTE_240M] = {
			{201,  63,	 0, 255},
			{  0,  13, 116, 255}
		},
		[MP_CSP_EBU] = {
			{145,  73,	 0, 255},
			{  0,  24, 129, 255}
		},
	};

	int brightness_offset;
	u8 contrast_konst, contrast_InD, brightness_konst, brightness_TB, brightness_OP;
	if (g_contrast <= 0.0f) {
		contrast_konst = (u8)((1 + (g_contrast * 0.5))*255);
		brightness_offset = -((int)contrast_konst)/2 + 128 + ((int)(g_brightness*255));
		contrast_InD = GX_CC_ZERO;
	}
	else {
		contrast_konst = (u8)(g_contrast * 255);
		brightness_offset = -((int)contrast_konst)/2 + ((int)(g_brightness*255));
		contrast_InD = GX_CC_CPREV;
	}

	if (brightness_offset > 255) {
		brightness_konst = (u8)(brightness_offset - 128);
		brightness_TB = GX_TB_ADDHALF;
		brightness_OP = GX_TEV_ADD;
	}
	else if (brightness_offset >= 0) {
		brightness_konst = (u8)(brightness_offset);
		brightness_TB = GX_TB_ZERO;
		brightness_OP = GX_TEV_ADD;
	}
	else if (brightness_offset >= -255) {
		brightness_konst = (u8)(-brightness_offset);
		brightness_TB = GX_TB_ZERO;
		brightness_OP = GX_TEV_SUB;
	}
	else {
		brightness_konst = (u8)(-brightness_offset - 128);
		brightness_TB = GX_TB_SUBHALF;
		brightness_OP = GX_TEV_SUB;
	}

	GXColor KColor2 = (GXColor) {uv_coeffs[colorspace][0].r, uv_coeffs[colorspace][0].g,
		uv_coeffs[colorspace][0].b, contrast_konst};
	GXColor KColor3 = (GXColor) {uv_coeffs[colorspace][1].r, uv_coeffs[colorspace][1].g,
		uv_coeffs[colorspace][1].b, brightness_konst};

	GX_SetTevKColor(GX_KCOLOR2, KColor2);
	GX_SetTevKColor(GX_KCOLOR3, KColor3);

	GX_SetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD2, GX_TEXMAP2, GX_COLOR0A0);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_RASC, GX_CC_KONST, GX_CC_TEXC, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_SUBHALF, GX_CS_SCALE_2, GX_ENABLE, GX_TEVREG0);
	GX_SetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_K0_A);
	GX_SetTevAlphaIn (GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_RASA, GX_CA_KONST, GX_CA_ZERO);
	GX_SetTevAlphaOp (GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVREG0);

	GX_SetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K1);
	GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD2, GX_TEXMAP2, GX_COLOR0A0);
	GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_KONST, GX_CC_RASC, GX_CC_TEXC, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_SUBHALF, GX_CS_SCALE_2, GX_ENABLE, GX_TEVREG1);
	GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);

	GX_SetTevKColorSel(GX_TEVSTAGE2, GX_TEV_KCSEL_K0);
	GX_SetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD2, GX_TEXMAP3, GX_COLOR0A0);
	GX_SetTevColorIn(GX_TEVSTAGE2, GX_CC_RASC, GX_CC_KONST, GX_CC_TEXC, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_SUBHALF, GX_CS_SCALE_1, GX_ENABLE, GX_TEVREG2);
	GX_SetTevAlphaIn(GX_TEVSTAGE2, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);

	GX_SetTevKColorSel(GX_TEVSTAGE3, GX_TEV_KCSEL_K0);
	GX_SetTevOrder(GX_TEVSTAGE3, GX_TEXCOORD2, GX_TEXMAP3, GX_COLOR0A0);
	GX_SetTevColorIn(GX_TEVSTAGE3, GX_CC_KONST, GX_CC_RASC, GX_CC_TEXC, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_SUBHALF, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE3, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);

	GX_SetTevKColorSel(GX_TEVSTAGE4, GX_TEV_KCSEL_K2);
	GX_SetTevOrder(GX_TEVSTAGE4, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE4, GX_CC_ZERO, GX_CC_KONST, GX_CC_CPREV, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE4, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_2, GX_DISABLE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE4, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
	GX_SetTevAlphaOp(GX_TEVSTAGE4, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_DISABLE, GX_TEVPREV);

	GX_SetTevKColorSel(GX_TEVSTAGE5, GX_TEV_KCSEL_K2);
	GX_SetTevOrder(GX_TEVSTAGE5, GX_TEXCOORD1, GX_TEXMAP1, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE5, GX_CC_ZERO, GX_CC_KONST, GX_CC_C2, GX_CC_CPREV);
	GX_SetTevColorOp(GX_TEVSTAGE5, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_DISABLE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE5, GX_CA_TEXA, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
	GX_SetTevAlphaOp(GX_TEVSTAGE5, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_DISABLE, GX_TEVREG1);

	GX_SetTevKColorSel(GX_TEVSTAGE6, GX_TEV_KCSEL_K2);
	GX_SetTevOrder(GX_TEVSTAGE6, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE6, GX_CC_ZERO, GX_CC_KONST, GX_CC_C2, GX_CC_CPREV);
	GX_SetTevColorOp(GX_TEVSTAGE6, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_DISABLE, GX_TEVPREV);
	GX_SetTevKAlphaSel(GX_TEVSTAGE6, GX_TEV_KASEL_1);
	GX_SetTevAlphaIn(GX_TEVSTAGE6, GX_CA_ZERO, GX_CA_KONST, GX_CA_A0, GX_CA_A1);
	GX_SetTevAlphaOp(GX_TEVSTAGE6, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, GX_DISABLE, GX_TEVPREV);

	GX_SetTevOrder(GX_TEVSTAGE7, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE7, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
	GX_SetTevColorOp(GX_TEVSTAGE7, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_DISABLE, GX_TEVPREV);
	GX_SetTevKAlphaSel(GX_TEVSTAGE7, GX_TEV_KASEL_K1_A);
	GX_SetTevAlphaIn(GX_TEVSTAGE7, GX_CA_ZERO, GX_CA_KONST, GX_CA_A1, GX_CA_APREV);
	GX_SetTevAlphaOp(GX_TEVSTAGE7, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVREG2);

	GX_SetTevKColorSel(GX_TEVSTAGE8, GX_TEV_KCSEL_1);
	GX_SetTevOrder(GX_TEVSTAGE8, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE8, GX_CC_ZERO, GX_CC_ONE, GX_CC_A2, GX_CC_CPREV);
	GX_SetTevColorOp(GX_TEVSTAGE8, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_DISABLE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE8, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE8, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);

	GX_SetTevKColorSel(GX_TEVSTAGE9, GX_TEV_KCSEL_K3);
	GX_SetTevOrder(GX_TEVSTAGE9, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE9, GX_CC_ZERO, GX_CC_KONST, GX_CC_C1, GX_CC_CPREV);
	GX_SetTevColorOp(GX_TEVSTAGE9, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, GX_DISABLE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE9, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE9, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);

	GX_SetTevKColorSel(GX_TEVSTAGE10, GX_TEV_KCSEL_K3);
	GX_SetTevOrder(GX_TEVSTAGE10, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE10, GX_CC_ZERO, GX_CC_KONST, GX_CC_C1, GX_CC_CPREV);
	GX_SetTevColorOp(GX_TEVSTAGE10, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, GX_DISABLE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE10, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE10, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);

	GX_SetTevKColorSel(GX_TEVSTAGE11, GX_TEV_KCSEL_K3);
	GX_SetTevOrder(GX_TEVSTAGE11, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE11, GX_CC_ZERO, GX_CC_KONST, GX_CC_C0, GX_CC_CPREV);
	GX_SetTevColorOp(GX_TEVSTAGE11, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_DISABLE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE11, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE11, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);

	GX_SetTevKColorSel(GX_TEVSTAGE12, GX_TEV_KCSEL_K3);
	GX_SetTevOrder(GX_TEVSTAGE12, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE12, GX_CC_ZERO, GX_CC_KONST, GX_CC_C0, GX_CC_CPREV);
	GX_SetTevColorOp(GX_TEVSTAGE12, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevKAlphaSel(GX_TEVSTAGE12, GX_TEV_KASEL_K2_A);
	GX_SetTevAlphaIn(GX_TEVSTAGE12, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);
	GX_SetTevAlphaOp(GX_TEVSTAGE12, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);

	GX_SetTevOrder(GX_TEVSTAGE13, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE13, GX_CC_ZERO, GX_CC_CPREV, GX_CC_APREV, contrast_InD);
	GX_SetTevColorOp(GX_TEVSTAGE13, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_DISABLE, GX_TEVPREV);
	GX_SetTevKAlphaSel(GX_TEVSTAGE13, GX_TEV_KASEL_K3_A);
	GX_SetTevAlphaIn(GX_TEVSTAGE13, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);
	GX_SetTevAlphaOp(GX_TEVSTAGE13, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);

	GX_SetTevOrder(GX_TEVSTAGE14, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE14, GX_CC_APREV, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
	GX_SetTevColorOp(GX_TEVSTAGE14, brightness_OP, brightness_TB, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevKAlphaSel(GX_TEVSTAGE14, GX_TEV_KCSEL_1);
	GX_SetTevAlphaIn(GX_TEVSTAGE14, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);
	GX_SetTevAlphaOp(GX_TEVSTAGE14, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_INDEX8);
	GX_SetVtxDesc(GX_VA_CLR0, GX_INDEX8);
	GX_SetVtxDesc(GX_VA_TEX0, GX_INDEX8);
	GX_SetVtxDesc(GX_VA_TEX1, GX_INDEX8);
	GX_SetVtxDesc(GX_VA_TEX2, GX_INDEX8);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_S16, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX1, GX_TEX_ST, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX2, GX_TEX_ST, GX_F32, 0);

	GX_SetArray(GX_VA_POS, square, 3 * sizeof(s16));
	GX_SetArray(GX_VA_CLR0, colors, sizeof(GXColor));
	GX_SetArray(GX_VA_TEX0, texcoordsY, 2 * sizeof(f32));
	GX_SetArray(GX_VA_TEX1, texcoordsY, 2 * sizeof(f32));
	GX_SetArray(GX_VA_TEX2, texcoordsUV, 2 * sizeof(f32));

	GX_InitTexObj(&YltexObj, drawY, (u16) Ylwidth, (u16) Yheight, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_InitTexObjLOD(&YltexObj, GX_LINEAR, GX_LINEAR, 0.0, 0.0, 0.0, GX_TRUE, GX_TRUE, GX_ANISO_4);
	GX_InitTexObj(&YrtexObj, blackY, (u16) Yrwidth, (u16) Yheight, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_InitTexObjLOD(&YrtexObj, GX_LINEAR, GX_LINEAR, 0.0, 0.0, 0.0, GX_TRUE, GX_TRUE, GX_ANISO_4);
	GX_InitTexObj(&UtexObj, drawU, (u16) UVwidth, (u16) UVheight, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_InitTexObjLOD(&UtexObj, GX_LINEAR, GX_LINEAR, 0.0, 0.0, 0.0, GX_TRUE, GX_TRUE, GX_ANISO_4);
	GX_InitTexObj(&VtexObj, drawV, (u16) UVwidth, (u16) UVheight, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_InitTexObjLOD(&VtexObj, GX_LINEAR, GX_LINEAR, 0.0, 0.0, 0.0, GX_TRUE, GX_TRUE, GX_ANISO_4);

	GX_LoadTexObj(&YltexObj, GX_TEXMAP0);
	GX_LoadTexObj(&YrtexObj, GX_TEXMAP1);
	GX_LoadTexObj(&UtexObj, GX_TEXMAP2);
	GX_LoadTexObj(&VtexObj, GX_TEXMAP3);

}

#define LUMA_COPY(type) \
{ \
	type *Yldst = (type *)Yltexture - 1; \
	type *Yrdst = (type *)Yrtexture - 1; \
	 \
	type *Ysrc1 = (type *) (buffer[0]) - 1; \
	type *Ysrc2 = (type *)((buffer[0]) + stride[0]) - 1; \
	type *Ysrc3 = (type *)((buffer[0]) + (stride[0] * 2)) - 1; \
	type *Ysrc4 = (type *)((buffer[0]) + (stride[0] * 3)) - 1; \
	 \
	Yrdst += 4; \
	int rows = Yheight / 4; \
	int tiles; \
	while (rows--) { \
		tiles = wl; \
		 \
		while (tiles--) { \
			__asm__ volatile("dcbz 0,%0" : : "b" (++Yldst)); \
			*Yldst = *++Ysrc1; \
			*++Yldst = *++Ysrc2; \
			*++Yldst = *++Ysrc3; \
			*++Yldst = *++Ysrc4; \
		} \
		if (wr>0){ \
			tiles = wr; \
			 \
			while (tiles--) { \
				__asm__ volatile("dcbz 0,%0" : : "b" (++Yrdst)); \
				*Yrdst = *++Ysrc1; \
				*++Yrdst = *++Ysrc2; \
				*++Yrdst = *++Ysrc3; \
				*++Yrdst = *++Ysrc4; \
			} \
			Yldst += 4; \
			Yrdst += 4; \
		}\
		 \
		Ysrc1 = (type *)((u32)Ysrc1 + Yrowpitch); \
		Ysrc2 = (type *)((u32)Ysrc2 + Yrowpitch); \
		Ysrc3 = (type *)((u32)Ysrc3 + Yrowpitch); \
		Ysrc4 = (type *)((u32)Ysrc4 + Yrowpitch); \
	} \
}

#define CHROMA_COPY(type) \
{ \
	type *Udst = (type *)Utexture - 1; \
	type *Vdst = (type *)Vtexture - 1; \
	 \
	type *Usrc1 = (type *) (buffer[1]) - 1; \
	type *Usrc2 = (type *)((buffer[1]) + stride[1]) - 1; \
	type *Usrc3 = (type *)((buffer[1]) + (stride[1] * 2)) - 1; \
	type *Usrc4 = (type *)((buffer[1]) + (stride[1] * 3)) - 1; \
	 \
	type *Vsrc1 = (type *) (buffer[2]) - 1; \
	type *Vsrc2 = (type *)((buffer[2]) + stride[2]) - 1; \
	type *Vsrc3 = (type *)((buffer[2]) + (stride[2] * 2)) - 1; \
	type *Vsrc4 = (type *)((buffer[2]) + (stride[2] * 3)) - 1; \
	 \
	int rows = UVheight / 4; \
	int tiles, ntiles = UVwidth / 8; \
		\
	while (rows--) { \
		tiles = ntiles; \
		while (tiles--) { \
			__asm__ volatile("dcbz 0,%0" : : "b" (++Udst) ); \
			*Udst = *++Usrc1; \
			*++Udst = *++Usrc2; \
			*++Udst = *++Usrc3; \
			*++Udst = *++Usrc4; \
			 \
			__asm__ volatile("dcbz 0,%0" : : "b" (++Vdst) ); \
			*Vdst = *++Vsrc1; \
			*++Vdst = *++Vsrc2; \
			*++Vdst = *++Vsrc3; \
			*++Vdst = *++Vsrc4; \
		} \
		 \
		Usrc1 = (type *)((u32)Usrc1 + UVrowpitch); \
		Usrc2 = (type *)((u32)Usrc2 + UVrowpitch); \
		Usrc3 = (type *)((u32)Usrc3 + UVrowpitch); \
		Usrc4 = (type *)((u32)Usrc4 + UVrowpitch); \
		 \
		Vsrc1 = (type *)((u32)Vsrc1 + UVrowpitch); \
		Vsrc2 = (type *)((u32)Vsrc2 + UVrowpitch); \
		Vsrc3 = (type *)((u32)Vsrc3 + UVrowpitch); \
		Vsrc4 = (type *)((u32)Vsrc4 + UVrowpitch); \
	} \
}

int yuvOpen(GXRModeObj *vmode, int width, int height, int sarNum, int sarDen, int wide, YuvFrame *frames, int count)
{
	int chromaWidth = (width + 1) / 2;
	int chromaHeight = (height + 1) / 2;
	float screenAspect, imageAspect;
	int drawWidth, drawHeight;

	if (width > MAX_TEX_WIDTH || height > MAX_TEX_WIDTH)
		return -1;

	mode = vmode;
	screenWidth = wide ? 854 : 640;
	screenHeight = 480;
	colorspace = height > 576 ? MP_CSP_BT_709 : MP_CSP_DEFAULT;

	Ywidth = (width + 7) & ~7;
	UVwidth = (chromaWidth + 7) & ~7;
	Yheight = (height + 3) & ~3;
	UVheight = (chromaHeight + 3) & ~3;
	Ylwidth = Ywidth;
	Yrwidth = 8;
	wl = Ywidth / 8;
	wr = 0;
	Ysize = Ywidth * Yheight;
	UVsize = UVwidth * UVheight;

	texcoordsY[2] = texcoordsY[4] = (f32)width / Ywidth;
	texcoordsY[5] = texcoordsY[7] = (f32)height / Yheight;
	texcoordsUV[2] = texcoordsUV[4] = (f32)chromaWidth / UVwidth;
	texcoordsUV[5] = texcoordsUV[7] = (f32)chromaHeight / UVheight;

	screenAspect = (float)screenWidth / screenHeight;
	imageAspect = (float)width * sarNum / ((float)height * sarDen);
	if (imageAspect > screenAspect) {
		drawWidth = screenWidth;
		drawHeight = screenWidth / imageAspect;
	} else {
		drawWidth = screenHeight * imageAspect;
		drawHeight = screenHeight;
	}

	square[0] = square[9] = -(drawWidth / 2);
	square[3] = square[6] = drawWidth / 2;
	square[1] = square[4] = drawHeight / 2;
	square[7] = square[10] = -(drawHeight / 2);

	DCFlushRange(square, sizeof(square));
	DCFlushRange(colors, sizeof(colors));
	DCFlushRange(texcoordsY, sizeof(texcoordsY));
	DCFlushRange(texcoordsUV, sizeof(texcoordsUV));

	blackY = memalign(32, 8 * Yheight);
	if (!blackY)
		return -1;
	memset(blackY, 0, 8 * Yheight);
	DCFlushRange(blackY, 8 * Yheight);

	for (int i = 0; i < count; i++) {
		frames[i].y = memalign(32, Ysize);
		frames[i].u = memalign(32, UVsize);
		frames[i].v = memalign(32, UVsize);
		if (!frames[i].y || !frames[i].u || !frames[i].v) {
			yuvClose(frames, i + 1);
			return -1;
		}
		memset(frames[i].y, 0, Ysize);
		memset(frames[i].u, 0x80, UVsize);
		memset(frames[i].v, 0x80, UVsize);
		DCFlushRange(frames[i].y, Ysize);
		DCFlushRange(frames[i].u, UVsize);
		DCFlushRange(frames[i].v, UVsize);
	}

	return 0;
}

void yuvClose(YuvFrame *frames, int count)
{
	for (int i = 0; i < count; i++) {
		free(frames[i].y);
		free(frames[i].u);
		free(frames[i].v);
		frames[i].y = frames[i].u = frames[i].v = NULL;
	}
	free(blackY);
	blackY = NULL;
}

void yuvFill(const YuvFrame *frame, u8 *const buffer[3], const int stride[3])
{
	u8 *Yltexture = frame->y;
	u8 *Yrtexture = blackY;
	u8 *Utexture = frame->u;
	u8 *Vtexture = frame->v;
	int Yrowpitch = (stride[0] * 4) - Ywidth;
	int UVrowpitch = (stride[1] * 4) - UVwidth;

	if (stride[0] & 7)
		LUMA_COPY(u64)
	else
		LUMA_COPY(double)

	if (stride[1] & 7)
		CHROMA_COPY(u64)
	else
		CHROMA_COPY(double)

	DCFlushRange(frame->y, Ysize);
	DCFlushRange(frame->u, UVsize);
	DCFlushRange(frame->v, UVsize);
}

void yuvDraw(const YuvFrame *frame)
{
	Mtx view, m, mv;
	Mtx44 p;

	drawY = frame->y;
	drawU = frame->u;
	drawV = frame->v;

	GX_SetCullMode(GX_CULL_NONE);
	GX_SetClipMode(GX_DISABLE);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_TRUE);

	guOrtho(p, screenHeight / 2.0f, -(screenHeight / 2.0f), -(screenWidth / 2.0f), screenWidth / 2.0f, 10.0f, 1000.0f);
	GX_LoadProjectionMtx(p, GX_ORTHOGRAPHIC);

	drawSetup();

	memset(&view, 0, sizeof(Mtx));
	guLookAt(view, (guVector *)&cam.pos, (guVector *)&cam.up, (guVector *)&cam.view);
	guMtxIdentity(m);
	guMtxTransApply(m, m, 0, 0, -100);
	guMtxConcat(view, m, mv);
	GX_LoadPosMtxImm(mv, GX_PNMTX0);
	GX_SetViewport(1.0f / 24.0f, 1.0f / 24.0f, mode->fbWidth, mode->efbHeight, 0, 1);

	GX_InvVtxCache();
	GX_InvalidateTexAll();

	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		GX_Position1x8(0); GX_Color1x8(0); GX_TexCoord1x8(0); GX_TexCoord1x8(4); GX_TexCoord1x8(0);
		GX_Position1x8(1); GX_Color1x8(0); GX_TexCoord1x8(1); GX_TexCoord1x8(5); GX_TexCoord1x8(1);
		GX_Position1x8(2); GX_Color1x8(0); GX_TexCoord1x8(2); GX_TexCoord1x8(6); GX_TexCoord1x8(2);
		GX_Position1x8(3); GX_Color1x8(0); GX_TexCoord1x8(3); GX_TexCoord1x8(7); GX_TexCoord1x8(3);
	GX_End();
}
