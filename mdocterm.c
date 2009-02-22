	/* $Id$ */
/*
 * Copyright (c) 2008 Kristaps Dzonsons <kristaps@kth.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
#include <assert.h>
#include <err.h>
#include <getopt.h>
#include <stdlib.h>

#include "mmain.h"
#include "term.h"

int
main(int argc, char *argv[])
{
	struct mmain	*p;
	const struct mdoc *mdoc;
	int		 c;

	extern int	 optreset;
	extern int	 optind;

	p = mmain_alloc();

	if ( ! mmain_getopt(p, argc, argv, ""))
		mmain_exit(p, 1);

	optreset = optind = 1;

	while (-1 != (c = getopt(argc, argv, "")))
		switch (c) {
		default:
			mmain_usage("");
			mmain_exit(p, 1);
			/* NOTREACHED */
		}

	if (NULL == (mdoc = mmain_mdoc(p)))
		mmain_exit(p, 1);

	termprint(mdoc_node(mdoc), mdoc_meta(mdoc));
	mmain_exit(p, 0);
	/* NOTREACHED */
}


