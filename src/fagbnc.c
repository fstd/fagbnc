/* fagbnc.c - description TBD
 * See README for contact-, COPYING for license information.  */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>

#include <common.h>
#include <libsrsirc/irc_basic.h>
#include <libsrsirc/irc_con.h>
#include <libsrsirc/irc_io.h>
#include <libsrsirc/irc_util.h>

#include <libsrsbsns/log.h>


static int g_verb = LOGLVL_ERR;
static bool g_fancy = false;


static void process_args(int *argc, char ***argv);
static void init(int *argc, char ***argv);
static void log_reinit(void);
static void usage(FILE *str, const char *a0, int ec);


static void
process_args(int *argc, char ***argv)
{
	char *a0 = (*argv)[0];

	for(int ch; (ch = getopt(*argc, *argv, "vch")) != -1;) {
		switch (ch) {
		case 'c':
			g_fancy = true;
			log_reinit();
			break;
		case 'v':
			g_verb++;
			log_reinit();
			break;
		case 'h':
			usage(stdout, a0, EXIT_SUCCESS);
			break;
		default:
			usage(stderr, a0, EXIT_FAILURE);
		}
	}
	*argc -= optind;
	*argv += optind;
}


static void
init(int *argc, char ***argv)
{
	LOG_INITX("fagbnc", LOGLVL_ERR, stderr, false);
	ircbas_dispose(ircbas_init());//init dem loggers
	log_reinit();

	process_args(argc, argv);
}


static void
log_reinit(void)
{
	LOG_COLORS(g_fancy);
	LOG_LEVEL(g_verb);
	ircbas_log_set_fancy(g_fancy);
	irccon_log_set_fancy(g_fancy);
	ircio_log_set_fancy(g_fancy);
	ircbas_log_set_loglvl(g_verb);
	irccon_log_set_loglvl(g_verb);
	ircio_log_set_loglvl(g_verb);
}


static void
usage(FILE *str, const char *a0, int ec)
{
	#define SH(STR) if (sh) fputs(STR "\n", str)
	#define LH(STR) if (!sh) fputs(STR "\n", str)
	#define BH(STR) fputs(STR "\n", str)
	BH("==================================");
	BH("== fagbnc - the pretty cool bnc ==");
	BH("==================================");
	fprintf(str, "usage: %s [-vch]\n", a0);
	BH("");
	BH("(C) 2012, fisted (contact: #fstd @ irc.freenode.org)");
	#undef SH
	#undef LH
	#undef BH
	exit(ec);
}


int
main(int argc, char **argv)
{
	init(&argc, &argv);

	return EXIT_SUCCESS;
}
