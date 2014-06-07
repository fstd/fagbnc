#if HAVE_CONFIG_H
# include <config.h>
#endif

#include "ucbase.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <vector>
#include <map>
#include <set>
#include <string>

extern "C" {
#include <common.h>

#include <libsrsirc/util.h>
#include "intlog.h"
}

class usercmp
{
public:
	static int casemap;
	static char *modepfx;
	bool operator()(std::string const& s1, std::string const& s2) const
	{
		std::string n1 = std::string(s1, strchr(modepfx, s1[0])?1:0,
		                                               s1.length());
		std::string n2 = std::string(s2, strchr(modepfx, s2[0])?1:0,
		                                               s2.length());
		int i = ut_istrcmp(n1.c_str(), n2.c_str(), casemap);
		return i < 0;
	}
};

class istringcmp
{
public:
	static int casemap;
	bool operator()(std::string const& s1, std::string const& s2) const
	{
		int i = ut_istrcmp(s1.c_str(), s2.c_str(), casemap);
		return i < 0;
	}
};

int istringcmp::casemap = 0;
int usercmp::casemap = 0;
char *usercmp::modepfx = NULL;

typedef std::map<std::string, std::string, istringcmp> keymap_t;
typedef std::set<std::string, usercmp> userset_t;
typedef std::map<std::string, userset_t, istringcmp> basemap_t;
typedef std::map<std::string, bool, istringcmp> syncmap_t;

static basemap_t *s_base;
static syncmap_t *s_syncmap;

static basemap_t *s_primbase;
static syncmap_t *s_primsyncmap;

static basemap_t *s_secbase;
static syncmap_t *s_secsyncmap;

static keymap_t s_keymap;

static bool is_modepfx_sym(char c);

extern "C" size_t
ucb_count_chans()
{
	return s_base->size();
}

extern "C" const char*
ucb_next_chan(bool first)
{
	static basemap_t::iterator it;
	if (first)
		it = s_base->begin();
	
	if (it == s_base->end())
		return NULL;
	
	return it++->first.c_str();
}

extern "C" size_t
ucb_count_users(const char *chan)
{
	if (!s_base->count(std::string(chan))) {
		W("no such chan: '%s'", chan);
		return 0;
	}
	userset_t &uset = (*s_base)[std::string(chan)];
	return uset.size();
}

extern "C" const char*
ucb_next_user(const char *chan, bool first)
{
	static userset_t::iterator it;
	if (first) {
		if (!s_base->count(std::string(chan))) {
			W("no such chan: '%s'", chan);
			return NULL;
		}
		it = (*s_base)[std::string(chan)].begin();
	}

	if (it == (*s_base)[std::string(chan)].end())
		return NULL;
	
	return it++->c_str();
}

extern "C" void
ucb_add_chan(const char *chan)
{
	if (!s_base->count(std::string(chan)))
		(*s_base)[std::string(chan)] = userset_t();
	else
		W("chan already known: '%s'", chan);
}

extern "C" void
ucb_drop_chan(const char *chan)
{
	if (!s_base->count(std::string(chan))) {
		W("no such chan: '%s'", chan);
		return;
	}
	userset_t &uset = (*s_base)[std::string(chan)];
	uset.clear();
	s_base->erase(std::string(chan));
	s_syncmap->erase(std::string(chan));
}

extern "C" bool
ucb_has_chan(const char *chan)
{
	return s_base->count(std::string(chan));
}

extern "C" void
ucb_clear_chan(const char *chan)
{
	if (!s_base->count(std::string(chan))) {
		W("no such chan: '%s'", chan);
		return;
	}
	userset_t &uset = (*s_base)[std::string(chan)];
	uset.clear();
}

extern "C" void
ucb_set_chan_sync(const char *chan, bool synced)
{
	(*s_syncmap)[std::string(chan)] = synced;
}

extern "C" bool
ucb_is_chan_sync(const char *chan)
{
	if (!s_syncmap->count(std::string(chan))) {
		W("no such chan: '%s'", chan);
		return false;
	}
	return (*s_syncmap)[std::string(chan)];
}

extern "C" void
ucb_reprefix_user(const char *chan, const char *name, char c)
{
	if (ucb_has_user(chan, name)) {
		size_t nsz = strlen(name)+2;
		char *nname = (char*)malloc(nsz);//modepfx and \0
		nname[0] = c;
		strNcpy(nname+1, name, nsz-1);

		//D("reprefixing user in '%s': '%s' to '%c'", chan, name, c);

		ucb_drop_user(chan, name);
		ucb_add_user(chan, c==' '?nname+1:nname);
		free(nname);
	}
}

extern "C" void
ucb_rename_user(const char *name, const char *newname)
{
	size_t nsz = strlen(newname)+2;
	char *nname = (char*)malloc(nsz);//modepfx and \0
	strNcpy(nname+1, newname, nsz-1);
	size_t osz = strlen(name)+2;
	char *oname = (char*)malloc(osz);//modepfx and \0
	//D("renaming user '%s' to '%s'", name, newname);

	for(basemap_t::iterator it = s_base->begin(); it != s_base->end();
	                                                             it++) {
		if (ucb_has_user(it->first.c_str(), name)) {
			ucb_get_user(oname, osz, it->first.c_str(), name);
			char c = oname[0];
			ucb_drop_user(it->first.c_str(), name);
			if (is_modepfx_sym(c)) {
				nname[0] = c;
				ucb_add_user(it->first.c_str(), nname);
			} else
				ucb_add_user(it->first.c_str(), nname+1);
		}
	}
	free(nname);
	free(oname);
}


extern "C" void
ucb_add_user(const char *chan, const char *user)
{
	if (!s_base->count(std::string(chan))) {
		W("no such chan: '%s'", chan);
		return;
	}
	(*s_base)[std::string(chan)].insert(std::string(user));
	//D("Adding user to chan '%s': '%s'", chan, user);
}

extern "C" void
ucb_drop_user(const char *chan, const char *user)
{
	if (!s_base->count(std::string(chan))) {
		W("no such chan: '%s'", chan);
		return;
	}
	userset_t &uset = (*s_base)[std::string(chan)];
	if (!uset.count(std::string(user))) {
		W("no such user '%s' in chan '%s'", user, chan);
		return;
	}
	//D("Dropping user from chan '%s': '%s'", chan, user);
	uset.erase(std::string(user));
}

extern "C" void
ucb_drop_user_all(const char *user)
{
	for(basemap_t::iterator it = s_base->begin(); it != s_base->end();
	                                                             it++) {
		if (ucb_has_user(it->first.c_str(), user))
			ucb_drop_user(it->first.c_str(), user);
	}
}

extern "C" bool
ucb_has_user(const char *chan, const char *user)
{
	return s_base->count(std::string(chan))
	           && (*s_base)[std::string(chan)].count(std::string(user));
}

extern "C" bool
ucb_get_user(char *dst, size_t dstsz, const char *ch, const char *us)
{
	if (!s_base->count(std::string(ch))) {
		W("no such chan: '%s'", ch);
		return false;
	}
	userset_t &uset = (*s_base)[std::string(ch)];
	if (!uset.count(std::string(us))) {
		W("no such user '%s' in chan '%s'", us, ch);
		return false;
	}

	userset_t::iterator it = uset.find(std::string(us));
	strncpy(dst, it->c_str(), dstsz);
	dst[dstsz-1] = '\0';
	return true;
}

extern "C" void
ucb_cleanup()
{
	ucb_switch_base(false);
	ucb_purge();
	ucb_switch_base(true);
	ucb_purge();
	free(usercmp::modepfx);
	s_primsyncmap->clear();
	s_secsyncmap->clear();
}

extern "C" void
ucb_purge()
{
	for(basemap_t::iterator it = s_base->begin(); it != s_base->end();
	                                                             it++) {
		(it->second).clear();
	}
	s_base->clear();
}

extern "C" void
ucb_dump()
{
	N("ucb dump");
	for(basemap_t::iterator it = s_base->begin(); it != s_base->end();
	                                                             it++) {
		N("chan: %s", it->first.c_str());
		userset_t &uset = it->second;
		for(userset_t::iterator ut = uset.begin(); ut != uset.end();
		                                                     ut++) {
			N("\tuser: %s", ut->c_str());
		}
	}
	N("end of dump");
}

extern "C" void
ucb_init()
{
	istringcmp::casemap = CMAP_RFC1459;
	usercmp::casemap = CMAP_RFC1459;
	usercmp::modepfx = strdup("@+");
	s_primbase = s_base = new basemap_t;
	s_primsyncmap = s_syncmap = new syncmap_t;
	s_secbase = new basemap_t;
	s_secsyncmap = new syncmap_t;
}

extern "C" void
ucb_set_modepfx(const char *modepfx)
{
	free(usercmp::modepfx);
	usercmp::modepfx = strdup(modepfx);
}

extern "C" void
ucb_set_casemap(int casemap)
{
	istringcmp::casemap = casemap;
	usercmp::casemap = casemap;
}

extern "C" void
ucb_copy()
{
	*s_primbase = *s_secbase;
	*s_primsyncmap = *s_secsyncmap;
}

extern "C" void
ucb_switch_base(bool primary)
{
	if (primary) {
		s_base = s_primbase;
		s_syncmap = s_primsyncmap;
	} else {
		s_base = s_secbase;
		s_syncmap = s_secsyncmap;
	}
}

extern "C" void
ucb_store_key(const char *chan, const char *key)
{
	s_keymap[std::string(chan)] = std::string(key);
}

extern "C" const char*
ucb_retrieve_key(const char *chan)
{
	if (!s_keymap.count(std::string(chan))) {
		return NULL;
	}
	return s_keymap[std::string(chan)].c_str();
}

extern "C" char*
ucb_diff_chans()
{
	size_t lrem = 0, ladd = 0;
	std::vector<std::string> chrem;
	std::vector<std::string> chadd;
	for(basemap_t::iterator it = s_primbase->begin();
	                                    it != s_primbase->end(); it++) {
		if (!s_secbase->count(it->first)) {
			chrem.push_back(it->first);
			lrem += 2 + it->first.length();
		}
	}
	for(basemap_t::iterator it = s_secbase->begin();
	                                     it != s_secbase->end(); it++) {
		if (!s_primbase->count(it->first)) {
			chadd.push_back(it->first);
			ladd += 2 + it->first.length();
		}
	}

	char *chmd = (char*)malloc(lrem+ladd+1);
	chmd[0] = '\0';
	char *end = chmd;
	bool first = true;
	for(std::vector<std::string>::const_iterator it = chrem.begin();
			it != chrem.end(); it++) {
		if (!first) {
			*end++ = ',';
			*end = '\0';
		}
		first = false;
		*end++ = '-';
		*end = '\0';
		strcat(end, it->c_str());
		end+= it->length();
	}

	for(std::vector<std::string>::const_iterator it = chadd.begin();
			it != chadd.end(); it++) {
		if (!first) {
			*end++ = ',';
			*end = '\0';
		}
		first = false;
		*end++ = '+';
		*end = '\0';
		strcat(end, it->c_str());
		end+= it->length();
	}

	//N("chdiff is '%s'", chmd);
	return chmd;
}

extern "C" char*
ucb_diff_users(const char *chan)
{
	size_t lrem = 0, ladd = 0, lmod = 0;
	std::vector<std::string> urem;
	std::vector<std::string> uadd;
	std::vector<std::string> umod;

	if (!s_primbase->count(std::string(chan))) {
		W("no such chan in prim base: '%s'", chan);
		return NULL;
	}
	userset_t &primuset = (*s_primbase)[std::string(chan)];
	if (!s_secbase->count(std::string(chan))) {
		W("no such chan in sec base: '%s'", chan);
		return NULL;
	}
	userset_t &secuset = (*s_secbase)[std::string(chan)];

	for(userset_t::iterator it = primuset.begin(); it != primuset.end();
	                                                             it++) {
		if (!secuset.count(*it)) {
			urem.push_back(*it);
			lrem += 2 + it->length();
		} else {
			char c = (*it)[0];
			char n = (*secuset.find(*it))[0];
			if (!is_modepfx_sym(c))
				c = ' ';
			if (!is_modepfx_sym(n))
				n = ' ';

			if (c != n) {
				char buf[128];
				snprintf(buf, sizeof buf, "*%c%c%s", c, n,
				                    c==' '?it->c_str()
				                          :(it->c_str()+1));
				umod.push_back(std::string(buf));
				lmod += 4 + strlen(buf);
			}
		}
	}

	for(userset_t::iterator it = secuset.begin(); it != secuset.end();
	                                                             it++) {
		if (!primuset.count(*it)) {
			uadd.push_back(*it);
			ladd += 2 + it->length();
		}
	}

	char *umd = (char*)malloc(lrem+ladd+lmod+1);
	umd[0] = '\0';
	char *end = umd;
	bool first = true;
	for(std::vector<std::string>::const_iterator it = urem.begin();
			it != urem.end(); it++) {
		if (!first) {
			*end++ = ',';
			*end = '\0';
		}
		first = false;
		*end++ = '-';
		*end = '\0';
		strcat(end, it->c_str());
		end+= it->length();
	}

	for(std::vector<std::string>::const_iterator it = uadd.begin();
			it != uadd.end(); it++) {
		if (!first) {
			*end++ = ',';
			*end = '\0';
		}
		first = false;
		*end++ = '+';
		*end = '\0';
		strcat(end, it->c_str());
		end+= it->length();
	}

	for(std::vector<std::string>::const_iterator it = umod.begin();
			it != umod.end(); it++) {
		if (!first) {
			*end++ = ',';
			*end = '\0';
		}
		first = false;
		strcat(end, it->c_str());
		end+= it->length();
	}

	//N("udiff for '%s' is '%s'", chan, umd);
	return umd;

}

static bool
is_modepfx_sym(char c)
{
	return strchr(usercmp::modepfx, c);
}
