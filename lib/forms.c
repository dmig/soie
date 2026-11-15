/*
 * Form controls, painted by the engine: text fields with their value,
 * placeholder and caret, checkboxes, radio buttons, push buttons and
 * selects. Their box (background, border) comes from the UA style.
 *
 * Copyright (C) 2026 Christophe Lincoln <pankso@slitaz.org>
 * Written with the assistance of Claude (Anthropic) and Opencode.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdlib.h>
#include <string.h>
#include "paint.h"

static const char *option_text(const soie_node *sel, struct buf *b)
{
	const soie_node *n = sel->first, *pick = NULL, *first = NULL;

	/* the selected option, else the first one */
	while (n && n != sel) {
		if (is_html(n, TAG_OPTION)) {
			if (!first)
				first = n;
			if (soie_element_get_attr(n, "selected")) {
				pick = n;
				break;
			}
		}
		if (n->first) {
			n = n->first;
			continue;
		}
		while (n != sel && !n->next)
			n = n->parent;
		if (n != sel)
			n = n->next;
	}
	if (!pick)
		pick = first;
	if (pick) {
		const soie_node *t;

		for (t = pick->first; t; t = t->next)
			if (t->type == SOIE_TEXT)
				buf_add(b, t->u.text.data, t->u.text.len);
	}
	buf_add(b, "", 0);
	return b->s;
}

/* A number attribute of a control, dflt when missing or not one */
static float control_num(const soie_node *n, const char *name, float dflt)
{
	const char *a = soie_element_get_attr(n, name);
	char *e;
	float v;

	if (!a)
		return dflt;
	v = strtof(a, &e);
	return e == a ? dflt : v;
}


/* Text fields. What they show is their value, a password as bullets
 * (one a character); a line of an input is moved left to keep the
 * caret in sight, a textarea starts at the line that does. Painting
 * and soie_field_offset() place the text alike. */

static int is_password(const soie_node *n)
{
	const char *t = soie_element_get_attr(n, "type");

	return n->tag == TAG_INPUT && t && ascii_ieq(t, "password");
}

/* The text shown in field n into t */
static void field_shown(const soie_node *n, struct buf *t)
{
	const char *v = soie_element_get_attr(n, "value");
	const soie_node *c;

	t->len = 0;
	buf_add(t, "", 0);
	if (n->tag == TAG_TEXTAREA && !v) {
		for (c = n->first; c; c = c->next)
			if (c->type == SOIE_TEXT)
				buf_add(t, c->u.text.data, c->u.text.len);
	} else if (v && is_password(n)) {
		for (; *v; v++)
			if (((unsigned char)*v & 0xc0) != 0x80)
				buf_add(t, "\xe2\x80\xa2", 3);
	} else if (v) {
		buf_add(t, v, strlen(v));
	}
}

/* Value offset off as an offset in the text shown (len bytes) */
static size_t shown_off(const soie_node *n, int off, size_t len)
{
	size_t k = 0;
	const char *v;

	if (off < 0)
		return len;
	if (is_password(n) && (v = soie_element_get_attr(n, "value"))) {
		int i;

		for (i = 0; v[i] && i < off; i++)
			if (((unsigned char)v[i] & 0xc0) != 0x80)
				k += 3;
	} else {
		k = (size_t)off;
	}
	return k < len ? k : len;
}

/* An offset in the text shown as one in the value */
static int value_off(const soie_node *n, size_t k)
{
	const char *v;
	size_t chars = k / 3;
	int i = 0;

	if (!is_password(n) || !(v = soie_element_get_attr(n, "value")))
		return (int)k;
	for (; v[i]; i++)
		if (((unsigned char)v[i] & 0xc0) != 0x80 && !chars--)
			break;
	return i;
}

/* How far an input's line moves left: its caret (at k) in sight */
static float field_shift(const struct style *st, const struct buf *t,
	size_t k, float avail)
{
	float w = text_width(st, t->s, k);

	return w > avail - 1 ? w - (avail - 1) : 0;
}

/* The first line a textarea of height h shows: the caret's (at k) is
 * one of them */
static int first_line(const struct buf *t, size_t k, float h, float lh)
{
	int line = 0, rows = lh > 0 ? (int)(h / lh) : 1;
	size_t i;

	for (i = 0; i < k && i < t->len; i++)
		if (t->s[i] == '\n')
			line++;
	if (rows < 1)
		rows = 1;
	return line >= rows ? line - rows + 1 : 0;
}

/* The character boundary of s (len bytes) nearest to x from its start */
static size_t nearest(const struct style *st, const char *s, size_t len,
	float x)
{
	size_t i = 0, best = 0;
	float bd = 1e30f;

	for (;;) {
		float w = text_width(st, s, i), d = fabsf(w - x);

		if (d < bd) {
			bd = d;
			best = i;
		}
		if (i >= len || w > x)
			break;
		i++;
		while (i < len && ((unsigned char)s[i] & 0xc0) == 0x80)
			i++;
	}
	return best;
}

/* The selection's background from a to z of s, at x on a line; its
 * line break selected too (nl), a space more */
static void paint_selected(struct canvas *c, const struct style *st,
	const char *s, size_t a, size_t z, float x, float top, float h,
	int nl)
{
	static const unsigned char blue[4] = { 0, 120, 215, 102 };
	float x0 = text_width(st, s, a), x1 = text_width(st, s, z);

	if (nl)
		x1 += text_width(st, " ", 1);
	fill_rect(c, x + x0, top, x1 - x0, h, blue);
}

int soie_field_offset(soie_node *doc, soie_node *el, float x, float y)
{
	const struct box *b;
	struct fontm m;
	struct buf t = { 0 };
	size_t k = 0;
	int r;

	if (doc->type != SOIE_DOCUMENT || !el || el->type != SOIE_ELEMENT ||
	    (el->tag != TAG_INPUT && el->tag != TAG_TEXTAREA) ||
	    !(b = element_box(el)))
		return -1;
	font_metrics(b->style, &m);
	field_shown(el, &t);
	if (doc->u.doc->focus == el)
		k = shown_off(el, doc->u.doc->caret, t.len);
	if (el->tag == TAG_TEXTAREA) {
		float lh = m.ascent + m.descent;
		int line = first_line(&t, k, b->h, lh) +
			(lh > 0 && y > b->y ? (int)((y - b->y) / lh) : 0);
		const char *s = t.s, *e;

		while (line-- > 0 && (e = strchr(s, '\n')))
			s = e + 1;
		e = strchr(s, '\n');
		r = (int)(s - t.s) + (int)nearest(b->style, s,
			e ? (size_t)(e - s) : strlen(s), x - b->x);
	} else {
		float sh = doc->u.doc->focus == el ?
			field_shift(b->style, &t, k, b->w) : 0;

		r = value_off(el, nearest(b->style, t.s, t.len,
			x - (b->x - sh)));
	}
	buf_free(&t);
	return r;
}

void paint_control(struct canvas *c, const struct box *b)
{
	const soie_node *n = b->node;
	const struct style *st = b->style;
	const char *type = soie_element_get_attr(n, "type"), *v;
	unsigned char fg[4];
	struct fontm m;
	struct buf text = { 0 };
	float base, x = b->x, tw;
	int focused = n->doc->u.doc->focus == n;

	color_to_rgba(st->v[P_COLOR].u.color, fg);
	font_metrics(st, &m);
	base = b->y + (b->h - m.ascent - m.descent) / 2 + m.ascent;
	if (n->tag == TAG_INPUT && type && (ascii_ieq(type, "checkbox") ||
					    ascii_ieq(type, "radio"))) {
		/* the colours of Firefox's and Chrome's, not the text's:
		 * a gray ring on white, the accent blue when checked */
		static const unsigned char ring[4] = { 118, 118, 118, 255 },
			white[4] = { 255, 255, 255, 255 },
			accent[4] = { 0, 117, 255, 255 };
		int checked = soie_element_get_attr(n, "checked") != NULL;
		float s = b->w < b->h ? b->w : b->h, x0 = b->x, y0 = b->y;

		if (ascii_ieq(type, "radio")) {
			fill_round(c, x0, y0, s, s, s / 2,
				checked ? accent : ring);
			fill_round(c, x0 + 1, y0 + 1, s - 2, s - 2,
				(s - 2) / 2, white);
			if (checked)
				fill_round(c, x0 + s * 0.25f, y0 + s * 0.25f,
					s * 0.5f, s * 0.5f, s * 0.25f, accent);
			return;
		}
		if (!checked) {
			fill_round(c, x0, y0, s, s, 2, ring);
			fill_round(c, x0 + 1, y0 + 1, s - 2, s - 2, 1.5f,
				white);
		} else {
			/* a tick, in fractions of the box */
			static const float tick[12] = { .20f, .52f, .40f, .70f,
				.78f, .28f, .88f, .40f, .40f, .88f, .10f, .62f };
			float p[12];
			int i;

			for (i = 0; i < 12; i += 2) {
				p[i] = x0 + tick[i] * s;
				p[i + 1] = y0 + tick[i + 1] * s;
			}
			fill_round(c, x0, y0, s, s, 2, accent);
			fill_polygon(c, p, 6, white);
		}
		return;
	}
	if (n->tag != TAG_INPUT && n->tag != TAG_SELECT &&
	    n->tag != TAG_TEXTAREA) {
		/* progress and meter: a rounded track, filled up to the
		 * value (none: nothing filled) */
		static const unsigned char track[4] = { 233, 233, 237, 255 },
			edge[4] = { 143, 143, 157, 255 },
			blue[4] = { 0, 96, 223, 255 },
			green[4] = { 23, 163, 74, 255 };
		float mn = n->name[0] == 'm' ? control_num(n, "min", 0) : 0;
		float mx = control_num(n, "max", 1);
		float val = control_num(n, "value", mn), f;

		f = mx > mn ? (val - mn) / (mx - mn) : 0;
		f = f < 0 ? 0 : f > 1 ? 1 : f;
		fill_round(c, b->x, b->y, b->w, b->h, b->h / 2, edge);
		fill_round(c, b->x + 1, b->y + 1, b->w - 2, b->h - 2,
			b->h / 2 - 1, track);
		if (f * b->w >= b->h)
			fill_round(c, b->x, b->y, f * b->w, b->h, b->h / 2,
				n->name[0] == 'm' ? green : blue);
		return;
	}
	if (n->tag == TAG_INPUT && type && ascii_ieq(type, "range")) {
		/* a track with its thumb where the value is */
		static const unsigned char track[4] = { 233, 233, 237, 255 },
			edge[4] = { 143, 143, 157, 255 },
			blue[4] = { 0, 96, 223, 255 },
			thumb[4] = { 103, 103, 116, 255 };
		float mn = control_num(n, "min", 0), mx = control_num(n, "max",
			100);
		float val = control_num(n, "value", (mn + mx) / 2), f;
		float th = b->h < 16 ? b->h : 16, ty = b->y + (b->h - 6) / 2;

		f = mx > mn ? (val - mn) / (mx - mn) : 0;
		f = f < 0 ? 0 : f > 1 ? 1 : f;
		fill_round(c, b->x, ty, b->w, 6, 3, edge);
		fill_round(c, b->x + 1, ty + 1, b->w - 2, 4, 2, track);
		fill_round(c, b->x, ty, th / 2 + f * (b->w - th), 6, 3, blue);
		fill_round(c, b->x + f * (b->w - th), b->y + (b->h - th) / 2,
			th, th, th / 2, thumb);
		return;
	}
	if (n->tag == TAG_INPUT && type && ascii_ieq(type, "color")) {
		/* the color it holds, black by default */
		static const unsigned char edge[4] = { 118, 118, 118, 255 };
		unsigned char col[4] = { 0, 0, 0, 255 };

		v = soie_element_get_attr(n, "value");
		if (v && *v == '#' && strlen(v) == 7) {
			unsigned long x24 = strtoul(v + 1, NULL, 16);

			col[0] = (unsigned char)(x24 >> 16);
			col[1] = (unsigned char)(x24 >> 8);
			col[2] = (unsigned char)x24;
		}
		fill_rect(c, b->x, b->y, b->w, b->h, edge);
		fill_rect(c, b->x + 1, b->y + 1, b->w - 2, b->h - 2, col);
		return;
	}
	if (n->tag == TAG_INPUT && type && ascii_ieq(type, "file")) {
		/* its button, then what is chosen: nothing here */
		static const unsigned char face[4] = { 233, 233, 237, 255 },
			edge[4] = { 143, 143, 157, 255 };
		static const char label[] = "Browse\xe2\x80\xa6";
		static const char none[] = "No file selected.";
		float bw = text_width(st, label, sizeof(label) - 1) + 10;

		fill_round(c, b->x, b->y, bw, b->h, 4, edge);
		fill_round(c, b->x + 1, b->y + 1, bw - 2, b->h - 2, 3, face);
		paint_text(c, st, label, sizeof(label) - 1, b->x + 5, base);
		paint_text(c, st, none, sizeof(none) - 1, b->x + bw + 5, base);
		return;
	}
	if (n->tag == TAG_SELECT && (soie_element_get_attr(n, "multiple") ||
	    control_num(n, "size", 1) > 1)) {
		/* a list box: its first options, one a row */
		const soie_node *o = n->first;
		float lh = m.normal_lh + 4, y = b->y;

		while (o && o != n && y + lh <= b->y + b->h + 1) {
			if (is_html(o, TAG_OPTION)) {
				const soie_node *t;

				text.len = 0;
				for (t = o->first; t; t = t->next)
					if (t->type == SOIE_TEXT)
						buf_add(&text, t->u.text.data,
							t->u.text.len);
				paint_text(c, st, text.s ? text.s : "", text.len,
					x + 4, y + 2 + m.ascent);
				y += lh;
			}
			if (o->first && !is_html(o, TAG_OPTION)) {
				o = o->first;
				continue;
			}
			while (o != n && !o->next)
				o = o->parent;
			if (o != n)
				o = o->next;
		}
		buf_free(&text);
		return;
	}
	if (n->tag == TAG_SELECT) {
		/* the chosen option and a chevron */
		float ax = b->x + b->w - 11, ay = b->y + b->h / 2 - 2;
		float p[12];

		option_text(n, &text);
		paint_text(c, st, text.s, text.len, x, base);
		p[0] = ax, p[1] = ay;
		p[2] = ax + 4, p[3] = ay + 4;
		p[4] = ax + 8, p[5] = ay;
		p[6] = ax + 8, p[7] = ay + 1.6f;
		p[8] = ax + 4, p[9] = ay + 5.6f;
		p[10] = ax, p[11] = ay + 1.6f;
		fill_polygon(c, p, 6, fg);
		buf_free(&text);
		return;
	}
	if (n->tag == TAG_TEXTAREA) {
		/* one line per newline, no wrapping yet */
		float lh = m.ascent + m.descent;
		size_t k = 0, lo = 0, hi = 0;
		const char *s, *e;
		int line, first;

		field_shown(n, &text);
		if (focused) {
			size_t a = shown_off(n, n->doc->u.doc->anchor, text.len);

			k = shown_off(n, n->doc->u.doc->caret, text.len);
			lo = a < k ? a : k;
			hi = a < k ? k : a;
		}
		first = first_line(&text, k, b->h, lh);
		base = b->y + m.ascent;
		for (s = text.s, line = 0; s && base < b->y + b->h + m.ascent;
		     line++) {
			size_t at = (size_t)(s - text.s), ll;

			e = strchr(s, '\n');
			ll = e ? (size_t)(e - s) : strlen(s);
			if (line >= first) {
				if (lo < hi && lo <= at + ll && hi > at)
					paint_selected(c, st, s, lo > at ?
						lo - at : 0, hi > at + ll ? ll :
						hi - at, x, base - m.ascent, lh,
						hi > at + ll);
				paint_text(c, st, s, ll, x, base);
				if (focused && lo == hi && k >= at &&
				    k <= at + ll)
					fill_rect(c, x + text_width(st, s,
						k - at), base - m.ascent, 1, lh,
						fg);
				base += lh;
			}
			s = e ? e + 1 : NULL;
		}
		buf_free(&text);
		return;
	}
	if (type && (ascii_ieq(type, "submit") || ascii_ieq(type, "reset") ||
		     ascii_ieq(type, "button"))) {
		v = soie_element_get_attr(n, "value");
		if (!v)
			v = ascii_ieq(type, "reset") ? "Reset" :
				ascii_ieq(type, "submit") ? "Submit" : "";
		tw = text_width(st, v, strlen(v));
		paint_text(c, st, v, strlen(v), x + (b->w - tw) / 2, base);
		return;
	}
	field_shown(n, &text);
	v = text.s;
	if (!*v && !focused &&
	    (v = soie_element_get_attr(n, "placeholder"))) {
		/* the text's colour, faded as browsers do (Firefox 0.54) */
		float a = c->alpha;

		c->alpha *= 0.55f;
		paint_text(c, st, v, strlen(v), x, base);
		c->alpha = a;
		buf_free(&text);
		return;
	}
	{
		int cx0 = c->cx0, cx1 = c->cx1;
		int l = (int)(b->x - b->p[3] - c->ox);
		int r = (int)(b->x + b->w - c->ox + 1);
		size_t k = 0, a = 0;
		float sh = 0;

		if (l > c->cx0)
			c->cx0 = l;
		if (r < c->cx1)
			c->cx1 = r;
		if (focused) {
			k = shown_off(n, n->doc->u.doc->caret, text.len);
			a = shown_off(n, n->doc->u.doc->anchor, text.len);
			sh = field_shift(st, &text, k, b->w);
		}
		if (a != k)
			paint_selected(c, st, v, a < k ? a : k, a < k ? k : a,
				x - sh, base - m.ascent, m.ascent + m.descent, 0);
		paint_text(c, st, v, text.len, x - sh, base);
		if (focused && a == k) {
			tw = text_width(st, v, k);
			fill_rect(c, x - sh + tw, base - m.ascent, 1,
				m.ascent + m.descent, fg);
		}
		c->cx0 = cx0;
		c->cx1 = cx1;
	}
	buf_free(&text);
}
