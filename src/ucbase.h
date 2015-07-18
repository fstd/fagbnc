#ifndef UCBASE_H
#define UCBASE_H 1


#ifdef __cplusplus
#include <cstddef>
extern "C" {
#else
#include <stddef.h>
#endif


size_t ucb_count_chans(void);
const char *ucb_next_chan(bool first);

size_t ucb_count_users(const char *chan);
const char *ucb_next_user(const char *chan, bool first);


void ucb_add_chan(const char *chan);
void ucb_drop_chan(const char *chan);
bool ucb_has_chan(const char *chan);
void ucb_clear_chan(const char *chan);
void ucb_set_chan_sync(const char *chan, bool synced);
bool ucb_is_chan_sync(const char *chan);

void ucb_add_user(const char *chan, const char *user);
void ucb_drop_user(const char *chan, const char *user);
void ucb_drop_user_all(const char *user);
bool ucb_has_user(const char *chan, const char *user);
bool ucb_get_user(char *dst, size_t dstsz, const char *ch, const char *us);
void ucb_reprefix_user(const char *chan, const char *name, char c);
void ucb_rename_user(const char *oldname, const char *newname);

char* ucb_diff_chans(void);
char* ucb_diff_users(const char *chan);
void ucb_cleanup(void);
void ucb_purge(void);

void ucb_init(void);
void ucb_set_modepfx(const char *modepfx);
void ucb_set_casemap(int casemap);
void ucb_switch_base(bool primary);
void ucb_copy(void);
void ucb_store_key(const char *chan, const char *key);
const char* ucb_retrieve_key(const char *chan);

void ucb_dump(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* UCBASE_H */
