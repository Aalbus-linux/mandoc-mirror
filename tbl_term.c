/*	$Id$ */
/*
 * Copyright (c) 2009, 2011 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2011-2018 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"

#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mandoc.h"
#include "out.h"
#include "term.h"

#define	IS_HORIZ(cp)	((cp)->pos == TBL_CELL_HORIZ || \
			 (cp)->pos == TBL_CELL_DHORIZ)

/* Flags for tbl_hrule(). */
#define	HRULE_DBOX	(1 << 0)  /* Top and bottom, ASCII mode only. */
#define	HRULE_DATA	(1 << 1)  /* In the middle of the table. */
#define	HRULE_DOWN	(1 << 2)  /* Allow downward branches. */
#define	HRULE_UP	(1 << 3)  /* Allow upward branches. */
#define	HRULE_ENDS	(1 << 4)  /* Also generate left and right ends. */


static	size_t	term_tbl_len(size_t, void *);
static	size_t	term_tbl_strlen(const char *, void *);
static	size_t	term_tbl_sulen(const struct roffsu *, void *);
static	void	tbl_data(struct termp *, const struct tbl_opts *,
			const struct tbl_cell *,
			const struct tbl_dat *,
			const struct roffcol *);
static	void	tbl_direct_border(struct termp *, int, size_t);
static	void	tbl_fill_border(struct termp *, int, size_t);
static	void	tbl_fill_char(struct termp *, char, size_t);
static	void	tbl_fill_string(struct termp *, const char *, size_t);
static	void	tbl_hrule(struct termp *, const struct tbl_span *, int);
static	void	tbl_literal(struct termp *, const struct tbl_dat *,
			const struct roffcol *);
static	void	tbl_number(struct termp *, const struct tbl_opts *,
			const struct tbl_dat *,
			const struct roffcol *);
static	void	tbl_word(struct termp *, const struct tbl_dat *);


/*
 * The following border-character tables are indexed
 * by ternary (3-based) numbers, as opposed to binary or decimal.
 * Each ternary digit describes the line width in one direction:
 * 0 means no line, 1 single or light line, 2 double or heavy line.
 */

/* Positional values of the four directions. */
#define	BRIGHT	1
#define	BDOWN	3
#define	BLEFT	(3 * 3)
#define	BUP	(3 * 3 * 3)
#define	BHORIZ	(BLEFT + BRIGHT)

/* Code points to use for each combination of widths. */
static  const int borders_utf8[81] = {
	0x0020, 0x2576, 0x257a,  /* 000 right */
	0x2577, 0x250c, 0x250d,  /* 001 down */
	0x257b, 0x250e, 0x250f,  /* 002 */
	0x2574, 0x2500, 0x257c,  /* 010 left */
	0x2510, 0x252c, 0x252e,  /* 011 left down */
	0x2512, 0x2530, 0x2532,  /* 012 */
	0x2578, 0x257e, 0x2501,  /* 020 left */
	0x2511, 0x252d, 0x252f,  /* 021 left down */
	0x2513, 0x2531, 0x2533,  /* 022 */
	0x2575, 0x2514, 0x2515,  /* 100 up */
	0x2502, 0x251c, 0x251d,  /* 101 up down */
	0x257d, 0x251f, 0x2522,  /* 102 */
	0x2518, 0x2534, 0x2536,  /* 110 up left */
	0x2524, 0x253c, 0x253e,  /* 111 all */
	0x2527, 0x2541, 0x2546,  /* 112 */
	0x2519, 0x2535, 0x2537,  /* 120 up left */
	0x2525, 0x253d, 0x253f,  /* 121 all */
	0x252a, 0x2545, 0x2548,  /* 122 */
	0x2579, 0x2516, 0x2517,  /* 200 up */
	0x257f, 0x251e, 0x2521,  /* 201 up down */
	0x2503, 0x2520, 0x2523,  /* 202 */
	0x251a, 0x2538, 0x253a,  /* 210 up left */
	0x2526, 0x2540, 0x2544,  /* 211 all */
	0x2528, 0x2542, 0x254a,  /* 212 */
	0x251b, 0x2539, 0x253b,  /* 220 up left */
	0x2529, 0x2543, 0x2547,  /* 221 all */
	0x252b, 0x2549, 0x254b,  /* 222 */
};

/* ASCII approximations for these code points, compatible with groff. */
static  const int borders_ascii[81] = {
	' ', '-', '=',  /* 000 right */
	'|', '+', '+',  /* 001 down */
	'|', '+', '+',  /* 002 */
	'-', '-', '=',  /* 010 left */
	'+', '+', '+',  /* 011 left down */
	'+', '+', '+',  /* 012 */
	'=', '=', '=',  /* 020 left */
	'+', '+', '+',  /* 021 left down */
	'+', '+', '+',  /* 022 */
	'|', '+', '+',  /* 100 up */
	'|', '+', '+',  /* 101 up down */
	'|', '+', '+',  /* 102 */
	'+', '+', '+',  /* 110 up left */
	'+', '+', '+',  /* 111 all */
	'+', '+', '+',  /* 112 */
	'+', '+', '+',  /* 120 up left */
	'+', '+', '+',  /* 121 all */
	'+', '+', '+',  /* 122 */
	'|', '+', '+',  /* 200 up */
	'|', '+', '+',  /* 201 up down */
	'|', '+', '+',  /* 202 */
	'+', '+', '+',  /* 210 up left */
	'+', '+', '+',  /* 211 all */
	'+', '+', '+',  /* 212 */
	'+', '+', '+',  /* 220 up left */
	'+', '+', '+',  /* 221 all */
	'+', '+', '+',  /* 222 */
};

/* Either of the above according to the selected output encoding. */
static	const int *borders_locale;


static size_t
term_tbl_sulen(const struct roffsu *su, void *arg)
{
	int	 i;

	i = term_hen((const struct termp *)arg, su);
	return i > 0 ? i : 0;
}

static size_t
term_tbl_strlen(const char *p, void *arg)
{
	return term_strlen((const struct termp *)arg, p);
}

static size_t
term_tbl_len(size_t sz, void *arg)
{
	return term_len((const struct termp *)arg, sz);
}


void
term_tbl(struct termp *tp, const struct tbl_span *sp)
{
	const struct tbl_cell	*cp, *cpn, *cpp, *cps;
	const struct tbl_dat	*dp;
	static size_t		 offset;
	size_t			 coloff, tsz;
	int			 hspans, ic, more;
	int			 dvert, fc, horiz, line, uvert;

	/* Inhibit printing of spaces: we do padding ourselves. */

	tp->flags |= TERMP_NOSPACE | TERMP_NONOSPACE;

	/*
	 * The first time we're invoked for a given table block,
	 * calculate the table widths and decimal positions.
	 */

	if (tp->tbl.cols == NULL) {
		borders_locale = tp->enc == TERMENC_UTF8 ?
		    borders_utf8 : borders_ascii;

		tp->tbl.len = term_tbl_len;
		tp->tbl.slen = term_tbl_strlen;
		tp->tbl.sulen = term_tbl_sulen;
		tp->tbl.arg = tp;

		tblcalc(&tp->tbl, sp, tp->tcol->offset, tp->tcol->rmargin);

		/* Tables leak .ta settings to subsequent text. */

		term_tab_set(tp, NULL);
		coloff = sp->opts->opts & (TBL_OPT_BOX | TBL_OPT_DBOX) ||
		    sp->opts->lvert;
		for (ic = 0; ic < sp->opts->cols; ic++) {
			coloff += tp->tbl.cols[ic].width;
			term_tab_iset(coloff);
			coloff += tp->tbl.cols[ic].spacing;
		}

		/* Center the table as a whole. */

		offset = tp->tcol->offset;
		if (sp->opts->opts & TBL_OPT_CENTRE) {
			tsz = sp->opts->opts & (TBL_OPT_BOX | TBL_OPT_DBOX)
			    ? 2 : !!sp->opts->lvert + !!sp->opts->rvert;
			for (ic = 0; ic + 1 < sp->opts->cols; ic++)
				tsz += tp->tbl.cols[ic].width +
				    tp->tbl.cols[ic].spacing;
			if (sp->opts->cols)
				tsz += tp->tbl.cols[sp->opts->cols - 1].width;
			if (offset + tsz > tp->tcol->rmargin)
				tsz -= 1;
			tp->tcol->offset = offset + tp->tcol->rmargin > tsz ?
			    (offset + tp->tcol->rmargin - tsz) / 2 : 0;
		}

		/* Horizontal frame at the start of boxed tables. */

		if (tp->enc == TERMENC_ASCII &&
		    sp->opts->opts & TBL_OPT_DBOX)
			tbl_hrule(tp, sp, HRULE_DBOX | HRULE_ENDS);
		if (sp->opts->opts & (TBL_OPT_DBOX | TBL_OPT_BOX))
			tbl_hrule(tp, sp, HRULE_DOWN | HRULE_ENDS);
	}

	/* Set up the columns. */

	tp->flags |= TERMP_MULTICOL;
	horiz = 0;
	switch (sp->pos) {
	case TBL_SPAN_HORIZ:
	case TBL_SPAN_DHORIZ:
		horiz = 1;
		term_setcol(tp, 1);
		break;
	case TBL_SPAN_DATA:
		term_setcol(tp, sp->opts->cols + 2);
		coloff = tp->tcol->offset;

		/* Set up a column for a left vertical frame. */

		if (sp->opts->opts & (TBL_OPT_BOX | TBL_OPT_DBOX) ||
		    sp->opts->lvert)
			coloff++;
		tp->tcol->rmargin = coloff;

		/* Set up the data columns. */

		dp = sp->first;
		hspans = 0;
		for (ic = 0; ic < sp->opts->cols; ic++) {
			if (hspans == 0) {
				tp->tcol++;
				tp->tcol->offset = coloff;
			}
			coloff += tp->tbl.cols[ic].width;
			tp->tcol->rmargin = coloff;
			if (ic + 1 < sp->opts->cols)
				coloff += tp->tbl.cols[ic].spacing;
			if (hspans) {
				hspans--;
				continue;
			}
			if (dp == NULL)
				continue;
			hspans = dp->hspans;
			if (ic || sp->layout->first->pos != TBL_CELL_SPAN)
				dp = dp->next;
		}

		/* Set up a column for a right vertical frame. */

		tp->tcol++;
		tp->tcol->offset = coloff + 1;
		tp->tcol->rmargin = tp->maxrmargin;

		/* Spans may have reduced the number of columns. */

		tp->lasttcol = tp->tcol - tp->tcols;

		/* Fill the buffers for all data columns. */

		tp->tcol = tp->tcols;
		cp = cpn = sp->layout->first;
		dp = sp->first;
		hspans = 0;
		for (ic = 0; ic < sp->opts->cols; ic++) {
			if (cpn != NULL) {
				cp = cpn;
				cpn = cpn->next;
			}
			if (hspans) {
				hspans--;
				continue;
			}
			tp->tcol++;
			tp->col = 0;
			tbl_data(tp, sp->opts, cp, dp, tp->tbl.cols + ic);
			if (dp == NULL)
				continue;
			hspans = dp->hspans;
			if (cp->pos != TBL_CELL_SPAN)
				dp = dp->next;
		}
		break;
	}

	do {
		/* Print the vertical frame at the start of each row. */

		tp->tcol = tp->tcols;
		uvert = dvert = sp->opts->opts & TBL_OPT_DBOX ? 2 :
		    sp->opts->opts & TBL_OPT_BOX ? 1 : 0;
		if (sp->pos == TBL_SPAN_DATA && uvert < sp->layout->vert)
			uvert = dvert = sp->layout->vert;
		if (sp->next != NULL && sp->next->pos == TBL_SPAN_DATA &&
		    dvert < sp->next->layout->vert)
			dvert = sp->next->layout->vert;
		if (sp->prev != NULL && uvert < sp->prev->layout->vert &&
		    (horiz || (IS_HORIZ(sp->layout->first) &&
		      !IS_HORIZ(sp->prev->layout->first))))
			uvert = sp->prev->layout->vert;
		line = sp->pos == TBL_SPAN_DHORIZ ||
		    sp->layout->first->pos == TBL_CELL_DHORIZ ? 2 :
		    sp->pos == TBL_SPAN_HORIZ ||
		    sp->layout->first->pos == TBL_CELL_HORIZ ? 1 : 0;
		fc = BUP * uvert + BDOWN * dvert + BRIGHT * line;
		if (uvert > 0 || dvert > 0 || (horiz && sp->opts->lvert)) {
			(*tp->advance)(tp, tp->tcols->offset);
			tp->viscol = tp->tcol->offset;
			tbl_direct_border(tp, fc, 1);
		}

		/* Print the data cells. */

		more = 0;
		if (horiz)
			tbl_hrule(tp, sp, HRULE_DATA | HRULE_DOWN | HRULE_UP);
		else {
			cp = sp->layout->first;
			cpn = sp->next == NULL ? NULL :
			    sp->next->layout->first;
			cpp = sp->prev == NULL ? NULL :
			    sp->prev->layout->first;
			dp = sp->first;
			hspans = 0;
			for (ic = 0; ic < sp->opts->cols; ic++) {

				/*
				 * Figure out whether to print a
				 * vertical line after this cell
				 * and advance to next layout cell.
				 */

				uvert = dvert = fc = 0;
				if (cp != NULL) {
					cps = cp;
					while (cps->next != NULL &&
					    cps->next->pos == TBL_CELL_SPAN)
						cps = cps->next;
					if (sp->pos == TBL_SPAN_DATA)
						uvert = dvert = cps->vert;
					switch (cp->pos) {
					case TBL_CELL_HORIZ:
						fc = BHORIZ;
						break;
					case TBL_CELL_DHORIZ:
						fc = BHORIZ * 2;
						break;
					default:
						break;
					}
				}
				if (cpp != NULL) {
					if (uvert < cpp->vert &&
					    cp != NULL &&
					    ((IS_HORIZ(cp) &&
					      !IS_HORIZ(cpp)) ||
					     (cp->next != NULL &&
					      cpp->next != NULL &&
					      IS_HORIZ(cp->next) &&
					      !IS_HORIZ(cpp->next))))
						uvert = cpp->vert;
					cpp = cpp->next;
				}
				if (sp->opts->opts & TBL_OPT_ALLBOX) {
					if (uvert == 0)
						uvert = 1;
					if (dvert == 0)
						dvert = 1;
				}
				if (cpn != NULL) {
					if (dvert == 0 ||
					    (dvert < cpn->vert &&
					     tp->enc == TERMENC_UTF8))
						dvert = cpn->vert;
					cpn = cpn->next;
				}

				/*
				 * Skip later cells in a span,
				 * figure out whether to start a span,
				 * and advance to next data cell.
				 */

				if (hspans) {
					hspans--;
					cp = cp->next;
					continue;
				}
				if (dp != NULL) {
					hspans = dp->hspans;
					if (ic || sp->layout->first->pos
					    != TBL_CELL_SPAN)
						dp = dp->next;
				}

				/*
				 * Print one line of text in the cell
				 * and remember whether there is more.
				 */

				tp->tcol++;
				if (tp->tcol->col < tp->tcol->lastcol)
					term_flushln(tp);
				if (tp->tcol->col < tp->tcol->lastcol)
					more = 1;

				/*
				 * Vertical frames between data cells,
				 * but not after the last column.
				 */

				if (fc == 0 && ((uvert == 0 && dvert == 0 &&
				     (cp->next == NULL ||
				      !IS_HORIZ(cp->next))) ||
				    tp->tcol + 1 == tp->tcols + tp->lasttcol)) {
					cp = cp->next;
					continue;
				}

				if (tp->viscol < tp->tcol->rmargin) {
					(*tp->advance)(tp, tp->tcol->rmargin
					   - tp->viscol);
					tp->viscol = tp->tcol->rmargin;
				}
				while (tp->viscol < tp->tcol->rmargin +
				    tp->tbl.cols[ic].spacing / 2)
					tbl_direct_border(tp, fc, 1);

				if (tp->tcol + 1 == tp->tcols + tp->lasttcol)
					continue;

				if (cp != NULL) {
					switch (cp->pos) {
					case TBL_CELL_HORIZ:
						fc = BLEFT;
						break;
					case TBL_CELL_DHORIZ:
						fc = BLEFT * 2;
						break;
					default:
						fc = 0;
						break;
					}
					cp = cp->next;
				}
				if (cp != NULL) {
					switch (cp->pos) {
					case TBL_CELL_HORIZ:
						fc += BRIGHT;
						break;
					case TBL_CELL_DHORIZ:
						fc += BRIGHT * 2;
						break;
					default:
						break;
					}
				}
				if (tp->tbl.cols[ic].spacing)
					tbl_direct_border(tp, fc +
					    BUP * uvert + BDOWN * dvert, 1);

				if (tp->enc == TERMENC_UTF8)
					uvert = dvert = 0;

				if (fc != 0) {
					if (cp != NULL &&
					    cp->pos == TBL_CELL_HORIZ)
						fc = BHORIZ;
					else if (cp != NULL &&
					    cp->pos == TBL_CELL_DHORIZ)
						fc = BHORIZ * 2;
					else
						fc = 0;
				}
				if (tp->tbl.cols[ic].spacing > 2 &&
				    (uvert > 1 || dvert > 1 || fc != 0))
					tbl_direct_border(tp, fc +
					    BUP * (uvert > 1) +
					    BDOWN * (dvert > 1), 1);
			}
		}

		/* Print the vertical frame at the end of each row. */

		uvert = dvert = sp->opts->opts & TBL_OPT_DBOX ? 2 :
		    sp->opts->opts & TBL_OPT_BOX ? 1 : 0;
		if (sp->pos == TBL_SPAN_DATA &&
		    uvert < sp->layout->last->vert &&
		    sp->layout->last->col + 1 == sp->opts->cols)
			uvert = dvert = sp->layout->last->vert;
		if (sp->next != NULL &&
		    dvert < sp->next->layout->last->vert &&
		    sp->next->layout->last->col + 1 == sp->opts->cols)
			dvert = sp->next->layout->last->vert;
		if (sp->prev != NULL &&
		    uvert < sp->prev->layout->last->vert &&
		    sp->prev->layout->last->col + 1 == sp->opts->cols &&
		    (horiz || (IS_HORIZ(sp->layout->last) &&
		     !IS_HORIZ(sp->prev->layout->last))))
			uvert = sp->prev->layout->last->vert;
		line = sp->pos == TBL_SPAN_DHORIZ ||
		    (sp->layout->last->pos == TBL_CELL_DHORIZ &&
		     sp->layout->last->col + 1 == sp->opts->cols) ? 2 :
		    sp->pos == TBL_SPAN_HORIZ ||
		    (sp->layout->last->pos == TBL_CELL_HORIZ &&
		     sp->layout->last->col + 1 == sp->opts->cols) ? 1 : 0;
		fc = BUP * uvert + BDOWN * dvert + BLEFT * line;
		if (uvert > 0 || dvert > 0 || (horiz && sp->opts->rvert)) {
			if (horiz == 0 && (IS_HORIZ(sp->layout->last) == 0 ||
			    sp->layout->last->col + 1 < sp->opts->cols)) {
				tp->tcol++;
				(*tp->advance)(tp,
				    tp->tcol->offset > tp->viscol ?
				    tp->tcol->offset - tp->viscol : 1);
			}
			tbl_direct_border(tp, fc, 1);
		}
		(*tp->endline)(tp);
		tp->viscol = 0;
	} while (more);

	/*
	 * Clean up after this row.  If it is the last line
	 * of the table, print the box line and clean up
	 * column data; otherwise, print the allbox line.
	 */

	term_setcol(tp, 1);
	tp->flags &= ~TERMP_MULTICOL;
	tp->tcol->rmargin = tp->maxrmargin;
	if (sp->next == NULL) {
		if (sp->opts->opts & (TBL_OPT_DBOX | TBL_OPT_BOX)) {
			tbl_hrule(tp, sp, HRULE_UP | HRULE_ENDS);
			tp->skipvsp = 1;
		}
		if (tp->enc == TERMENC_ASCII &&
		    sp->opts->opts & TBL_OPT_DBOX) {
			tbl_hrule(tp, sp, HRULE_DBOX | HRULE_ENDS);
			tp->skipvsp = 2;
		}
		assert(tp->tbl.cols);
		free(tp->tbl.cols);
		tp->tbl.cols = NULL;
		tp->tcol->offset = offset;
	} else if (horiz == 0 && sp->opts->opts & TBL_OPT_ALLBOX &&
	    (sp->next == NULL || sp->next->pos == TBL_SPAN_DATA ||
	     sp->next->next != NULL))
		tbl_hrule(tp, sp,
		    HRULE_DATA | HRULE_DOWN | HRULE_UP | HRULE_ENDS);

	tp->flags &= ~TERMP_NONOSPACE;
}

static void
tbl_hrule(struct termp *tp, const struct tbl_span *sp, int flags)
{
	const struct tbl_cell *cp, *cpn, *cpp;
	const struct roffcol *col;
	int cross, dvert, line, linewidth, uvert;

	cp = sp->layout->first;
	cpn = cpp = NULL;
	if (flags & HRULE_DATA) {
		linewidth = sp->pos == TBL_SPAN_DHORIZ ? 2 : 1;
		cpn = sp->next == NULL ? NULL : sp->next->layout->first;
		if (cpn == cp)
			cpn = NULL;
	} else
		linewidth = tp->enc == TERMENC_UTF8 &&
		    sp->opts->opts & TBL_OPT_DBOX ? 2 : 1;
	if (tp->viscol == 0) {
		(*tp->advance)(tp, tp->tcols->offset);
		tp->viscol = tp->tcols->offset;
	}
	if (flags & HRULE_ENDS)
		tbl_direct_border(tp, linewidth * (BRIGHT +
		    (flags & (HRULE_UP | HRULE_DBOX) ? BUP : 0) +
		    (flags & (HRULE_DOWN | HRULE_DBOX) ? BDOWN : 0)), 1);
	else {
		cpp = sp->prev == NULL ? NULL : sp->prev->layout->first;
		if (cpp == cp)
			cpp = NULL;
	}
	for (;;) {
		col = tp->tbl.cols + cp->col;
		line = cpn == NULL || cpn->pos != TBL_CELL_DOWN ?
		    BHORIZ * linewidth : 0;
		tbl_direct_border(tp, line, col->width + col->spacing / 2);
		uvert = dvert = 0;
		if (flags & HRULE_UP &&
		    (tp->enc == TERMENC_ASCII || sp->pos == TBL_SPAN_DATA ||
		     (sp->prev != NULL && sp->prev->layout == sp->layout)))
			uvert = cp->vert;
		if (flags & HRULE_DOWN)
			dvert = cp->vert;
		if ((cp = cp->next) == NULL)
			break;
		if (cpp != NULL) {
			if (uvert < cpp->vert)
				uvert = cpp->vert;
			cpp = cpp->next;
		}
		if (cpn != NULL) {
			if (dvert < cpn->vert)
				dvert = cpn->vert;
			cpn = cpn->next;
		}
		if (sp->opts->opts & TBL_OPT_ALLBOX) {
			if (flags & HRULE_UP && uvert == 0)
				uvert = 1;
			if (flags & HRULE_DOWN && dvert == 0)
				dvert = 1;
		}
		cross = BHORIZ * linewidth + BUP * uvert + BDOWN * dvert;
		if (col->spacing)
			tbl_direct_border(tp, cross, 1);
		if (col->spacing > 2)
			tbl_direct_border(tp, tp->enc == TERMENC_ASCII &&
			    (uvert > 1 || dvert > 1) ? cross : line, 1);
		if (col->spacing > 4)
			tbl_direct_border(tp, line, (col->spacing - 3) / 2);
	}
	if (flags & HRULE_ENDS) {
		tbl_direct_border(tp, linewidth * (BLEFT +
		    (flags & (HRULE_UP | HRULE_DBOX) ? BUP : 0) +
		    (flags & (HRULE_DOWN | HRULE_DBOX) ? BDOWN : 0)), 1);
		(*tp->endline)(tp);
		tp->viscol = 0;
	}
}

static void
tbl_data(struct termp *tp, const struct tbl_opts *opts,
    const struct tbl_cell *cp, const struct tbl_dat *dp,
    const struct roffcol *col)
{
	switch (cp->pos) {
	case TBL_CELL_HORIZ:
		tbl_fill_border(tp, BHORIZ, col->width);
		return;
	case TBL_CELL_DHORIZ:
		tbl_fill_border(tp, BHORIZ * 2, col->width);
		return;
	default:
		break;
	}

	if (dp == NULL)
		return;

	switch (dp->pos) {
	case TBL_DATA_NONE:
		return;
	case TBL_DATA_HORIZ:
	case TBL_DATA_NHORIZ:
		tbl_fill_border(tp, BHORIZ, col->width);
		return;
	case TBL_DATA_NDHORIZ:
	case TBL_DATA_DHORIZ:
		tbl_fill_border(tp, BHORIZ * 2, col->width);
		return;
	default:
		break;
	}

	switch (cp->pos) {
	case TBL_CELL_LONG:
	case TBL_CELL_CENTRE:
	case TBL_CELL_LEFT:
	case TBL_CELL_RIGHT:
		tbl_literal(tp, dp, col);
		break;
	case TBL_CELL_NUMBER:
		tbl_number(tp, opts, dp, col);
		break;
	case TBL_CELL_DOWN:
	case TBL_CELL_SPAN:
		break;
	default:
		abort();
	}
}

static void
tbl_fill_string(struct termp *tp, const char *cp, size_t len)
{
	size_t	 i, sz;

	sz = term_strlen(tp, cp);
	for (i = 0; i < len; i += sz)
		term_word(tp, cp);
}

static void
tbl_fill_char(struct termp *tp, char c, size_t len)
{
	char	 cp[2];

	cp[0] = c;
	cp[1] = '\0';
	tbl_fill_string(tp, cp, len);
}

static void
tbl_fill_border(struct termp *tp, int c, size_t len)
{
	char	 buf[12];

	if ((c = borders_locale[c]) > 127) {
		(void)snprintf(buf, sizeof(buf), "\\[u%04x]", c);
		tbl_fill_string(tp, buf, len);
	} else
		tbl_fill_char(tp, c, len);
}

static void
tbl_direct_border(struct termp *tp, int c, size_t len)
{
	size_t	 i, sz;

	c = borders_locale[c];
	sz = (*tp->width)(tp, c);
	for (i = 0; i < len; i += sz) {
		(*tp->letter)(tp, c);
		tp->viscol += sz;
	}
}

static void
tbl_literal(struct termp *tp, const struct tbl_dat *dp,
		const struct roffcol *col)
{
	size_t		 len, padl, padr, width;
	int		 ic, hspans;

	assert(dp->string);
	len = term_strlen(tp, dp->string);
	width = col->width;
	ic = dp->layout->col;
	hspans = dp->hspans;
	while (hspans--)
		width += tp->tbl.cols[++ic].width + 3;

	padr = width > len ? width - len : 0;
	padl = 0;

	switch (dp->layout->pos) {
	case TBL_CELL_LONG:
		padl = term_len(tp, 1);
		padr = padr > padl ? padr - padl : 0;
		break;
	case TBL_CELL_CENTRE:
		if (2 > padr)
			break;
		padl = padr / 2;
		padr -= padl;
		break;
	case TBL_CELL_RIGHT:
		padl = padr;
		padr = 0;
		break;
	default:
		break;
	}

	tbl_fill_char(tp, ASCII_NBRSP, padl);
	tbl_word(tp, dp);
	tbl_fill_char(tp, ASCII_NBRSP, padr);
}

static void
tbl_number(struct termp *tp, const struct tbl_opts *opts,
		const struct tbl_dat *dp,
		const struct roffcol *col)
{
	const char	*cp, *lastdigit, *lastpoint;
	size_t		 intsz, padl, totsz;
	char		 buf[2];

	/*
	 * Almost the same code as in tblcalc_number():
	 * First find the position of the decimal point.
	 */

	assert(dp->string);
	lastdigit = lastpoint = NULL;
	for (cp = dp->string; cp[0] != '\0'; cp++) {
		if (cp[0] == '\\' && cp[1] == '&') {
			lastdigit = lastpoint = cp;
			break;
		} else if (cp[0] == opts->decimal &&
		    (isdigit((unsigned char)cp[1]) ||
		     (cp > dp->string && isdigit((unsigned char)cp[-1]))))
			lastpoint = cp;
		else if (isdigit((unsigned char)cp[0]))
			lastdigit = cp;
	}

	/* Then measure both widths. */

	padl = 0;
	totsz = term_strlen(tp, dp->string);
	if (lastdigit != NULL) {
		if (lastpoint == NULL)
			lastpoint = lastdigit + 1;
		intsz = 0;
		buf[1] = '\0';
		for (cp = dp->string; cp < lastpoint; cp++) {
			buf[0] = cp[0];
			intsz += term_strlen(tp, buf);
		}

		/*
		 * Pad left to match the decimal position,
		 * but avoid exceeding the total column width.
		 */

		if (col->decimal > intsz && col->width > totsz) {
			padl = col->decimal - intsz;
			if (padl + totsz > col->width)
				padl = col->width - totsz;
		}

	/* If it is not a number, simply center the string. */

	} else if (col->width > totsz)
		padl = (col->width - totsz) / 2;

	tbl_fill_char(tp, ASCII_NBRSP, padl);
	tbl_word(tp, dp);

	/* Pad right to fill the column.  */

	if (col->width > padl + totsz)
		tbl_fill_char(tp, ASCII_NBRSP, col->width - padl - totsz);
}

static void
tbl_word(struct termp *tp, const struct tbl_dat *dp)
{
	int		 prev_font;

	prev_font = tp->fonti;
	if (dp->layout->flags & TBL_CELL_BOLD)
		term_fontpush(tp, TERMFONT_BOLD);
	else if (dp->layout->flags & TBL_CELL_ITALIC)
		term_fontpush(tp, TERMFONT_UNDER);

	term_word(tp, dp->string);

	term_fontpopq(tp, prev_font);
}
