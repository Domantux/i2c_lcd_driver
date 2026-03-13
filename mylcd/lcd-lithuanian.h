/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LCD_LITHUANIAN_H
#define _LCD_LITHUANIAN_H

#include <linux/types.h>

/*
 * Lithuanian UTF-8 → internal ID mapping.
 * IDs 256+ are used for Lithuanian characters.
 */
#define LT_CHAR_BASE	256
#define LT_CGRAM_SLOTS	8

enum lt_char_id {
	LT_a_ogonek = LT_CHAR_BASE,	/* ą - U+0105 = C4 85 */
	LT_A_ogonek,			/* Ą - U+0104 = C4 84 */
	LT_c_caron,			/* č - U+010D = C4 8D */
	LT_C_caron,			/* Č - U+010C = C4 8C */
	LT_e_ogonek,			/* ę - U+0119 = C4 99 */
	LT_E_ogonek,			/* Ę - U+0118 = C4 98 */
	LT_e_dot,			/* ė - U+0117 = C4 97 */
	LT_E_dot,			/* Ė - U+0116 = C4 96 */
	LT_i_ogonek,			/* į - U+012F = C4 AF */
	LT_I_ogonek,			/* Į - U+012E = C4 AE */
	LT_s_caron,			/* š - U+0161 = C5 A1 */
	LT_S_caron,			/* Š - U+0160 = C5 A0 */
	LT_u_ogonek,			/* ų - U+0173 = C5 B3 */
	LT_U_ogonek,			/* Ų - U+0172 = C5 B2 */
	LT_u_macron,			/* ū - U+016B = C5 AB */
	LT_U_macron,			/* Ū - U+016A = C5 AA */
	LT_z_caron,			/* ž - U+017E = C5 BE */
	LT_Z_caron,			/* Ž - U+017D = C5 BD */
	LT_CHAR_COUNT = LT_Z_caron - LT_CHAR_BASE + 1
};

/* 5x8 CGRAM bitmaps for Lithuanian characters (8 rows each) */
static const u8 lt_bitmaps[LT_CHAR_COUNT][8] = {
	[LT_a_ogonek - LT_CHAR_BASE] = {	/* ą */
		0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F, 0x02
	},
	[LT_A_ogonek - LT_CHAR_BASE] = {	/* Ą */
		0x04, 0x0A, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x02
	},
	[LT_c_caron - LT_CHAR_BASE] = {	/* č */
		0x0A, 0x04, 0x0E, 0x11, 0x10, 0x11, 0x0E, 0x00
	},
	[LT_C_caron - LT_CHAR_BASE] = {	/* Č */
		0x0A, 0x04, 0x0E, 0x11, 0x10, 0x11, 0x0E, 0x00
	},
	[LT_e_ogonek - LT_CHAR_BASE] = {	/* ę */
		0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E, 0x02
	},
	[LT_E_ogonek - LT_CHAR_BASE] = {	/* Ę */
		0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F, 0x02
	},
	[LT_e_dot - LT_CHAR_BASE] = {		/* ė */
		0x04, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E, 0x00
	},
	[LT_E_dot - LT_CHAR_BASE] = {		/* Ė */
		0x04, 0x00, 0x1F, 0x10, 0x1E, 0x10, 0x1F, 0x00
	},
	[LT_i_ogonek - LT_CHAR_BASE] = {	/* į */
		0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E, 0x02
	},
	[LT_I_ogonek - LT_CHAR_BASE] = {	/* Į */
		0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x02
	},
	[LT_s_caron - LT_CHAR_BASE] = {	/* š */
		0x0A, 0x04, 0x0E, 0x10, 0x0E, 0x01, 0x1E, 0x00
	},
	[LT_S_caron - LT_CHAR_BASE] = {	/* Š */
		0x0A, 0x04, 0x0E, 0x10, 0x0E, 0x01, 0x1E, 0x00
	},
	[LT_u_ogonek - LT_CHAR_BASE] = {	/* ų */
		0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D, 0x02
	},
	[LT_U_ogonek - LT_CHAR_BASE] = {	/* Ų */
		0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x02
	},
	[LT_u_macron - LT_CHAR_BASE] = {	/* ū */
		0x0E, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D, 0x00
	},
	[LT_U_macron - LT_CHAR_BASE] = {	/* Ū */
		0x0E, 0x00, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00
	},
	[LT_z_caron - LT_CHAR_BASE] = {	/* ž */
		0x0A, 0x04, 0x1F, 0x02, 0x04, 0x08, 0x1F, 0x00
	},
	[LT_Z_caron - LT_CHAR_BASE] = {	/* Ž */
		0x0A, 0x04, 0x1F, 0x02, 0x04, 0x08, 0x1F, 0x00
	},
};

/* Map a 2-byte UTF-8 sequence to a Lithuanian char ID, or 0 if not Lithuanian */
static inline u16 utf8_to_lt_id(u8 b0, u8 b1)
{
	if (b0 == 0xC4) {
		switch (b1) {
		case 0x84: return LT_A_ogonek;
		case 0x85: return LT_a_ogonek;
		case 0x8C: return LT_C_caron;
		case 0x8D: return LT_c_caron;
		case 0x96: return LT_E_dot;
		case 0x97: return LT_e_dot;
		case 0x98: return LT_E_ogonek;
		case 0x99: return LT_e_ogonek;
		case 0xAE: return LT_I_ogonek;
		case 0xAF: return LT_i_ogonek;
		}
	} else if (b0 == 0xC5) {
		switch (b1) {
		case 0xA0: return LT_S_caron;
		case 0xA1: return LT_s_caron;
		case 0xAA: return LT_U_macron;
		case 0xAB: return LT_u_macron;
		case 0xB2: return LT_U_ogonek;
		case 0xB3: return LT_u_ogonek;
		case 0xBD: return LT_Z_caron;
		case 0xBE: return LT_z_caron;
		}
	}
	return 0;
}

/* Return ASCII fallback for a Lithuanian char ID */
static inline u8 lt_fallback(u16 id)
{
	switch (id) {
	case LT_a_ogonek: return 'a';
	case LT_A_ogonek: return 'A';
	case LT_c_caron:  return 'c';
	case LT_C_caron:  return 'C';
	case LT_e_ogonek: return 'e';
	case LT_E_ogonek: return 'E';
	case LT_e_dot:    return 'e';
	case LT_E_dot:    return 'E';
	case LT_i_ogonek: return 'i';
	case LT_I_ogonek: return 'I';
	case LT_s_caron:  return 's';
	case LT_S_caron:  return 'S';
	case LT_u_ogonek: return 'u';
	case LT_U_ogonek: return 'U';
	case LT_u_macron: return 'u';
	case LT_U_macron: return 'U';
	case LT_z_caron:  return 'z';
	case LT_Z_caron:  return 'Z';
	default:          return '?';
	}
}

#endif /* _LCD_LITHUANIAN_H */
