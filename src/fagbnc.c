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
#include <libsrsbsns/io.h>

#include "ucbase.h"

#define STRTOUS(STR) ((unsigned short)strtol((STR), NULL, 10))
#define DEF_LISTENPORT ((unsigned short)7778)
#define DEF_LISTENIF "0.0.0.0"
#define MAX_IRCARGS 16

static int g_verb = LOGLVL_WARN;
static bool g_fancy = false;

static char g_listenif[256];
static char g_trgsrv[256];
static unsigned short g_listenport = DEF_LISTENPORT;

static int g_sck;
static int g_clt;
static ibhnd_t g_irc;
static bool g_ucbinit = false;


static void process_args(int *argc, char ***argv);
static void init(int *argc, char ***argv);
static void log_reinit(void);
static void usage(FILE *str, const char *a0, int ec);
static void joinmsg(char *dest, size_t destsz, const char *const *msg);
static void disconnected(void);
static void handle_ircmsg(char **msg, size_t nelem);
static void handle_353(const char *chan, const char *users);


static void
life(void)
{
	for(;;) {
		if (!ircbas_online(g_irc))
			disconnected();

		char *tok[MAX_IRCARGS];
		int r = ircbas_read(g_irc, tok, 16, 10000);

		if (r == -1)
			continue;

		if (r == 1) {
			dumpmsg(0, tok, MAX_IRCARGS);
			char buf[1024];
			joinmsg(buf, sizeof buf, (const char* const*)tok);

			io_fprintf(g_clt, "%s\r\n", buf);

			handle_ircmsg(tok, MAX_IRCARGS);
		}

		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(g_clt, &fds);
		struct timeval to = {0, 0};

		r = select(g_clt+1, &fds, NULL, NULL, &to);
		if (r == -1)
			E("select() failed");

		if (r == 1) {
			char buf[1024];
			r = io_read_line(g_clt, buf, sizeof buf);
			if (r == -1)
				EE("io_read_line failed");

			char *end = buf + strlen(buf) - 1;
			while (end >= buf && (*end == '\r' || *end == '\n' || *end == ' '))
				*end-- = '\0';

			if (strlen(buf) > 0)
				ircbas_write(g_irc, buf);
		}
	}
}


static void
handle_ircmsg(char **msg, size_t nelem)
{
	static bool s_got005casemap, s_got005prefix;
	char nick[64];
	pfx_extract_nick(nick, sizeof nick, msg[0]);
	if (strcmp(msg[1], "005") == 0) {
		if (!g_ucbinit) {
			for(size_t i = 3; i < nelem; i++) {
				if (!msg[i])
					break;
				if (strstr(msg[i], "CASEMAP"))
					s_got005casemap = true;
				if (strstr(msg[i], "PREFIX"))
					s_got005prefix = true;
			}
			if (s_got005casemap && s_got005prefix) {
				ucb_init(ircbas_casemap(g_irc), ircbas_005modepfx(g_irc)[1]);
				g_ucbinit = true;
			}
		}
	} else if (strcmp(msg[1], "JOIN") == 0) {
		if (istrcasecmp(ircbas_mynick(g_irc), nick, ircbas_casemap(g_irc)) == 0) {
			ucb_add_chan(msg[2]);
		} else {
			ucb_add_user(msg[2], nick);
		}
	} else if (strcmp(msg[1], "PART") == 0) {
		if (istrcasecmp(ircbas_mynick(g_irc), nick, ircbas_casemap(g_irc)) == 0) {
			ucb_drop_chan(msg[2]);
		} else {
			ucb_drop_user(msg[2], nick);
		}
	} else if (strcmp(msg[1], "QUIT") == 0) {
		ucb_drop_user_all(nick);
	} else if (strcmp(msg[1], "NICK") == 0) {
		if (istrcasecmp(ircbas_mynick(g_irc), msg[2], ircbas_casemap(g_irc)) != 0) {
			ucb_rename_user(nick, msg[2]);
		}
	} else if (strcmp(msg[1], "KICK") == 0) {
		if (istrcasecmp(ircbas_mynick(g_irc), msg[3], ircbas_casemap(g_irc)) == 0) {
			ucb_drop_chan(msg[2]);
		} else {
			ucb_drop_user(msg[2], msg[3]);
		}
	} else if (strcmp(msg[1], "353") == 0) { //RPL_NAMES
		if (ucb_is_chan_sync(msg[4])) {
			ucb_clear_chan(msg[4]);
			ucb_set_chan_sync(msg[4], false);
		}
		handle_353(msg[4], msg[5]);
	} else if (strcmp(msg[1], "366") == 0) { //RPL_ENDOFNAMES
		ucb_set_chan_sync(msg[2], true);
	} else if (strcmp(msg[1], "PRIVMSG") == 0 && strstr(msg[3], "dump")) {
		ucb_dump();
	}
}


static void
handle_353(const char *chan, const char *users)
{
	char *ubuf = strdup(users);
	char *tok = strtok(ubuf, " ");

	do {
		ucb_add_user(chan, tok);
	} while ((tok = strtok(NULL, " ")));
	free(users);
}

static void
disconnected(void)
{
	E("damn!");
}


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

	g_clt = accept(g_sck, NULL, NULL);

	if (g_clt == -1)
		EE("failed to accept()");

	char buf[1024];
	int r;
	bool gotnick = false;
	bool gotuser = false;
	bool gotpass = false;
	char *uname, *fname, *nick, *pass;
	int conflags;
	do {
		r = io_read_line(g_clt, buf, sizeof buf);	
		if (r == -1)
			E("io_read_line failed");
		if (strncmp(buf, "PASS", 4) == 0) {
			gotpass = true;
			char *tok = strtok(buf, " ");
			pass = strdup(strtok(NULL, " "));
		} else if (strncmp(buf, "NICK", 4) == 0) {
			gotnick = true;
			char *tok = strtok(buf, " ");
			nick = strdup(strtok(NULL, " "));
		} else if (strncmp(buf, "USER", 4) == 0) {
			gotuser = true;
			char *tok = strtok(buf, " ");
			uname = strdup(strtok(NULL, " "));
			conflags = (int)strtol(strtok(NULL, " "), NULL, 10);
			strtok(NULL, " ");
			fname = strdup(strtok(NULL, " ")+1);
		}
	} while (!gotnick || !gotuser);

	ircbas_set_nick(g_irc, nick);
	if (gotpass)
		ircbas_set_pass(g_irc, pass);
	ircbas_set_uname(g_irc, uname);
	ircbas_set_fname(g_irc, fname);
	ircbas_set_conflags(g_irc, conflags);

	if (!ircbas_connect(g_irc, 30000000)) {
		E("failed to connect/logon to IRC");
	}

	const char * const* const* lc = ircbas_logonconv(g_irc);
	for(int i = 0; i < 4; i++) {
		char buf[1024];
		joinmsg(buf, sizeof buf, lc[i]);

		io_fprintf(g_clt, "%s\r\n", buf);
	}

	life();

	close(g_sck);
	ircbas_dispose(g_irc);
	
	return EXIT_SUCCESS;
}


static void
joinmsg(char *dest, size_t destsz, const char *const *msg)
{
	snprintf(dest, destsz, ":%s %s", msg[0], msg[1]);

	int j = 2;
	while(msg[j]) {
		strNcat(dest, " ", destsz);
		if (!msg[j+1] && strchr(msg[j], ' '))
			strNcat(dest, ":", destsz);
		strNcat(dest, msg[j], destsz);
		j++;
	}
}
