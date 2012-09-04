/* fagbnc.c - description TBD
 * See README for contact-, COPYING for license information.  */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include <common.h>
#include <libsrsirc/irc_basic.h>
#include <libsrsirc/irc_con.h>
#include <libsrsirc/irc_io.h>
#include <libsrsirc/irc_util.h>

#include <libsrslog/log.h>
#include <libsrsbsns/addr.h>

#define STRTOUS(STR) ((unsigned short)strtol((STR), NULL, 10))
#define DEF_LISTENPORT ((unsigned short)7778)
#define DEF_LISTENIF "0.0.0.0"

static int g_verb = LOGLVL_WARN;
static bool g_fancy = false;

static char g_listenif[256];
static char g_trgsrv[256];
static unsigned short g_listenport = DEF_LISTENPORT;

static int g_sck;
static ibhnd_t g_irc;


static void process_args(int *argc, char ***argv);
static void init(int *argc, char ***argv);
static void log_reinit(void);
static void usage(FILE *str, const char *a0, int ec);


static void
process_args(int *argc, char ***argv)
{
	char *a0 = (*argv)[0];

	for(int ch; (ch = getopt(*argc, *argv, "vchi:p:s:")) != -1;) {
		switch (ch) {
		case 'i':
			strNcpy(g_listenif, optarg, sizeof g_listenif);
			break;
		case 'p':
			g_listenport = STRTOUS(optarg);
			break;
		case 's':
			strNcpy(g_trgsrv, optarg, sizeof g_trgsrv);
			break;
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
	log_reinit();

	strNcpy(g_listenif, DEF_LISTENIF, sizeof g_listenif);
	
	process_args(argc, argv);

	if (strlen(g_trgsrv) == 0)
		E("no server given (need -s)");

	char host[256];
	unsigned short port;
	parse_hostspec(host, sizeof host, &port, g_trgsrv);

	g_irc = ircbas_init();
	ircbas_set_server(g_irc, host, port);

	g_sck = addr_bind_socket(g_listenif, g_listenport);
	if (g_sck == -1)
		EE("couldn't bind to '%s:%hu'", g_listenif, g_listenport);

	D("bound socket to '%s:%hu'", g_listenif, g_listenport);

	if (listen(g_sck, 128) != 0)
		EE("failed to listen()");
}


static void
log_reinit(void)
{
	log_set_deflevel(g_verb);
	log_set_deffancy(g_fancy);
	int n = log_count_mods();
	for(int i = 0; i < n; i++) {
		log_set_level(log_get_mod(i), g_verb);
		log_set_fancy(log_get_mod(i), g_fancy);
	}
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
	D("initialized");


	close(g_sck);
	ircbas_dispose(g_irc);
	
	return EXIT_SUCCESS;
}
