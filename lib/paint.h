/*
 * Painting internals (paint.c, image.c). Never installed.
 *
 * Copyright (C) 2026 Christophe Lincoln <pankso@slitaz.org>
 * Written with the assistance of Claude (Anthropic) and Opencode.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef SOIE_PAINT_H
#define SOIE_PAINT_H

#include <math.h>
#include "layout.h"

struct canvas {
	unsigned char *px;	/* RGBA, or BGRA when bgr */
	int w, h, stride;
	float ox, oy;		/* page coordinates of pixel (0, 0) */
	int cx0, cy0, cx1, cy1;	/* clip, in pixels */
	float alpha;		/* opacity of what is painted now */
	int bgr;		/* red and blue swapped in px */
	/* mask-image: the alpha of pixels mx0..mx1, my0..my1 (0 out of
	 * them), NULL without a mask */
	unsigned char *mask;
	int mx0, my0, mx1, my1;
	/* A video box whose opaque frame fills all of this canvas: what
	 * is painted before it would not show and is skipped, until
	 * paint_media() of that box clears this; NULL otherwise */
	const struct box *cover;
};

/* The mask's alpha at pixel x, y; 255 without one */
static inline int mask_at(const struct canvas *c, int x, int y)
{
	if (!c->mask)
		return 255;
	if (x < c->mx0 || y < c->my0 || x >= c->mx1 || y >= c->my1)
		return 0;
	return c->mask[(size_t)(y - c->my0) * (size_t)(c->mx1 - c->mx0) +
		(size_t)(x - c->mx0)];
}

/* x / 255 rounded down, for x in 0..65534 (a byte times a byte is
 * 65025 at most): two shifts whatever the compiler's -O, where -Os
 * keeps a division (tests/unit.c tries every x) */
static inline int div255(int x)
{
	return (x + 1 + (x >> 8)) >> 8;
}

/* A page coordinate as a pixel index: bounded first, a box ten
 * billion pixels away does not fit an int (the conversion is then
 * undefined, and gave clips larger than the canvas) */
static inline int ifloor(float v)
{
	return v < -1e9f ? -1000000000 : v > 1e9f ? 1000000000 :
		(int)floorf(v);
}

static inline int iceil(float v)
{
	return v < -1e9f ? -1000000000 : v > 1e9f ? 1000000000 :
		(int)ceilf(v);
}

extern soie_node *paint_doc;
extern unsigned paint_pass;	/* counts the paints: soie_paint() calls */

/* The polygon of n points x, y (page coordinates), anti-aliased:
 * marks of form controls (svg.c) */
void fill_polygon(struct canvas *c, const float *pts, int n,
	const unsigned char *rgba);
/* A rectangle with round corners of radius r (r = w / 2: a disc) */
void fill_round(struct canvas *c, float x, float y, float w, float h,
	float r, const unsigned char *rgba);
void fill_rect(struct canvas *c, float x, float y, float w, float h,
	const unsigned char *rgba);
void paint_replaced(struct canvas *c, const struct box *b);
int media_frame_rect(soie_node *doc, int i, int bgr, const struct box **b,
	int *r);
void paint_bg_image(struct canvas *c, const struct box *b, float x,
	float y, float w, float h);
void paint_layers_of(struct canvas *c, const struct box *b, int img_p,
	float x, float y, float w, float h);
void paint_bg_canvas(struct canvas *c, const struct box *b);
float css_len_px(const struct cv *v, float ref, float em);
int clip_path_paint(struct canvas *c, const struct box *b);
void glyph_cache_clear(void);
void paint_text(struct canvas *c, const struct style *st, const char *s,
	size_t len, float x, float base);
void paint_control(struct canvas *c, const struct box *b);

#endif
