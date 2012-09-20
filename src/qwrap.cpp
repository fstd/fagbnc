#if HAVE_CONFIG_H
# include <config.h>
#endif

#include "qwrap.h"

#include <string>
#include <deque>

extern "C" {
#include <libsrsirc/irc_util.h>
#include <libsrslog/log.h>
}

typedef std::deque<std::string> queue_t;

extern "C" void*
q_init()
{
	queue_t *q = new queue_t;
	return q;
}

extern "C" void 
q_add(void *q, bool head, const char *data)
{
	queue_t *qu = static_cast<queue_t*>(q);
	if (head)
		qu->push_front(std::string(data));
	else
		qu->push_back(std::string(data));
}

extern "C" const char*
q_peek(void *q, bool head)
{
	queue_t *qu = static_cast<queue_t*>(q);
	if (qu->empty())
		return NULL;
	return head ? qu->front().c_str() : qu->back().c_str();
}

extern "C" const char*
q_pop(void *q, bool head)
{
	queue_t *qu = static_cast<queue_t*>(q);
	if (qu->empty())
		return NULL;
	const char *c = q_peek(q, head);
	if (head) 
		qu->pop_front();
	else
		qu->pop_back();
	return c;
}

extern "C" size_t
q_size(void *q)
{
	queue_t *qu = static_cast<queue_t*>(q);
	return qu->size();
}

extern "C" void 
q_clear(void *q)
{
	queue_t *qu = static_cast<queue_t*>(q);
	qu->clear();
}

extern "C" void 
q_dispose(void *q)
{
	queue_t *qu = static_cast<queue_t*>(q);
	qu->clear();
	delete qu;
}

extern "C" void 
q_dump(void *q, const char *label)
{
	D("Q dump '%s'", label);
	size_t n = 0;
	queue_t *qu = static_cast<queue_t*>(q);
	for(queue_t::iterator it = qu->begin(); it != qu->end(); it++, n++)
		D("elem: '%s'", it->c_str());
	D("end of Q dump '%s' (%zu elements)", label, n);
}

