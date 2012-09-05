#ifndef UCBASE_H
#define UCBASE_H 1


#ifdef __cplusplus
#include <cstddef>
extern "C" {
#else
#include <stddef.h>
#endif


size_t ucb_count_chans();
const char *ucb_next_chan(bool first);

size_t ucb_count_users(const char *chan);
const char *ucb_next_user(const char *chan, bool first);


void ucb_add_chan(const char *chan);
void ucb_drop_chan(const char *chan);
bool ucb_has_chan(const char *chan);

void ucb_add_user(const char *chan, const char *user);
void ucb_drop_user(const char *chan, const char *user);
void ucb_drop_user_all(const char *user);
bool ucb_has_user(const char *chan, const char *user);

void ucb_init(int casemap);

void ucb_dump();

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* UCBASE_H */
