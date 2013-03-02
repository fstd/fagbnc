/* fagbnc.c - description TBD
 * See README for contact-, COPYING for license information.  */

/* XXX: endless connect loop, ignoring client disconnect */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <signal.h>

#include <common.h>
#include <libsrsirc/irc_basic.h>
#include <libsrsirc/irc_con.h>
#include <libsrsirc/irc_io.h>
#include <libsrsirc/irc_util.h>

#include <libsrslog/log.h>
#include <libsrsbsns/addr.h>
#include <libsrsbsns/io.h>

#include "qwrap.h"
#include "ucbase.h"



#define STRTOUS(STR) ((unsigned short)strtol((STR), NULL, 10))
#define STRTOI(STR) ((int)strtol((STR), NULL, 10))
#define DEF_LISTENPORT ((unsigned short)7778)
#define DEF_LISTENIF "0.0.0.0"
#define MAX_IRCARGS 16
#define MAX_NICKLEN 15
#define MAX_JOIN_AT_ONCE 10
#define SYNC_DELAY 15
#define LGRAB_TIME 20
#define SHUTDOWN_TIME 20
#define SEND_DELAY 2

/* shadowing the irclib calls and g_irc, to keep things
 * readable */
#define IRESET() ircbas_reset(g_irc)
#define IDISPOSE() ircbas_dispose(g_irc)
#define ICONNECT(TOUT) ircbas_connect(g_irc, (TOUT)*1000000ul)
#define IREAD(TOK,TOKLEN,TO_US) ircbas_read(g_irc,(TOK),(TOKLEN),(TO_US))
#define IWRITE(LINE) ircbas_write(g_irc, (LINE))
#define IONLINE() ircbas_online(g_irc)
#define ICASEMAP() ircbas_casemap(g_irc)
#define ISTRCASECMP(A,B) istrcasecmp((A),(B),ircbas_casemap(g_irc))
#define ISTRNCASECMP(A,B,N) istrncasecmp((A),(B),(N),ircbas_casemap(g_irc))

typedef void (*sig_t) (int);

static int g_verb = LOGLVL_WARN;
static bool g_fancy = false;

static char g_listen_if[256];
static char g_irc_srv[256];
static unsigned short g_irc_port = DEF_LISTENPORT;

static ibhnd_t g_irc;

static int g_listen_sck;
static int g_clt_sck;

static bool g_synced = true;
static bool g_grab_logon = true;
static time_t g_last_clt_ping;
static time_t g_next_con_try;

static char g_sync_nick[MAX_NICKLEN+1];
static char *g_needpong;
static time_t g_last_num;
static bool g_shutdown;
static bool g_keep_trying;
static volatile bool g_hup;
static int g_max_lag;
static char *g_prim_nick;
static int g_hup_wait_time;

static void* g_irc_sendQ;
static void* g_irc_logonQ;
static void* g_irc_confirmQ;
static void* g_irc_missQ;
static void* g_irc_outmissQ;



static void life(void);
static void process_irc(void);
static void handle_irc_msg(char **msg, size_t nelem);
static void handle_irc_353(const char *chan, const char *users);
static void handle_irc_cmode(const char *chan, const char *const *modes);

static int clt_read_line(char *dest, size_t destsz);
static void handle_clt_msg(const char *line);
static void handle_fagcmd(const char *line);

static int clt_printf(const char *fmt, ...);
static void send_logon_conv();
static void replay_logon(void);
static void resync(void);

static void process_sendQ(void);

static void on_disconnect(void);

static bool is_modepfx_chr(char c);
static bool is_modepfx_sym(char c);
static bool is_weaker_modepfx_sym(char c1, char c2);
static char translate_modepfx(char c);

static void join_irc_msg(char *dest, size_t destsz, const char *const *msg,
                                                           bool colonTrail);
static bool dump_irc_msg(char **msg, size_t msg_len, void *tag);
static bool dump_irc_msg_ex(char **msg, size_t msg_len, void *tag,
                                                          bool colonTrail);

static void process_args(int *argc, char ***argv);
static int init(int *argc, char ***argv);
static void setup_clt(void);
static void cleanup(void);
static void log_reinit(void);
static void usage(FILE *str, const char *a0, int ec);
int main(int argc, char **argv);
void sighnd(int sig);



static void
life(void)
{
	bool fresh = true;
	g_next_con_try = time(NULL);
	for(;;) {
		if (!IONLINE()) {
			D("not online! (fresh: %d, shutdown: %d, nextcontry: %ld)",
				fresh, g_shutdown, g_next_con_try);

			if (g_shutdown)
				break;

			if (!g_next_con_try && !fresh) {
				N("calling disconnect handler");
				on_disconnect();
			}

			if (g_next_con_try <= time(NULL)) {
				N("time for connect has arrived, connecting");
				if (ICONNECT(30)) {
					N("connected (fresh: %d)", fresh);
					if (fresh) {
						strNcpy(g_sync_nick,
						       ircbas_mynick(g_irc),
						       sizeof g_sync_nick);
						fresh = false;
						D("not fresh anymore, saved syncnick as '%s', sending logon conv now", g_sync_nick);
						send_logon_conv();
					} else {
						D("replaying logon");
						replay_logon();
					}

					g_last_num = time(NULL);
					g_next_con_try = 0;
				} else {
					if (fresh && !g_keep_trying)
						E("failed to connect");

					W("failed to connect, attempting next connect in 30 seconds");
					g_next_con_try = time(NULL) + 30;
				}
			}
		}

		if (IONLINE()) {

			process_irc();

			if (g_grab_logon && g_last_num + LGRAB_TIME < time(NULL)) {
				N("first sync complete, disabling g_grab_logon which was %d", g_grab_logon);
				clt_printf(":-fagbnc!fag@bnc NOTICE %s "
					   ":first sync complete\r\n", g_sync_nick);
				g_grab_logon = false;
			}

			if (!g_synced && g_last_num + SYNC_DELAY < time(NULL)) {
				N("time for resync has come");
				resync();
				clt_printf(":-fagbnc!fag@bnc NOTICE %s "
							":synced\r\n", g_sync_nick);
			}

			if (g_synced && g_max_lag && g_last_clt_ping &&
					  time(NULL)-g_last_clt_ping > g_max_lag) {
				W("too laggy (%d sec), assuming d/c, resetting irc backend", g_max_lag);
				IRESET();
				continue;
			}

			if (g_synced && g_hup) {
				W("SIGHUP causing a hard reset (as intended)");
				g_hup = false;
				IRESET();
				N("calling disconnect handler sicne we were synced");
				on_disconnect();
				N("next connection attempt in %d seconds", g_hup_wait_time);
				g_next_con_try = time(NULL) + g_hup_wait_time;
			}
		} else
			usleep(20000);

		if (g_shutdown && g_shutdown < time(NULL)) {
			W("aborting life loop due to shutdown");
			break;
		}

		char buf[1024];
		int r = clt_read_line(buf, sizeof buf);
		if (r == -1) {
			EE("clt_read_line failed");
			//setup_clt(true);
		}
		else if (r != 0) {
			handle_clt_msg(buf);
		}

		if (IONLINE())
			process_sendQ();
	}
}


static void
process_irc(void)
{
	char *tok[MAX_IRCARGS];
	int r = IREAD(tok, 16, 10000);
	bool colon = ircbas_colon_trail(g_irc);


	if (r == 1) {
		dump_irc_msg_ex(tok, MAX_IRCARGS, NULL, colon);

		if (tok[1][0] >= '0' && tok[1][0] <= '9')
			g_last_num = time(NULL);

		handle_irc_msg(tok, MAX_IRCARGS);

		char buf[1024];
		join_irc_msg(buf, sizeof buf, (const char*const*)tok, colon);

		if (g_synced) {
			clt_printf("%s\r\n", buf);
			q_clear(g_irc_confirmQ);
		} else {
			if (strcmp(tok[1], "PRIVMSG") == 0) {
				q_add(g_irc_missQ, false, buf);
				N("added to missQ: '%s'", buf);
				//Q_DUMP(g_irc_missQ);
			}
		}
	} else if (r == -1)
		W("IREAD failed");
}


static void
handle_irc_msg(char **msg, size_t nelem)
{
	static bool g_ucbinit = false;
	char nick[64];
	pfx_extract_nick(nick, sizeof nick, msg[0]);
	if (strcmp(msg[1], "005") == 0) {
		if (!g_ucbinit) {
			for(size_t i = 3; i < nelem; i++) {
				if (!msg[i])
					break;
				if (strstr(msg[i], "CASEMAP"))
					ucb_set_casemap(ICASEMAP());
				if (strstr(msg[i], "PREFIX"))
					ucb_set_modepfx(
					       ircbas_005modepfx(g_irc)[1]);
			}
			g_ucbinit = true;
		}
	} else if (strcmp(msg[1], "JOIN") == 0) {
		if (ISTRCASECMP(ircbas_mynick(g_irc), nick) == 0) {
			ucb_add_chan(msg[2]);
			ucb_set_chan_sync(msg[2], true);
		} else {
			ucb_add_user(msg[2], nick);
		}
	} else if (strcmp(msg[1], "PART") == 0) {
		if (ISTRCASECMP(ircbas_mynick(g_irc), nick) == 0) {
			ucb_drop_chan(msg[2]);
		} else {
			ucb_drop_user(msg[2], nick);
		}
	} else if (strcmp(msg[1], "QUIT") == 0) {
		ucb_drop_user_all(nick);
		if (ISTRCASECMP(nick, g_prim_nick) == 0) {
			N("getting our nick back...");
			char buf[512];
			snprintf(buf, sizeof buf, "NICK %s\r\n", g_prim_nick);
			q_add(g_irc_sendQ, false, buf);
			//Q_DUMP(g_irc_sendQ);
		}
	} else if (strcmp(msg[1], "NICK") == 0) {
		if (ISTRCASECMP(ircbas_mynick(g_irc), msg[2]) != 0) {
			ucb_rename_user(nick, msg[2]);
		} else if (g_synced)
			strNcpy(g_sync_nick, ircbas_mynick(g_irc),
			                                sizeof g_sync_nick);
	} else if (strcmp(msg[1], "KICK") == 0) {
		if (ISTRCASECMP(ircbas_mynick(g_irc), msg[3]) == 0) {
			ucb_drop_chan(msg[2]);
		} else {
			ucb_drop_user(msg[2], msg[3]);
		}
	} else if (strcmp(msg[1], "MODE") == 0) {
		if (msg[2][0] == '#') //fix when supporting 005 CHANTYPES
			handle_irc_cmode(msg[2], (const char*const*)msg+3);
	} else if (strcmp(msg[1], "353") == 0) { //RPL_NAMES
		if (ucb_is_chan_sync(msg[4])) {
			ucb_clear_chan(msg[4]);
			ucb_set_chan_sync(msg[4], false);
		}
		handle_irc_353(msg[4], msg[5]);
	} else if (strcmp(msg[1], "366") == 0) { //RPL_ENDOFNAMES
		ucb_set_chan_sync(msg[3], true);
	} else if (strcmp(msg[1], "PING") == 0) {
		if (!g_synced) {
			D("PING while desynced, queunig PONG");
			char buf[512];
			snprintf(buf, sizeof buf, "PONG :%s\r\n", msg[2]);
			q_add(g_irc_sendQ, false, buf);
			//Q_DUMP(g_irc_sendQ);
		}
	} else if (strcmp(msg[1], "PONG") == 0) {
		free(g_needpong);
		g_needpong = NULL;
		g_last_clt_ping = 0;
	} else if (strcmp(msg[1], "PRIVMSG") == 0
	                                    && strstr(msg[3], "dump plx")) {
		ucb_dump();//XXX remove this eventually
	}
}


static void
handle_irc_353(const char *chan, const char *users)
{
	char *ubuf = strdup(users);
	char *tok = strtok(ubuf, " ");

	do {
		ucb_add_user(chan, tok);
	} while ((tok = strtok(NULL, " ")));
	free(ubuf);
}


static void
handle_irc_cmode(const char *ch, const char *const *modes)
{
	size_t modecnt = 0;
	while(modecnt < (MAX_IRCARGS-3) && modes[modecnt])
		modecnt++;
	size_t num;
	char **p = parse_chanmodes(modes, modecnt, &num,
	                                        ircbas_005modepfx(g_irc)[0],
	                                        ircbas_005chanmodes(g_irc));
	for(size_t i = 0; i < num; i++) {
		bool on = p[i][0] == '+';
		char mc = p[i][1];
		if (is_modepfx_chr(mc)) {
			mc = translate_modepfx(mc);
			char *bname = p[i] + 3;
			char buf[MAX_NICKLEN+2];
			if (!ucb_get_user(buf, sizeof buf, ch, bname)) {
				W("unknown user: '%s'", bname);
				continue;
			}

			if (is_modepfx_sym(buf[0])) {
				if (is_weaker_modepfx_sym(mc, buf[0])) {
					continue;
				}

				ucb_reprefix_user(ch, bname, on ? mc : ' ');
			} else if (on) {
				ucb_reprefix_user(ch, bname, mc);
			}
		}
	}
	for(size_t i = 0; i < num; i++)
		free(p[i]);
	free(p);

}


static int
clt_read_line(char *dest, size_t destsz)
{
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(g_clt_sck, &fds);
	struct timeval to = {0, 0};

	int r = select(g_clt_sck+1, &fds, NULL, NULL, &to);
	if (r == -1)
		E("select() failed");

	if (r == 1) {
		r = io_read_line(g_clt_sck, dest, destsz);
		if (r == -1) {
			W("io_read_line() failed");
			return -1;
		}

		char *end = dest + strlen(dest) - 1;
		while (end >= dest && (*end == '\r' || *end == '\n'
		                                            || *end == ' '))
			*end-- = '\0';
		return strlen(dest);
	}

	return 0;
}


static void
handle_clt_msg(const char *line)
{
	static time_t firstline = 0;
	D("handling from client: '%s'", line);
	if (g_grab_logon) {
		if (!firstline)
			firstline = time(NULL);
		int secoff = (int)(time(NULL) - firstline);

		char buf[1024];
		char tmp[128];
		snprintf(tmp, sizeof tmp, "MODE %s", ircbas_mynick(g_irc));
		if (ISTRNCASECMP(tmp, line, strlen(tmp)) == 0)
			snprintf(buf, sizeof buf, "%d $USERMODE$%s",
			                        secoff, line + strlen(tmp));
		else if (strncmp("MODE ", line, 5) == 0
		                         || strncmp("WHO ", line, 4) == 0
		                         || strncmp("PING", line, 4) == 0
		                         || strncmp("NOTICE", line, 6) == 0) 
			buf[0] = '\0'; //exclude these for now
		else if (strncmp("PRIVMSG -fagbnc ", line, 15) == 0)
			;//exclude
		else
			snprintf(buf, sizeof buf, "%d %s", secoff, line);

		if (strlen(buf) > 0) {
			q_add(g_irc_logonQ, false, buf);
			//Q_DUMP(g_irc_logonQ);
		}
	}

	if (strncmp(line, "JOIN ", 5) == 0) {
		char *dup = strdup(line);
		char *chans = strtok(dup+5, " ");
		char *keys = strtok(NULL, " ");
		if (keys) {
			keys = strdup(keys);
			chans = strdup(chans);
			char *cctx, *kctx;
			char *chan = strtok_r(chans, ",", &cctx);
			char *key = strtok_r(keys, ",", &kctx);
			do {
				ucb_store_key(chan, key);
				chan = strtok_r(NULL, ",", &cctx);
				key = strtok_r(NULL, ",", &kctx);
			} while (chan && key);
			free(chans);
			free(keys);
		}
		free(dup);
	} else if (strncmp(line, "PING ", 5) == 0) {
		char *dup = strdup(line);
		char *tok = strtok(dup+5, " ");
		if (!g_synced) {
			g_last_clt_ping = 0;
			D("not synced, fake a PONG (:%1$s PONG %1$s :%1$s)",
			                                               tok);
			clt_printf(":%1$s PONG %1$s :%1$s\r\n",
			                                               tok);
		} else {
			g_last_clt_ping = time(NULL);
			free(g_needpong);
			g_needpong = strdup(tok);
			D("synced, we need a PONG soon, fgt");
		}
		free(dup);
		if (!g_synced) {
			D("not synced, we wont add this to irc sendQ");
			return; //don't queue this ping
		}
	} else if (strncmp(line, "PRIVMSG ", 8) == 0) {
		if (strncmp (line+8,"-fagbnc :", 9) == 0) {
			//fagbnc internal command
			handle_fagcmd(line+9+8);
			return; //don't queue internal commands
		} else {
			if (!g_synced) {
				q_add(g_irc_outmissQ, false, line);
				//Q_DUMP(g_irc_outmissQ);
			} else {
				q_add(g_irc_confirmQ, false, line);
				//Q_DUMP(g_irc_confirmQ);
			}
		}
	}

	if (g_synced)
		q_add(g_irc_sendQ, false, line);
	//Q_DUMP(g_irc_sendQ);
}


static void
handle_fagcmd(const char *line)
{
	D("fagcmd: '%s'", line);
	if (strncmp(line, "DUMPREPQ", 8) == 0) {
		void *tmpQ = q_init();

		int i = 0;
		while(q_size(g_irc_logonQ) > 0) {
			const char *line = q_peek(g_irc_logonQ, true);
			q_pop(g_irc_logonQ, true);
			q_add(tmpQ, false, line);

			clt_printf(":-fagbnc PRIVMSG %s :%d: %s\r\n", ircbas_mynick(g_irc), i, line);
			i++;
		}

		while(q_size(tmpQ) > 0) {
			const char *line = q_peek(tmpQ, true);
			q_pop(tmpQ, true);
			q_add(g_irc_logonQ, false, line);
		}

		q_dispose(tmpQ);
	} else if (strncmp(line, "DROPREPQ ", 9) == 0) {
		int elem = STRTOI(line+9);
		bool success = false;
		void *tmpQ = q_init();

		int i = 0;
		while(q_size(g_irc_logonQ) > 0) {
			const char *line = q_peek(g_irc_logonQ, true);
			q_pop(g_irc_logonQ, true);
			if (i != elem) {
				q_add(tmpQ, false, line);
			} else {
				free((char*)line);
				success = true;
			}

			i++;
		}

		while(q_size(tmpQ) > 0) {
			const char *line = q_peek(tmpQ, true);
			q_pop(tmpQ, true);
			q_add(g_irc_logonQ, false, line);
		}

		clt_printf(":-fagbnc PRIVMSG %s :%s element %d\r\n", ircbas_mynick(g_irc), success?"dropped":"failed to drop", elem);
	} else if (strncmp(line, "DUMPVERB", 8) == 0) {
		int n = log_count_mods();
		for(int i = 0; i < n; i++) {
			clt_printf(":-fagbnc PRIVMSG %s :mod '%s', verb: %d\r\n", ircbas_mynick(g_irc), log_get_mod(i), log_get_level(log_get_mod(i)));
		}
	} else if (strncmp(line, "SETVERB ", 8) == 0) {
		char *dup = strdup(line+8);
		char *mod = strdup(strtok(dup, " "));
		int verb = STRTOI(strtok(NULL, " "));

		log_set_level(mod, verb);
		free(mod);
		free(dup);
	}
}

static int
clt_printf(const char *fmt, ...)
{
	va_list l;
	va_start(l, fmt);
	char buf[1024];
	vsnprintf(buf, sizeof buf, fmt, l);
	char *p = buf+strlen(buf)-1;
	if (*p != '\n')
		W("line doesntend with \\n:");
	while(p >= buf && (*p == '\n' || *p == '\r'))
		*p-- = '\0';
	D("to client: '%s'", buf);
	va_end(l);
	va_start(l, fmt);
	int r = io_vfprintf(g_clt_sck, fmt, l);
	va_end(l);
	return r;
}		


static void
send_logon_conv()
{
	const char * const* const* lc = ircbas_logonconv(g_irc);
	for(int i = 0; i < 4; i++) {
		char buf[1024];
		join_irc_msg(buf, sizeof buf, lc[i], true);

		clt_printf( "%s\r\n", buf);
	}
}


static void
replay_logon(void)
{
	void *bakQ = q_init();
	void *tmpQ = q_init();
	time_t tstart = time(NULL);
	int lastoff = -1;
	//Q_DUMP(g_irc_logonQ);
	while(q_size(g_irc_logonQ) > 0) {
		char *dup = strdup(q_peek(g_irc_logonQ, true));
		char *orig = strdup(dup);
		char *tok = strtok(dup, " ");
		int off = (int)strtol(tok, NULL, 10);
		tok += strlen(tok)+1;
		if (lastoff == -1)
			lastoff = off;

		if (off == lastoff) {
			q_add(tmpQ, false, tok);
			q_add(bakQ, false, orig);
			q_pop(g_irc_logonQ, true);
		}

		if (off != lastoff || q_size(g_irc_logonQ) == 0) {
			while (tstart + lastoff > time(NULL))
				sleep(1);

			//Q_DUMP(tmpQ);

			while(q_size(tmpQ)) {
				const char *l = q_peek(tmpQ, true);
				const char *snd;
				if (strncmp(l, "$USERMODE$", 10) == 0) {
					char li[1024];
					snprintf(li, sizeof li, "MODE %s%s",
					               ircbas_mynick(g_irc),
					               l+10);
					snd = li;
				} else
					snd = l;
				IWRITE(snd);
				q_pop(tmpQ, true);
			}

			lastoff = off;
		}
		free(orig);
		free(dup);
	}

	//Q_DUMP(bakQ);

	while(q_size(bakQ)) {
		q_add(g_irc_logonQ, false, q_peek(bakQ, true));
		q_pop(bakQ, true);
	}

	//Q_DUMP(g_irc_logonQ);

	q_dispose(bakQ);
	q_dispose(tmpQ);
}


static void
resync(void)
{
	if (strcmp(g_sync_nick, ircbas_mynick(g_irc)) != 0) {
		ucb_switch_base(true);
		ucb_rename_user(g_sync_nick, ircbas_mynick(g_irc));
		ucb_switch_base(false);

		clt_printf( ":%s!fix@me NICK %s\r\n", g_sync_nick,
		                                      ircbas_mynick(g_irc));

		strNcpy(g_sync_nick, ircbas_mynick(g_irc),
		                                        sizeof g_sync_nick);
	}

	char *chandiff = ucb_diff_chans();

	char *tok = strtok(chandiff, ",");
	while(tok) {
		bool added = tok[0] == '+';
		tok++;

		if (added) {
			clt_printf( ":%s!fix@me JOIN %s\r\n",
			                         ircbas_mynick(g_irc), tok);
		} else
			clt_printf( ":%s!fix@me PART %s :syn\r\n",
			                         ircbas_mynick(g_irc), tok);
		tok = strtok(NULL, ",");
	}

	free(chandiff);

	size_t numchans = ucb_count_chans();
	for(size_t i = 0; i < numchans; i++) {
		const char *chan = ucb_next_chan(i==0);
		char *udiff = ucb_diff_users(chan);

		char *tok = strtok(udiff, ",");
		while(tok) {
			bool add = false;
			switch(tok[0]) {
			case '+':
				add = true;
			case '-':
				tok++;
				char c = tok[0];
				if (is_modepfx_sym(c)) {
					tok++;
				} else
					c = 0;

				if (add) {
					clt_printf(":%s!fix@me "
					                      "JOIN %s\r\n",
					                      tok, chan);
					if (c)
						clt_printf(":fix.me MODE "
						      "%s +%c %s\r\n", chan,
						      translate_modepfx(c),
						      tok);
				} else
					clt_printf(":%s!fix@me "
					               "PART %s :synth\r\n",
					               tok, chan);
				break;
			case '*':
				{
				tok++;
				char old = tok[0];
				char new = tok[1];
				tok += 2;
				if (old != ' ')
					clt_printf(":fix.me MODE"
					             " %s -%c %s\r\n", chan,
					             translate_modepfx(old),
					             tok);
				if (new != ' ')
					clt_printf(":fix.me MODE"
					             " %s +%c %s\r\n", chan,
					             translate_modepfx(new),
					             tok);
				}
			}

			tok = strtok(NULL, ",");
		}

		free(udiff);
	}
	ucb_copy();	
	ucb_switch_base(true);
	g_synced = true;
	
	//Q_DUMP(g_irc_missQ);
	while(q_size(g_irc_missQ) > 0) {
		clt_printf("%s\r\n", q_peek(g_irc_missQ, true));
		q_pop(g_irc_missQ, true);
	}

	//Q_DUMP(g_irc_outmissQ);
	while(q_size(g_irc_outmissQ) > 0) {
		q_add(g_irc_sendQ, false, q_peek(g_irc_outmissQ, true));
		q_pop(g_irc_outmissQ, true);
	}
	//Q_DUMP(g_irc_sendQ);

	if (ISTRCASECMP(ircbas_mynick(g_irc), g_prim_nick) != 0) {
		char buf[512];
		snprintf(buf, sizeof buf, "NICK %s\r\n", g_prim_nick);
		q_add(g_irc_sendQ, false, buf);
	}
	//Q_DUMP(g_irc_sendQ);

	N("resynced!");
}


static void
process_sendQ(void)
{
	static time_t lastsend;
	if (g_synced) {
		if (q_size(g_irc_sendQ) > 0 && lastsend + SEND_DELAY <= time(NULL)) {
			const char *msg = q_peek(g_irc_sendQ, true);
			lastsend = time(NULL);
			IWRITE(msg);
			if (strncmp(msg, "QUIT", 4) == 0) {
				N("relayed QUIT, shutting down");
				g_shutdown = time(NULL) + SHUTDOWN_TIME;
			}
			q_pop(g_irc_sendQ, true);
			//Q_DUMP(g_irc_sendQ);
		}
	}
}


static void
on_disconnect(void)
{
	D("on disconnect! synced: %d", g_synced);
	if (g_synced) {
		if (g_needpong) {
			clt_printf(":%1$s PONG %1$s :%1$s\r\n", g_needpong);
			g_last_clt_ping = 0;
			free(g_needpong);
			g_needpong = NULL;
		}

		//Q_DUMP(g_irc_confirmQ);
		while(q_size(g_irc_confirmQ) > 0) {
			q_add(g_irc_outmissQ, false, q_peek(g_irc_confirmQ, true));
			q_pop(g_irc_confirmQ, true);
		}
		//Q_DUMP(g_irc_outmissQ);

		clt_printf(":-fagbnc!fag@bnc NOTICE %s "
		                              ":desynced\r\n", g_sync_nick);
		g_synced = false;
		ucb_switch_base(false);
	} else
		W("disconnected while already desynced");

	ucb_purge();
	q_clear(g_irc_sendQ);
	//Q_DUMP(g_irc_sendQ);
}


static bool
is_modepfx_chr(char c)
{
	return strchr(ircbas_005modepfx(g_irc)[0], c);
}


static bool
is_modepfx_sym(char c)
{
	return strchr(ircbas_005modepfx(g_irc)[1], c);
}


static bool
is_weaker_modepfx_sym(char c1, char c2)
{
	return strchr(ircbas_005modepfx(g_irc)[1], c1)
	                          > strchr(ircbas_005modepfx(g_irc)[1], c2);
}


static char
translate_modepfx(char c)
{
	if (is_modepfx_chr(c)) {
		size_t off = (size_t)(strchr(ircbas_005modepfx(g_irc)[0], c)
		                             - ircbas_005modepfx(g_irc)[0]);
		return ircbas_005modepfx(g_irc)[1][off];
	}
	if (is_modepfx_sym(c)) {
		size_t off = (size_t)(strchr(ircbas_005modepfx(g_irc)[1], c)
		                             - ircbas_005modepfx(g_irc)[1]);
		return ircbas_005modepfx(g_irc)[0][off];
	}
	W("unknown modepfx '%c'", c);
	return 0;
}


static void
join_irc_msg(char *dest, size_t destsz, const char *const *msg,
                                                            bool colonTrail)
{
	snprintf(dest, destsz, ":%s %s", msg[0], msg[1]);

	int j = 2;
	while(j < MAX_IRCARGS && msg[j]) {
		strNcat(dest, " ", destsz);
		//if ((j+1 == MAX_IRCARGS || !msg[j+1]) && strchr(msg[j],' '))
		if (colonTrail && (j+1 == MAX_IRCARGS || !msg[j+1]))
			strNcat(dest, ":", destsz);
		strNcat(dest, msg[j], destsz);
		j++;
	}
}


static bool
dump_irc_msg(char **msg, size_t msg_len, void *tag)
{
	return dump_irc_msg_ex(msg, msg_len, tag, true);
}


static bool
dump_irc_msg_ex(char **msg, size_t msg_len, void *tag, bool colonTrail)
{
	char msgbuf[1024];
	join_irc_msg(msgbuf, sizeof msgbuf, (const char*const*)msg, colonTrail);
	//sndumpmsg(msgbuf, sizeof msgbuf, tag, msg, msg_len);
	D("from irc: %s", msgbuf);
	return true;
}


static void
process_args(int *argc, char ***argv)
{
	char *a0 = (*argv)[0];

	for(int ch; (ch = getopt(*argc, *argv, "vchi:p:s:kl:w:")) != -1;) {
		switch (ch) {
		case 'i':
			strNcpy(g_listen_if, optarg, sizeof g_listen_if);
			break;
		case 'p':
			g_irc_port = STRTOUS(optarg);
			break;
		case 's':
			strNcpy(g_irc_srv, optarg, sizeof g_irc_srv);
			break;
		case 'l':
			g_max_lag = STRTOI(optarg);
			break;
		case 'w':
			g_hup_wait_time = STRTOI(optarg);
			break;
		case 'k':
			g_keep_trying = true;
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


static int
init(int *argc, char ***argv)
{
	log_reinit();

	strNcpy(g_listen_if, DEF_LISTENIF, sizeof g_listen_if);

	process_args(argc, argv);

	if (strlen(g_irc_srv) == 0)
		E("no server given (need -s)");

	char host[256];
	unsigned short port;
	parse_hostspec(host, sizeof host, &port, g_irc_srv);

	g_irc = ircbas_init();
	ircbas_regcb_conread(g_irc, dump_irc_msg, 0);
	ircbas_set_server(g_irc, host, port);

	int listen_sck = addr_bind_socket(g_listen_if, g_irc_port);
	if (listen_sck == -1)
		EE("couldn't bind to '%s:%hu'", g_listen_if, g_irc_port);

	D("bound socket to '%s:%hu'", g_listen_if, g_irc_port);

	if (listen(listen_sck, 128) != 0)
		EE("failed to listen()");

	ucb_init();

	g_irc_sendQ = q_init();
	g_irc_logonQ = q_init();
	g_irc_confirmQ = q_init();
	g_irc_missQ = q_init();
	g_irc_outmissQ = q_init();

	atexit(cleanup);

	return listen_sck;
}


static void
setup_clt(void)
{
	D("setting up client");

	D("accepting a socket");
	g_clt_sck = accept(g_listen_sck, NULL, NULL);

	if (g_clt_sck == -1)
		EE("failed to accept()");
	N("accepted sockfd %d", g_clt_sck);

	char buf[1024];
	int r;
	bool gotnick = false;
	bool gotuser = false;
	bool gotpass = false;
	char *uname = NULL, *fname = NULL, *nick = NULL, *pass = NULL;
	int conflags;
	do {
		r = io_read_line(g_clt_sck, buf, sizeof buf);	
		if (r == -1)
			E("io_read_line failed");
		char *end = buf + strlen(buf) - 1;
		while(end >= buf && (*end == '\n' || *end == '\r'
		                                            || *end == ' '))
			*end-- = '\0';

		D("read line: '%s'", buf);

		if (strncmp(buf, "PASS", 4) == 0) {
			gotpass = true;
			if (pass)
				free(pass);
			pass = strdup(strtok(buf+4, " "));
			D("set pass to '%s'", pass);
		} else if (strncmp(buf, "NICK", 4) == 0) {
			gotnick = true;
			if (nick)
				free(nick);
			nick = strdup(strtok(buf+4, " "));
			if (!g_prim_nick)
				g_prim_nick = strdup(nick);
			D("set nick to '%s'", nick);
		} else if (strncmp(buf, "USER", 4) == 0) {
			gotuser = true;
			if (uname)
				free(uname);
			uname = strdup(strtok(buf+4, " "));
			D("set uname to '%s'", uname);
			conflags = (int)strtol(strtok(NULL, " "), NULL, 10);
			D("set conflags to %d", conflags);
			strtok(NULL, " ");
			if (fname)
				free(fname);
			fname = strdup(strtok(NULL, " ")+1);
			D("set fname to '%s'", fname);
		}
	} while (!gotnick || !gotuser);
	D("got NICK and USER");

	strNcpy(g_sync_nick, nick, sizeof g_sync_nick);
	ircbas_set_nick(g_irc, nick);
	if (gotpass)
		ircbas_set_pass(g_irc, pass);
	ircbas_set_uname(g_irc, uname);
	ircbas_set_fname(g_irc, fname);
	ircbas_set_conflags(g_irc, conflags);
}

static void
cleanup(void)
{
	ucb_cleanup();
	q_dispose(g_irc_logonQ);
	q_dispose(g_irc_sendQ);
	q_dispose(g_irc_confirmQ);
	q_dispose(g_irc_missQ);
	q_dispose(g_irc_outmissQ);
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
	signal(SIGHUP, sighnd);
	g_listen_sck = init(&argc, &argv);
	D("initialized");

	setup_clt();

	life();

	IDISPOSE();
	close(g_listen_sck);
	close(g_clt_sck);

	return EXIT_SUCCESS;
}

void
sighnd(int sig)
{
	switch(sig)
	{
	case SIGHUP:
		D("caught HUP");
		g_hup = true;
		break;
	default:
		W("caught unknown signal %d", sig);
	}
}
