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
#include <libsrslog/log.h>
}

static int s_casemap;
typedef std::set<std::string> userset_t;
typedef std::map<std::string, userset_t> basemap_t;
static basemap_t s_base;

extern "C" size_t
ucb_count_chans()
{
	return s_base.size();
}

extern "C" const char*
ucb_next_chan(bool first)
{
	static basemap_t::iterator it;
	if (first)
		it = s_base.begin();
	
	if (it == s_base.end())
		return NULL;
	
	return it++->first.c_str();
}

extern "C" size_t
ucb_count_users(const char *chan)
{
	if (!s_base.count(std::string(chan))) {
		W("no such chan: '%s'", chan);
		return 0;
	}
	userset_t &uset = s_base[std::string(chan)];
	return uset.size();
}

extern "C" const char*
ucb_next_user(const char *chan, bool first)
{
	static userset_t::iterator it;
	if (first) {
		if (!s_base.count(std::string(chan))) {
			W("no such chan: '%s'", chan);
			return NULL;
		}
		it = s_base[std::string(chan)].begin();
	}

	if (it == s_base[std::string(chan)].end())
		return NULL;
	
	return it++->c_str();
}

extern "C" void
ucb_add_chan(const char *chan)
{
	if (!s_base.count(std::string(chan)))
		s_base[std::string(chan)] = userset_t();
	else
		W("chan already known: '%s'", chan);
}

extern "C" void
ucb_drop_chan(const char *chan)
{
	if (!s_base.count(std::string(chan))) {
		W("no such chan: '%s'", chan);
		return;
	}
	userset_t &uset = s_base[std::string(chan)];
	uset.clear();
	s_base.erase(std::string(chan));
}

extern "C" bool
ucb_has_chan(const char *chan)
{
	return s_base.count(std::string(chan));
}

extern "C" void
ucb_add_user(const char *chan, const char *user)
{
	if (!s_base.count(std::string(chan))) {
		W("no such chan: '%s'", chan);
		return;
	}
	s_base[std::string(chan)].insert(std::string(user));
}

extern "C" void
ucb_drop_user(const char *chan, const char *user)
{
	if (!s_base.count(std::string(chan))) {
		W("no such chan: '%s'", chan);
		return;
	}
	userset_t &uset = s_base[std::string(chan)];
	if (!uset.count(std::string(user))) {
		W("no such user '%s' in chan '%s'", user, chan);
		return;
	}

	uset.erase(std::string(user));
}

extern "C" void
ucb_drop_user_all(const char *user)
{
	for(basemap_t::iterator it = s_base.begin(); it != s_base.end(); it++) {
		if (ucb_has_user(it->first.c_str(), user))
			ucb_drop_user(it->first.c_str(), user);
	}
}

extern "C" bool
ucb_has_user(const char *chan, const char *user)
{
	return s_base.count(std::string(chan))
	             && s_base[std::string(chan)].count(std::string(user));
}

extern "C" void
ucb_set_casemap(int casemap)
{
	s_casemap = casemap;
}

extern "C" void
ucb_dump()
{
	N("ucb dump");
	for(basemap_t::iterator it = s_base.begin(); it != s_base.end(); it++) {
		N("chan: %s", it->first.c_str());
		userset_t &uset = it->second;
		for(userset_t::iterator uit = uset.begin(); uit != uset.end(); uit++) {
			N("\tuser: %s", uit->c_str());
		}
	}
	N("end of dump");
}

