/*	$Id$ */
/*
 * Copyright (c) 2008-2012 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2010-2012, 2014, 2015 Ingo Schwarze <schwarze@openbsd.org>
 * Copyright (c) 2010 Joerg Sonnenberger <joerg@netbsd.org>
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
 */
#include "config.h"

#include <sys/types.h>
#include <sys/param.h>	/* MACHINE */
#include <sys/wait.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <glob.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mandoc_aux.h"
#include "mandoc.h"
#include "roff.h"
#include "mdoc.h"
#include "man.h"
#include "tag.h"
#include "main.h"
#include "manconf.h"
#include "mansearch.h"

#if !defined(__GNUC__) || (__GNUC__ < 2)
# if !defined(lint)
#  define __attribute__(x)
# endif
#endif /* !defined(__GNUC__) || (__GNUC__ < 2) */

enum	outmode {
	OUTMODE_DEF = 0,
	OUTMODE_FLN,
	OUTMODE_LST,
	OUTMODE_ALL,
	OUTMODE_INT,
	OUTMODE_ONE
};

typedef	void		(*out_mdoc)(void *, const struct roff_man *);
typedef	void		(*out_man)(void *, const struct roff_man *);
typedef	void		(*out_free)(void *);

enum	outt {
	OUTT_ASCII = 0,	/* -Tascii */
	OUTT_LOCALE,	/* -Tlocale */
	OUTT_UTF8,	/* -Tutf8 */
	OUTT_TREE,	/* -Ttree */
	OUTT_MAN,	/* -Tman */
	OUTT_HTML,	/* -Thtml */
	OUTT_LINT,	/* -Tlint */
	OUTT_PS,	/* -Tps */
	OUTT_PDF	/* -Tpdf */
};

struct	curparse {
	struct mparse	 *mp;
	struct mchars	 *mchars;	/* character table */
	enum mandoclevel  wlevel;	/* ignore messages below this */
	int		  wstop;	/* stop after a file with a warning */
	enum outt	  outtype;	/* which output to use */
	out_mdoc	  outmdoc;	/* mdoc output ptr */
	out_man		  outman;	/* man output ptr */
	out_free	  outfree;	/* free output ptr */
	void		 *outdata;	/* data for output */
	struct manoutput *outopts;	/* output options */
};

static	int		  fs_lookup(const struct manpaths *,
				size_t ipath, const char *,
				const char *, const char *,
				struct manpage **, size_t *);
static	void		  fs_search(const struct mansearch *,
				const struct manpaths *, int, char**,
				struct manpage **, size_t *);
static	int		  koptions(int *, char *);
#if HAVE_SQLITE3
int			  mandocdb(int, char**);
#endif
static	int		  moptions(int *, char *);
static	void		  mmsg(enum mandocerr, enum mandoclevel,
				const char *, int, int, const char *);
static	void		  parse(struct curparse *, int, const char *);
static	void		  passthrough(const char *, int, int);
static	pid_t		  spawn_pager(struct tag_files *);
static	int		  toptions(struct curparse *, char *);
static	void		  usage(enum argmode) __attribute__((noreturn));
static	int		  woptions(struct curparse *, char *);

extern	char		 *__progname;

static	const int sec_prios[] = {1, 4, 5, 8, 6, 3, 7, 2, 9};
static	char		  help_arg[] = "help";
static	char		 *help_argv[] = {help_arg, NULL};
static	enum mandoclevel  rc;


int
main(int argc, char *argv[])
{
	struct manconf	 conf;
	struct curparse	 curp;
	struct mansearch search;
	struct tag_files *tag_files;
	char		*auxpaths;
	char		*defos;
	unsigned char	*uc;
	struct manpage	*res, *resp;
	char		*conf_file, *defpaths;
	size_t		 isec, i, sz;
	int		 prio, best_prio;
	char		 sec;
	enum mandoclevel rctmp;
	enum outmode	 outmode;
	int		 fd;
	int		 show_usage;
	int		 options;
	int		 use_pager;
	int		 c;

#if !HAVE_PROGNAME
	if (argc < 1)
		__progname = mandoc_strdup("mandoc");
	else if ((__progname = strrchr(argv[0], '/')) == NULL)
		__progname = argv[0];
	else
		++__progname;
#endif

#if HAVE_SQLITE3
	if (strcmp(__progname, BINM_MAKEWHATIS) == 0)
		return mandocdb(argc, argv);
#endif

	/* Search options. */

	memset(&conf, 0, sizeof(conf));
	conf_file = defpaths = NULL;
	auxpaths = NULL;

	memset(&search, 0, sizeof(struct mansearch));
	search.outkey = "Nd";

	if (strcmp(__progname, BINM_MAN) == 0)
		search.argmode = ARG_NAME;
	else if (strcmp(__progname, BINM_APROPOS) == 0)
		search.argmode = ARG_EXPR;
	else if (strcmp(__progname, BINM_WHATIS) == 0)
		search.argmode = ARG_WORD;
	else if (strncmp(__progname, "help", 4) == 0)
		search.argmode = ARG_NAME;
	else
		search.argmode = ARG_FILE;

	/* Parser and formatter options. */

	memset(&curp, 0, sizeof(struct curparse));
	curp.outtype = OUTT_LOCALE;
	curp.wlevel  = MANDOCLEVEL_BADARG;
	curp.outopts = &conf.output;
	options = MPARSE_SO | MPARSE_UTF8 | MPARSE_LATIN1;
	defos = NULL;

	use_pager = 1;
	tag_files = NULL;
	show_usage = 0;
	outmode = OUTMODE_DEF;

	while (-1 != (c = getopt(argc, argv,
			"aC:cfhI:iK:klM:m:O:S:s:T:VW:w"))) {
		switch (c) {
		case 'a':
			outmode = OUTMODE_ALL;
			break;
		case 'C':
			conf_file = optarg;
			break;
		case 'c':
			use_pager = 0;
			break;
		case 'f':
			search.argmode = ARG_WORD;
			break;
		case 'h':
			conf.output.synopsisonly = 1;
			use_pager = 0;
			outmode = OUTMODE_ALL;
			break;
		case 'I':
			if (strncmp(optarg, "os=", 3)) {
				warnx("-I %s: Bad argument", optarg);
				return (int)MANDOCLEVEL_BADARG;
			}
			if (defos) {
				warnx("-I %s: Duplicate argument", optarg);
				return (int)MANDOCLEVEL_BADARG;
			}
			defos = mandoc_strdup(optarg + 3);
			break;
		case 'i':
			outmode = OUTMODE_INT;
			break;
		case 'K':
			if ( ! koptions(&options, optarg))
				return (int)MANDOCLEVEL_BADARG;
			break;
		case 'k':
			search.argmode = ARG_EXPR;
			break;
		case 'l':
			search.argmode = ARG_FILE;
			outmode = OUTMODE_ALL;
			break;
		case 'M':
			defpaths = optarg;
			break;
		case 'm':
			auxpaths = optarg;
			break;
		case 'O':
			search.outkey = optarg;
			while (optarg != NULL)
				manconf_output(&conf.output,
				    strsep(&optarg, ","));
			break;
		case 'S':
			search.arch = optarg;
			break;
		case 's':
			search.sec = optarg;
			break;
		case 'T':
			if ( ! toptions(&curp, optarg))
				return (int)MANDOCLEVEL_BADARG;
			break;
		case 'W':
			if ( ! woptions(&curp, optarg))
				return (int)MANDOCLEVEL_BADARG;
			break;
		case 'w':
			outmode = OUTMODE_FLN;
			break;
		default:
			show_usage = 1;
			break;
		}
	}

	if (show_usage)
		usage(search.argmode);

	/* Postprocess options. */

	if (outmode == OUTMODE_DEF) {
		switch (search.argmode) {
		case ARG_FILE:
			outmode = OUTMODE_ALL;
			use_pager = 0;
			break;
		case ARG_NAME:
			outmode = OUTMODE_ONE;
			break;
		default:
			outmode = OUTMODE_LST;
			break;
		}
	}

	if (outmode == OUTMODE_FLN ||
	    outmode == OUTMODE_LST ||
	    !isatty(STDOUT_FILENO))
		use_pager = 0;

	/* Parse arguments. */

	if (argc > 0) {
		argc -= optind;
		argv += optind;
	}
	resp = NULL;

	/*
	 * Quirks for help(1)
	 * and for a man(1) section argument without -s.
	 */

	if (search.argmode == ARG_NAME) {
		if (*__progname == 'h') {
			if (argc == 0) {
				argv = help_argv;
				argc = 1;
			}
		} else if (argc > 1 &&
		    ((uc = (unsigned char *)argv[0]) != NULL) &&
		    ((isdigit(uc[0]) && (uc[1] == '\0' ||
		      (isalpha(uc[1]) && uc[2] == '\0'))) ||
		     (uc[0] == 'n' && uc[1] == '\0'))) {
			search.sec = (char *)uc;
			argv++;
			argc--;
		}
		if (search.arch == NULL)
			search.arch = getenv("MACHINE");
#ifdef MACHINE
		if (search.arch == NULL)
			search.arch = MACHINE;
#endif
	}

	rc = MANDOCLEVEL_OK;

	/* man(1), whatis(1), apropos(1) */

	if (search.argmode != ARG_FILE) {
		if (argc == 0)
			usage(search.argmode);

		if (search.argmode == ARG_NAME &&
		    outmode == OUTMODE_ONE)
			search.firstmatch = 1;

		/* Access the mandoc database. */

		manconf_parse(&conf, conf_file, defpaths, auxpaths);
#if HAVE_SQLITE3
		mansearch_setup(1);
		if ( ! mansearch(&search, &conf.manpath,
		    argc, argv, &res, &sz))
			usage(search.argmode);
#else
		if (search.argmode != ARG_NAME) {
			fputs("mandoc: database support not compiled in\n",
			    stderr);
			return (int)MANDOCLEVEL_BADARG;
		}
		sz = 0;
#endif

		if (sz == 0) {
			if (search.argmode == ARG_NAME)
				fs_search(&search, &conf.manpath,
				    argc, argv, &res, &sz);
			else
				warnx("nothing appropriate");
		}

		if (sz == 0) {
			rc = MANDOCLEVEL_BADARG;
			goto out;
		}

		/*
		 * For standard man(1) and -a output mode,
		 * prepare for copying filename pointers
		 * into the program parameter array.
		 */

		if (outmode == OUTMODE_ONE) {
			argc = 1;
			best_prio = 10;
		} else if (outmode == OUTMODE_ALL)
			argc = (int)sz;

		/* Iterate all matching manuals. */

		resp = res;
		for (i = 0; i < sz; i++) {
			if (outmode == OUTMODE_FLN)
				puts(res[i].file);
			else if (outmode == OUTMODE_LST)
				printf("%s - %s\n", res[i].names,
				    res[i].output == NULL ? "" :
				    res[i].output);
			else if (outmode == OUTMODE_ONE) {
				/* Search for the best section. */
				isec = strcspn(res[i].file, "123456789");
				sec = res[i].file[isec];
				if ('\0' == sec)
					continue;
				prio = sec_prios[sec - '1'];
				if (prio >= best_prio)
					continue;
				best_prio = prio;
				resp = res + i;
			}
		}

		/*
		 * For man(1), -a and -i output mode, fall through
		 * to the main mandoc(1) code iterating files
		 * and running the parsers on each of them.
		 */

		if (outmode == OUTMODE_FLN || outmode == OUTMODE_LST)
			goto out;
	}

	/* mandoc(1) */

	if (search.argmode == ARG_FILE && ! moptions(&options, auxpaths))
		return (int)MANDOCLEVEL_BADARG;

	curp.mchars = mchars_alloc();
	curp.mp = mparse_alloc(options, curp.wlevel, mmsg,
	    curp.mchars, defos);

	/*
	 * Conditionally start up the lookaside buffer before parsing.
	 */
	if (OUTT_MAN == curp.outtype)
		mparse_keep(curp.mp);

	if (argc < 1) {
		if (use_pager)
			tag_files = tag_init();
		parse(&curp, STDIN_FILENO, "<stdin>");
	}

	while (argc > 0) {
		rctmp = mparse_open(curp.mp, &fd,
		    resp != NULL ? resp->file : *argv);
		if (rc < rctmp)
			rc = rctmp;

		if (fd != -1) {
			if (use_pager) {
				tag_files = tag_init();
				use_pager = 0;
			}

			if (resp == NULL)
				parse(&curp, fd, *argv);
			else if (resp->form & FORM_SRC) {
				/* For .so only; ignore failure. */
				chdir(conf.manpath.paths[resp->ipath]);
				parse(&curp, fd, resp->file);
			} else
				passthrough(resp->file, fd,
				    conf.output.synopsisonly);

			if (argc > 1 && curp.outtype <= OUTT_UTF8)
				ascii_sepline(curp.outdata);
		}

		if (MANDOCLEVEL_OK != rc && curp.wstop)
			break;

		if (resp != NULL)
			resp++;
		else
			argv++;
		if (--argc)
			mparse_reset(curp.mp);
	}

	if (curp.outfree)
		(*curp.outfree)(curp.outdata);
	mparse_free(curp.mp);
	mchars_free(curp.mchars);

out:
	if (search.argmode != ARG_FILE) {
		manconf_free(&conf);
#if HAVE_SQLITE3
		mansearch_free(res, sz);
		mansearch_setup(0);
#endif
	}

	free(defos);

	/*
	 * When using a pager, finish writing both temporary files,
	 * fork it, wait for the user to close it, and clean up.
	 */

	if (tag_files != NULL) {
		fclose(stdout);
		tag_write();
		waitpid(spawn_pager(tag_files), NULL, 0);
		tag_unlink();
	}

	return (int)rc;
}

static void
usage(enum argmode argmode)
{

	switch (argmode) {
	case ARG_FILE:
		fputs("usage: mandoc [-acfhkl] [-I os=name] "
		    "[-K encoding] [-mformat] [-O option]\n"
		    "\t      [-T output] [-W level] [file ...]\n", stderr);
		break;
	case ARG_NAME:
		fputs("usage: man [-acfhklw] [-C file] [-I os=name] "
		    "[-K encoding] [-M path] [-m path]\n"
		    "\t   [-O option=value] [-S subsection] [-s section] "
		    "[-T output] [-W level]\n"
		    "\t   [section] name ...\n", stderr);
		break;
	case ARG_WORD:
		fputs("usage: whatis [-acfhklw] [-C file] "
		    "[-M path] [-m path] [-O outkey] [-S arch]\n"
		    "\t      [-s section] name ...\n", stderr);
		break;
	case ARG_EXPR:
		fputs("usage: apropos [-acfhklw] [-C file] "
		    "[-M path] [-m path] [-O outkey] [-S arch]\n"
		    "\t       [-s section] expression ...\n", stderr);
		break;
	}
	exit((int)MANDOCLEVEL_BADARG);
}

static int
fs_lookup(const struct manpaths *paths, size_t ipath,
	const char *sec, const char *arch, const char *name,
	struct manpage **res, size_t *ressz)
{
	glob_t		 globinfo;
	struct manpage	*page;
	char		*file;
	int		 form, globres;

	form = FORM_SRC;
	mandoc_asprintf(&file, "%s/man%s/%s.%s",
	    paths->paths[ipath], sec, name, sec);
	if (access(file, R_OK) != -1)
		goto found;
	free(file);

	mandoc_asprintf(&file, "%s/cat%s/%s.0",
	    paths->paths[ipath], sec, name);
	if (access(file, R_OK) != -1) {
		form = FORM_CAT;
		goto found;
	}
	free(file);

	if (arch != NULL) {
		mandoc_asprintf(&file, "%s/man%s/%s/%s.%s",
		    paths->paths[ipath], sec, arch, name, sec);
		if (access(file, R_OK) != -1)
			goto found;
		free(file);
	}

	mandoc_asprintf(&file, "%s/man%s/%s.[01-9]*",
	    paths->paths[ipath], sec, name);
	globres = glob(file, 0, NULL, &globinfo);
	if (globres != 0 && globres != GLOB_NOMATCH)
		warn("%s: glob", file);
	free(file);
	if (globres == 0)
		file = mandoc_strdup(*globinfo.gl_pathv);
	globfree(&globinfo);
	if (globres != 0)
		return 0;

found:
#if HAVE_SQLITE3
	warnx("outdated mandoc.db lacks %s(%s) entry, run makewhatis %s\n",
	    name, sec, paths->paths[ipath]);
#endif
	*res = mandoc_reallocarray(*res, ++*ressz, sizeof(struct manpage));
	page = *res + (*ressz - 1);
	page->file = file;
	page->names = NULL;
	page->output = NULL;
	page->ipath = ipath;
	page->bits = NAME_FILE & NAME_MASK;
	page->sec = (*sec >= '1' && *sec <= '9') ? *sec - '1' + 1 : 10;
	page->form = form;
	return 1;
}

static void
fs_search(const struct mansearch *cfg, const struct manpaths *paths,
	int argc, char **argv, struct manpage **res, size_t *ressz)
{
	const char *const sections[] =
	    {"1", "8", "6", "2", "3", "3p", "5", "7", "4", "9"};
	const size_t nsec = sizeof(sections)/sizeof(sections[0]);

	size_t		 ipath, isec, lastsz;

	assert(cfg->argmode == ARG_NAME);

	*res = NULL;
	*ressz = lastsz = 0;
	while (argc) {
		for (ipath = 0; ipath < paths->sz; ipath++) {
			if (cfg->sec != NULL) {
				if (fs_lookup(paths, ipath, cfg->sec,
				    cfg->arch, *argv, res, ressz) &&
				    cfg->firstmatch)
					return;
			} else for (isec = 0; isec < nsec; isec++)
				if (fs_lookup(paths, ipath, sections[isec],
				    cfg->arch, *argv, res, ressz) &&
				    cfg->firstmatch)
					return;
		}
		if (*ressz == lastsz)
			warnx("No entry for %s in the manual.", *argv);
		lastsz = *ressz;
		argv++;
		argc--;
	}
}

static void
parse(struct curparse *curp, int fd, const char *file)
{
	enum mandoclevel  rctmp;
	struct roff_man	 *man;

	/* Begin by parsing the file itself. */

	assert(file);
	assert(fd >= -1);

	rctmp = mparse_readfd(curp->mp, fd, file);
	if (rc < rctmp)
		rc = rctmp;

	/*
	 * With -Wstop and warnings or errors of at least the requested
	 * level, do not produce output.
	 */

	if (rctmp != MANDOCLEVEL_OK && curp->wstop)
		return;

	/* If unset, allocate output dev now (if applicable). */

	if ( ! (curp->outman && curp->outmdoc)) {
		switch (curp->outtype) {
		case OUTT_HTML:
			curp->outdata = html_alloc(curp->mchars,
			    curp->outopts);
			curp->outfree = html_free;
			break;
		case OUTT_UTF8:
			curp->outdata = utf8_alloc(curp->mchars,
			    curp->outopts);
			curp->outfree = ascii_free;
			break;
		case OUTT_LOCALE:
			curp->outdata = locale_alloc(curp->mchars,
			    curp->outopts);
			curp->outfree = ascii_free;
			break;
		case OUTT_ASCII:
			curp->outdata = ascii_alloc(curp->mchars,
			    curp->outopts);
			curp->outfree = ascii_free;
			break;
		case OUTT_PDF:
			curp->outdata = pdf_alloc(curp->mchars,
			    curp->outopts);
			curp->outfree = pspdf_free;
			break;
		case OUTT_PS:
			curp->outdata = ps_alloc(curp->mchars,
			    curp->outopts);
			curp->outfree = pspdf_free;
			break;
		default:
			break;
		}

		switch (curp->outtype) {
		case OUTT_HTML:
			curp->outman = html_man;
			curp->outmdoc = html_mdoc;
			break;
		case OUTT_TREE:
			curp->outman = tree_man;
			curp->outmdoc = tree_mdoc;
			break;
		case OUTT_MAN:
			curp->outmdoc = man_mdoc;
			curp->outman = man_man;
			break;
		case OUTT_PDF:
		case OUTT_ASCII:
		case OUTT_UTF8:
		case OUTT_LOCALE:
		case OUTT_PS:
			curp->outman = terminal_man;
			curp->outmdoc = terminal_mdoc;
			break;
		default:
			break;
		}
	}

	mparse_result(curp->mp, &man, NULL);

	/* Execute the out device, if it exists. */

	if (man == NULL)
		return;
	if (curp->outmdoc != NULL && man->macroset == MACROSET_MDOC)
		(*curp->outmdoc)(curp->outdata, man);
	if (curp->outman != NULL && man->macroset == MACROSET_MAN)
		(*curp->outman)(curp->outdata, man);
}

static void
passthrough(const char *file, int fd, int synopsis_only)
{
	const char	 synb[] = "S\bSY\bYN\bNO\bOP\bPS\bSI\bIS\bS";
	const char	 synr[] = "SYNOPSIS";

	FILE		*stream;
	const char	*syscall;
	char		*line;
	size_t		 len, off;
	ssize_t		 nw;
	int		 print;

	fflush(stdout);

	if ((stream = fdopen(fd, "r")) == NULL) {
		close(fd);
		syscall = "fdopen";
		goto fail;
	}

	print = 0;
	while ((line = fgetln(stream, &len)) != NULL) {
		if (synopsis_only) {
			if (print) {
				if ( ! isspace((unsigned char)*line))
					goto done;
				while (len &&
				    isspace((unsigned char)*line)) {
					line++;
					len--;
				}
			} else {
				if ((len == sizeof(synb) &&
				     ! strncmp(line, synb, len - 1)) ||
				    (len == sizeof(synr) &&
				     ! strncmp(line, synr, len - 1)))
					print = 1;
				continue;
			}
		}
		for (off = 0; off < len; off += nw)
			if ((nw = write(STDOUT_FILENO, line + off,
			    len - off)) == -1 || nw == 0) {
				fclose(stream);
				syscall = "write";
				goto fail;
			}
	}

	if (ferror(stream)) {
		fclose(stream);
		syscall = "fgetln";
		goto fail;
	}

done:
	fclose(stream);
	return;

fail:
	warn("%s: SYSERR: %s", file, syscall);
	if (rc < MANDOCLEVEL_SYSERR)
		rc = MANDOCLEVEL_SYSERR;
}

static int
koptions(int *options, char *arg)
{

	if ( ! strcmp(arg, "utf-8")) {
		*options |=  MPARSE_UTF8;
		*options &= ~MPARSE_LATIN1;
	} else if ( ! strcmp(arg, "iso-8859-1")) {
		*options |=  MPARSE_LATIN1;
		*options &= ~MPARSE_UTF8;
	} else if ( ! strcmp(arg, "us-ascii")) {
		*options &= ~(MPARSE_UTF8 | MPARSE_LATIN1);
	} else {
		warnx("-K %s: Bad argument", arg);
		return 0;
	}
	return 1;
}

static int
moptions(int *options, char *arg)
{

	if (arg == NULL)
		/* nothing to do */;
	else if (0 == strcmp(arg, "doc"))
		*options |= MPARSE_MDOC;
	else if (0 == strcmp(arg, "andoc"))
		/* nothing to do */;
	else if (0 == strcmp(arg, "an"))
		*options |= MPARSE_MAN;
	else {
		warnx("-m %s: Bad argument", arg);
		return 0;
	}

	return 1;
}

static int
toptions(struct curparse *curp, char *arg)
{

	if (0 == strcmp(arg, "ascii"))
		curp->outtype = OUTT_ASCII;
	else if (0 == strcmp(arg, "lint")) {
		curp->outtype = OUTT_LINT;
		curp->wlevel  = MANDOCLEVEL_WARNING;
	} else if (0 == strcmp(arg, "tree"))
		curp->outtype = OUTT_TREE;
	else if (0 == strcmp(arg, "man"))
		curp->outtype = OUTT_MAN;
	else if (0 == strcmp(arg, "html"))
		curp->outtype = OUTT_HTML;
	else if (0 == strcmp(arg, "utf8"))
		curp->outtype = OUTT_UTF8;
	else if (0 == strcmp(arg, "locale"))
		curp->outtype = OUTT_LOCALE;
	else if (0 == strcmp(arg, "xhtml"))
		curp->outtype = OUTT_HTML;
	else if (0 == strcmp(arg, "ps"))
		curp->outtype = OUTT_PS;
	else if (0 == strcmp(arg, "pdf"))
		curp->outtype = OUTT_PDF;
	else {
		warnx("-T %s: Bad argument", arg);
		return 0;
	}

	return 1;
}

static int
woptions(struct curparse *curp, char *arg)
{
	char		*v, *o;
	const char	*toks[7];

	toks[0] = "stop";
	toks[1] = "all";
	toks[2] = "warning";
	toks[3] = "error";
	toks[4] = "unsupp";
	toks[5] = "fatal";
	toks[6] = NULL;

	while (*arg) {
		o = arg;
		switch (getsubopt(&arg, UNCONST(toks), &v)) {
		case 0:
			curp->wstop = 1;
			break;
		case 1:
		case 2:
			curp->wlevel = MANDOCLEVEL_WARNING;
			break;
		case 3:
			curp->wlevel = MANDOCLEVEL_ERROR;
			break;
		case 4:
			curp->wlevel = MANDOCLEVEL_UNSUPP;
			break;
		case 5:
			curp->wlevel = MANDOCLEVEL_BADARG;
			break;
		default:
			warnx("-W %s: Bad argument", o);
			return 0;
		}
	}

	return 1;
}

static void
mmsg(enum mandocerr t, enum mandoclevel lvl,
		const char *file, int line, int col, const char *msg)
{
	const char	*mparse_msg;

	fprintf(stderr, "%s: %s:", __progname, file);

	if (line)
		fprintf(stderr, "%d:%d:", line, col + 1);

	fprintf(stderr, " %s", mparse_strlevel(lvl));

	if (NULL != (mparse_msg = mparse_strerror(t)))
		fprintf(stderr, ": %s", mparse_msg);

	if (msg)
		fprintf(stderr, ": %s", msg);

	fputc('\n', stderr);
}

static pid_t
spawn_pager(struct tag_files *tag_files)
{
#define MAX_PAGER_ARGS 16
	char		*argv[MAX_PAGER_ARGS];
	const char	*pager;
	char		*cp;
	size_t		 cmdlen;
	int		 argc;
	pid_t		 pager_pid;

	pager = getenv("MANPAGER");
	if (pager == NULL || *pager == '\0')
		pager = getenv("PAGER");
	if (pager == NULL || *pager == '\0')
		pager = "more -s";
	cp = mandoc_strdup(pager);

	/*
	 * Parse the pager command into words.
	 * Intentionally do not do anything fancy here.
	 */

	argc = 0;
	while (argc + 4 < MAX_PAGER_ARGS) {
		argv[argc++] = cp;
		cp = strchr(cp, ' ');
		if (cp == NULL)
			break;
		*cp++ = '\0';
		while (*cp == ' ')
			cp++;
		if (*cp == '\0')
			break;
	}

	/* For more(1) and less(1), use the tag file. */

	if ((cmdlen = strlen(argv[0])) >= 4) {
		cp = argv[0] + cmdlen - 4;
		if (strcmp(cp, "less") == 0 || strcmp(cp, "more") == 0) {
			argv[argc++] = mandoc_strdup("-T");
			argv[argc++] = tag_files->tfn;
		}
	}
	argv[argc++] = tag_files->ofn;
	argv[argc] = NULL;

	switch (pager_pid = fork()) {
	case -1:
		err((int)MANDOCLEVEL_SYSERR, "fork");
	case 0:
		break;
	default:
		return pager_pid;
	}

	/* The child process becomes the pager. */

	if (dup2(tag_files->ofd, STDOUT_FILENO) == -1)
		err((int)MANDOCLEVEL_SYSERR, "pager stdout");
	close(tag_files->ofd);
	close(tag_files->tfd);
	execvp(argv[0], argv);
	err((int)MANDOCLEVEL_SYSERR, "exec %s", argv[0]);
}
