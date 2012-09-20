#ifndef QWRAP_H
#define QWRAP_H 1


#ifdef __cplusplus
#include <cstddef>
extern "C" {
#else
#include <stddef.h>
#endif

void *q_init();
void q_add(void *q, bool head, const char *data);
const char *q_peek(void *q, bool head);
const char *q_pop(void *q, bool head);
size_t q_size(void *q);
void q_clear(void *q);
void q_dispose(void *q);
void q_dump(void *q, const char *label);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* QWRAP_H */
