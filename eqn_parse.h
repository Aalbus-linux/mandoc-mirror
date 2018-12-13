/*	$Id$ */
/*
 * Copyright (c) 2011 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2014, 2017 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * External interface of the eqn(7) parser.
 * For use in the roff(7) and eqn(7) parsers only.
 */

struct mparse;
struct roff_node;
struct eqn_box;
struct eqn_def;

struct	eqn_node {
	struct mparse	 *parse;   /* Main parser, for error reporting. */
	struct roff_node *node;    /* Syntax tree of this equation. */
	struct eqn_def	 *defs;    /* Array of definitions. */
	char		 *data;    /* Source code of this equation. */
	char		 *start;   /* First byte of the current token. */
	char		 *end;	   /* First byte of the next token. */
	size_t		  defsz;   /* Number of definitions. */
	size_t		  sz;      /* Length of the source code. */
	size_t		  toksz;   /* Length of the current token. */
	int		  gsize;   /* Default point size. */
	int		  delim;   /* In-line delimiters enabled. */
	char		  odelim;  /* In-line opening delimiter. */
	char		  cdelim;  /* In-line closing delimiter. */
};


struct eqn_node	*eqn_alloc(struct mparse *);
void		 eqn_box_free(struct eqn_box *);
void		 eqn_free(struct eqn_node *);
void		 eqn_parse(struct eqn_node *);
void		 eqn_read(struct eqn_node *, const char *);
void		 eqn_reset(struct eqn_node *);
