#if HAVE_CONFIG_H
# include <config.h>
#endif

#include "ucbase.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <map>
#include <set>
#include <string>

extern "C" {
#include <libsrsirc/irc_util.h>
#include <libsrslog/log.h>
}

class usercmp
{
public:
	static int casemap;
	static char *modepfx;
	bool operator()(std::string const& s1, std::string const& s2) const
	{
		std::string n1 = std::string(s1, strchr(modepfx, s1[0])?1:0, s1.length());
		std::string n2 = std::string(s2, strchr(modepfx, s2[0])?1:0, s2.length());
		int i = istrcasecmp(n1.c_str(), n2.c_str(), casemap);
		D("comparing '%s' vs '%s': %d", s1.c_str(), s2.c_str(), i);
		return i < 0;
	}
};

class istringcmp
{
public:
	static int casemap;
	bool operator()(std::string const& s1, std::string const& s2) const
	{
		int i = istrcasecmp(s1.c_str(), s2.c_str(), casemap);
		D("comparing '%s' vs '%s': %d", s1.c_str(), s2.c_str(), i);
		return i < 0;
	}
};

int istringcmp::casemap = 0;
int usercmp::casemap = 0;
char *usercmp::modepfx = NULL;

typedef std::map<std::string, std::string> keymap_t;
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
	
	ucb_set_chan_sync(chan, false);
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
ucb_rename_user(const char *oldname, const char *newname)
{
	for(basemap_t::iterator it = s_base->begin(); it != s_base->end(); it++) {
		if (ucb_has_user(it->first.c_str(), oldname)) {
			ucb_drop_user(it->first.c_str(), oldname);
			ucb_add_user(it->first.c_str(), newname);
		}
	}
}


extern "C" void
ucb_add_user(const char *chan, const char *user)
{
	if (!s_base->count(std::string(chan))) {
		W("no such chan: '%s'", chan);
		return;
	}
	(*s_base)[std::string(chan)].insert(std::string(user));
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

	uset.erase(std::string(user));
}

extern "C" void
ucb_drop_user_all(const char *user)
{
	for(basemap_t::iterator it = s_base->begin(); it != s_base->end(); it++) {
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
ucb_get_user(char *dest, size_t destsz, const char *chan, const char *user)
{
	if (!s_base->count(std::string(chan))) {
		W("no such chan: '%s'", chan);
		return false;
	}
	userset_t &uset = (*s_base)[std::string(chan)];
	if (!uset.count(std::string(user))) {
		W("no such user '%s' in chan '%s'", user, chan);
		return false;
	}

	userset_t::iterator it = uset.find(std::string(user));
	strncpy(dest, it->c_str(), destsz);
	dest[destsz-1] = '\0';
	return true;
}

extern "C" void
ucb_dump()
{
	N("ucb dump");
	for(basemap_t::iterator it = s_base->begin(); it != s_base->end(); it++) {
		N("chan: %s", it->first.c_str());
		userset_t &uset = it->second;
		for(userset_t::iterator uit = uset.begin(); uit != uset.end(); uit++) {
			N("\tuser: %s", uit->c_str());
		}
	}
	N("end of dump");
}

extern "C" void
ucb_init(int casemap, const char *modepfx)
{
	istringcmp::casemap = casemap;
	usercmp::casemap = casemap;
	usercmp::modepfx = strdup(modepfx);
	D("ucb initialized with casemap: %d, modepfx: '%s'", casemap, modepfx);
	s_primbase = s_base = new basemap_t;
	s_primsyncmap = s_syncmap = new syncmap_t;
	s_secbase = new basemap_t;
	s_secsyncmap = new syncmap_t;
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
	if (!s_keymap.count(std::string(chan)))
		return NULL;
	return s_keymap[std::string(chan)].c_str();
}
