/*
 * Paint tests: render tests/paint/NAME.html at 320x240 with the stub
 * font and compare with NAME.png, pixel for pixel, within 2 levels per
 * channel (x87 and SSE floats round differently), and painted in
 * pieces against itself whole. With -u, write the references instead
 * (check them by eye before committing).
 * Usage: painttest [-u] FILE.html...
 *
 * Copyright (C) 2026 Christophe Lincoln <pankso@slitaz.org>
 * Written with the assistance of Claude (Anthropic) and Opencode.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <unistd.h>
#include <soie/soie.h>

#define W 320
#define H 240

static char *slurp(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *b;
	long n;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc((size_t)n + 1);
	*len = fread(b, 1, (size_t)n, f);
	fclose(f);
	return b;
}

/* file:// URLs only: the test images. */
static int fetch(void *data, const char *url, char **buf, size_t *len)
{
	(void)data;
	if (strncmp(url, "file://", 7))
		return -1;
	*buf = slurp(url + 7, len);
	return *buf ? 0 : -1;
}

/* Same image, give or take rounding. */
static int same(const unsigned char *a, const unsigned char *b)
{
	int i;

	for (i = 0; i < W * H * 4; i++)
		if (a[i] - b[i] > 2 || b[i] - a[i] > 2)
			return 0;
	return 1;
}

/*
 * What the browser's partial repaints rely on, against px (the page
 * painted whole): soie_paint_bgra() swaps red and blue and nothing
 * else; bands and rects painted alone, and the page painted scrolled,
 * give the pixels of the whole. Returns what differs, or NULL.
 */
static const char *pieces(soie_node *doc, const unsigned char *px)
{
	static unsigned char q[W * H * 4], r[W * H * 4];
	static const int bands[] = { 0, 70, 71, 170, H };
	int i, x, y;

	soie_paint_bgra(doc, q, W, H, W * 4, 0, 0);
	for (i = 0; i < W * H * 4; i += 4)
		if (q[i] != px[i + 2] || q[i + 1] != px[i + 1] ||
		    q[i + 2] != px[i] || q[i + 3] != px[i + 3])
			return "BGRA";
	memset(r, 0, sizeof(r));
	for (i = 0; i < 4; i++)
		soie_paint_bgra(doc, r + bands[i] * W * 4, W, bands[i + 1] -
			bands[i], W * 4, 0, bands[i]);
	if (!same(q, r))
		return "bands";
	/* a rect, as a video frame is painted */
	soie_paint_bgra(doc, r + (50 * W + 33) * 4, 101, 77, W * 4, 33, 50);
	if (!same(q, r))
		return "rect";
	/* scrolled by 37: what was at y + 37 is at y */
	soie_paint_bgra(doc, r, W, H, W * 4, 0, 37);
	for (y = 0; y < H - 37; y++)
		for (x = 0; x < W * 4; x++)
			if (r[y * W * 4 + x] - q[(y + 37) * W * 4 + x] > 2 ||
			    q[(y + 37) * W * 4 + x] - r[y * W * 4 + x] > 2)
				return "scrolled";
	return NULL;
}

static int write_png(const char *path, unsigned char *px)
{
	FILE *f = fopen(path, "wb");
	png_structp p;
	png_infop i;
	int y;

	if (!f)
		return -1;
	p = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	i = png_create_info_struct(p);
	if (setjmp(png_jmpbuf(p))) {
		fclose(f);
		return -1;
	}
	png_init_io(p, f);
	png_set_IHDR(p, i, W, H, 8, PNG_COLOR_TYPE_RGB_ALPHA,
		PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
		PNG_FILTER_TYPE_DEFAULT);
	png_write_info(p, i);
	for (y = 0; y < H; y++)
		png_write_row(p, px + y * W * 4);
	png_write_end(p, i);
	png_destroy_write_struct(&p, &i);
	fclose(f);
	return 0;
}

static unsigned char *read_png(const char *path)
{
	FILE *f = fopen(path, "rb");
	png_structp p;
	png_infop i;
	unsigned char *px;
	int y;

	if (!f)
		return NULL;
	p = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	i = png_create_info_struct(p);
	if (setjmp(png_jmpbuf(p))) {
		fclose(f);
		return NULL;
	}
	png_init_io(p, f);
	png_read_info(p, i);
	if (png_get_image_width(p, i) != W || png_get_image_height(p, i) != H ||
	    png_get_color_type(p, i) != PNG_COLOR_TYPE_RGB_ALPHA) {
		fclose(f);
		return NULL;
	}
	px = malloc(W * H * 4);
	for (y = 0; y < H; y++)
		png_read_row(p, px + y * W * 4, NULL);
	png_destroy_read_struct(&p, &i, NULL);
	fclose(f);
	return px;
}

/*
 * A playing video: the page under its opaque frame is not painted
 * (cover, lib/paint.c). A media extension of our own gives the frames;
 * each page is painted whole, then in bands too low for a cover: the
 * pixels must be the same, and the cover taken where it can be, left
 * where something is in the way of the frame.
 */
extern unsigned paint_covered;	/* lib/paint.c, not in the API */

static int fake_alpha;		/* frames are translucent */

static int fake_can(void *data, const char *type)
{
	(void)data;
	(void)type;
	return 1;
}

static void *fake_open(void *data, const char *url, soie_request_fn request,
	void *rdata)
{
	(void)data;
	(void)request;
	(void)rdata;
	return strstr(url, "fake") ? calloc(1, sizeof(unsigned char *)) : NULL;
}

static void fake_close(void *p)
{
	free(*(unsigned char **)p);
	free(p);
}

static void fake_do(void *p)
{
	(void)p;
}

static void fake_seek(void *p, double t)
{
	(void)p;
	(void)t;
}

static void fake_volume(void *p, double v, int muted)
{
	(void)p;
	(void)v;
	(void)muted;
}

static void fake_state(void *p, soie_media_state *st)
{
	(void)p;
	st->width = 64;
	st->height = 36;
	st->duration = 10;
	st->time = 2;
	st->changes = 1;
}

/* Always the same picture, at the size and in the order asked */
static const unsigned char *fake_frame_for(void *p, int ww, int wh, int bgra,
	int *w, int *h, int *flags)
{
	unsigned char **buf = p, *q;
	int x, y;

	if (ww <= 0 || wh <= 0)
		return NULL;
	free(*buf);
	q = *buf = malloc((size_t)ww * (size_t)wh * 4);
	for (y = 0; y < wh; y++)
		for (x = 0; x < ww; x++, q += 4) {
			q[bgra ? 2 : 0] = (unsigned char)(x * 7 + y * 3);
			q[1] = (unsigned char)(x ^ y);
			q[bgra ? 0 : 2] = (unsigned char)(y * 5);
			q[3] = fake_alpha ? 128 : 255;
		}
	*w = ww;
	*h = wh;
	*flags = (bgra ? SOIE_FRAME_BGRA : 0) |
		(fake_alpha ? 0 : SOIE_FRAME_OPAQUE);
	return *buf;
}

/* An extension built before frame_for was: frame() alone, RGBA at the
 * video's own size, its hook set ending where it did then */
static void *old_open(void *data, const char *url, soie_request_fn request,
	void *rdata)
{
	(void)data;
	(void)request;
	(void)rdata;
	return strstr(url, "old:") ? calloc(1, sizeof(unsigned char *)) : NULL;
}

static const unsigned char *old_frame(void *p, int *w, int *h)
{
	int flags;

	return fake_frame_for(p, 64, 36, 0, w, h, &flags);
}

static struct soie_ext_media old_media = {
	{ SOIE_EXT_ABI_MAJOR << 16 | 1, offsetof(struct soie_ext_media,
	  frame_for), SOIE_EXT_MEDIA, "old", NULL },
	fake_can, old_open, fake_close, fake_do, fake_do, fake_seek,
	fake_volume, fake_state, old_frame, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL
};

static struct soie_ext_media fake_media = {
	{ SOIE_EXT_ABI, sizeof(fake_media), SOIE_EXT_MEDIA, "fake", NULL },
	fake_can, fake_open, fake_close, fake_do, fake_do, fake_seek,
	fake_volume, fake_state, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	fake_frame_for, NULL
};

static int fake_request(void *data, const soie_request *req,
	soie_response *res)
{
	(void)data;
	(void)req;
	(void)res;
	return -1;
}

#define VIDEO "<video controls src=fake:v style='display:block;width:200px;" \
	"height:120px;margin:20px;border:3px solid #fff8;border-radius:16px;" \
	"background:#000"
#define PAGE "<body style='margin:0;background:linear-gradient(#135,#e94)'>" \
	"<p style='margin:0'>Under and around the video"

static const struct {
	const char *html;
	int cover;	/* the frame hides what is under it */
	int alpha;
} videos[] = {
	/* the plain case, a box over the video, one under it */
	{ PAGE "<div style='position:absolute;left:60px;top:60px;width:50px;"
	  "height:50px;background:#0f08;z-index:2'>over</div>"
	  "<div style='position:absolute;left:150px;top:30px;width:90px;"
	  "height:90px;background:red;z-index:-1'>under</div>"
	  VIDEO "'></video><p style='margin-top:-60px;color:#fff'>Text over "
	  "the video", 1, 0 },
	{ PAGE VIDEO ";object-fit:cover'></video>", 1, 0 },
	{ PAGE VIDEO ";object-fit:fill;border-radius:0;border:0'></video>", 1,
	  0 },
	/* letterboxed: the frame alone is covered */
	{ PAGE VIDEO ";height:200px'></video>", 1, 0 },
	{ PAGE VIDEO ";object-fit:none'></video>", 1, 0 },
	/* a rounded clip well around it does not hide it */
	{ PAGE "<div style='overflow:hidden;border-radius:24px;width:300px;"
	  "background:#408'>" VIDEO ";margin:40px'></video></div>", 1, 0 },
	/* something in the way of the frame */
	{ PAGE "<div style='opacity:.5'>" VIDEO "'></video></div>", 0, 0 },
	{ PAGE VIDEO ";opacity:.7'></video>", 0, 0 },
	{ PAGE "<div style='transform:rotate(4deg)'>" VIDEO "'></video></div>",
	  0, 0 },
	{ PAGE VIDEO ";transform:scale(.8)'></video>", 0, 0 },
	{ PAGE "<div style='overflow:hidden;width:150px;height:90px'>" VIDEO
	  "'></video></div>", 0, 0 },
	{ PAGE "<div style='overflow:hidden;border-radius:60px;width:246px'>"
	  VIDEO "'></video></div>", 0, 0 },
	{ PAGE VIDEO ";clip-path:circle(40%)'></video>", 0, 0 },
	{ PAGE "<div style='mask-image:linear-gradient(#000,#0000)'>" VIDEO
	  "'></video></div>", 0, 0 },
	{ PAGE "<div style='clip:rect(10px,150px,100px,30px);position:absolute'>"
	  VIDEO "'></video></div>", 0, 0 },
	/* frames with an alpha channel show the page */
	{ PAGE VIDEO "'></video>", 0, 1 },
	/* an older extension's frames are scaled and blended as before */
	{ PAGE "<video controls src=old:v style='display:block;width:200px;"
	  "margin:20px;border-radius:16px'></video>", 0, 0 }
};

static int video_tests(void)
{
	static unsigned char a[W * H * 4], b[W * H * 4], q[W * H * 4];
	unsigned i, fail = 0;
	int y, k;

	soie_ext_register(&fake_media.base);
	soie_ext_register(&old_media.base);
	for (i = 0; i < sizeof(videos) / sizeof(*videos); i++) {
		soie_node *doc = soie_html_parse(videos[i].html,
			strlen(videos[i].html), NULL);
		soie_node **v;
		soie_style_options o;
		const char *why = NULL;
		unsigned covered;
		size_t n;

		fake_alpha = videos[i].alpha;
		memset(&o, 0, sizeof(o));
		o.request = fake_request;
		soie_style_document(doc, &o);
		soie_layout_document(doc, W, H);
		v = soie_query(doc, "video", 1, &n);
		if (!n || soie_media_control(v[0], SOIE_MEDIA_PLAY, 0)) {
			printf("FAIL video %u: no player\n", i);
			fail++;
			free(v);
			soie_node_free(doc);
			continue;
		}
		free(v);
		soie_media_tick(doc, &k);
		soie_document_update(doc);
		covered = paint_covered;
		memset(a, 0x55, sizeof(a));
		soie_paint(doc, a, W, H, W * 4, 0, 0);
		if ((paint_covered != covered) != videos[i].cover)
			why = videos[i].cover ? "no cover taken" :
				"a cover taken";
		covered = paint_covered;
		memset(b, 0xaa, sizeof(b));
		for (y = 0; y < H; y += 16)
			soie_paint(doc, b + y * W * 4, W, 16, W * 4, 0, y);
		if (paint_covered != covered)
			why = "a cover in a low band";
		if (!why && memcmp(a, b, sizeof(a)))
			why = "whole and bands differ";
		memset(q, 0, sizeof(q));
		soie_paint_bgra(doc, q, W, H, W * 4, 0, 0);
		for (k = 0; !why && k < W * H * 4; k += 4)
			if (q[k] != a[k + 2] || q[k + 1] != a[k + 1] ||
			    q[k + 2] != a[k] || q[k + 3] != a[k + 3])
				why = "BGRA";
		/* the frame's rect alone, as a tick has it painted, and
		 * a rect across its edge */
		soie_paint(doc, b + (23 * W + 23) * 4, 200, 120, W * 4, 23, 23);
		soie_paint(doc, b + (50 * W + 100) * 4, 180, 150, W * 4, 100, 50);
		if (!why && memcmp(a, b, sizeof(a)))
			why = "whole and rects differ";
		if (why) {
			printf("FAIL video %u: %s\n", i, why);
			if (getenv("PAINTTEST_OUT")) {
				write_png("tests/paint/video.a.out.png", a);
				write_png("tests/paint/video.b.out.png", b);
			}
			fail++;
		}
		soie_node_free(doc);
	}
	return (int)fail;
}

int main(int argc, char **argv)
{
	int i, update = 0, fail = 0, pass = 0;

	soie_fonts_stub(1);
	for (i = 1; i < argc; i++) {
		char ref[1024], out[1024];
		unsigned char *px, *want;
		size_t len, n;
		char *html;
		const char *why;
		soie_node *doc;

		if (!strcmp(argv[i], "-u")) {
			update = 1;
			continue;
		}
		html = slurp(argv[i], &len);
		if (!html)
			continue;
		n = strlen(argv[i]) - 5;
		snprintf(ref, sizeof(ref), "%.*s.png", (int)n, argv[i]);
		snprintf(out, sizeof(out), "%.*s.out.png", (int)n, argv[i]);
		doc = soie_html_parse(html, len, NULL);
		{
			soie_style_options o;
			char base[2048], cwd[1024];

			memset(&o, 0, sizeof(o));
			if (!getcwd(cwd, sizeof(cwd)))
				cwd[0] = '\0';
			snprintf(base, sizeof(base), "file://%s/%s", cwd,
				argv[i]);
			o.base_url = base;
			o.fetch = fetch;
			soie_style_document(doc, &o);
		}
		soie_layout_document(doc, W, H);
		px = malloc(W * H * 4);
		soie_paint(doc, px, W, H, W * 4, 0, 0);
		if (update) {
			write_png(ref, px);
			printf("wrote %s\n", ref);
		} else if (!(want = read_png(ref)) || !same(want, px)) {
			write_png(out, px);
			printf("FAIL %s (got %s)\n", argv[i], out);
			if (want) {
				int k;

				for (k = 0; k < W * H * 4 && want[k] - px[k] <= 2 &&
				     px[k] - want[k] <= 2; k++)
					;
				printf("  first difference at pixel %d,%d\n",
					k / 4 % W, k / 4 / W);
			}
			fail++;
			free(want);
		} else if ((why = pieces(doc, px))) {
			printf("FAIL %s: painted in pieces (%s) it differs\n",
				argv[i], why);
			fail++;
			free(want);
		} else {
			remove(out);
			pass++;
			free(want);
		}
		free(px);
		free(html);
		soie_node_free(doc);
	}
	if (!update) {
		i = video_tests();
		fail += i;
		pass += !i;
	}
	if (!update)
		printf("paint: %d passed, %d failed\n", pass, fail);
	return fail != 0;
}
