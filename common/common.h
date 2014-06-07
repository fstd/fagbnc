#ifndef SRS_COMMON_H
#define SRS_COMMON_H 1

#include <stddef.h>

#define COUNTOF(ARR) (sizeof (ARR) / sizeof (ARR)[0])

void strNcat(char *dest, const char *src, size_t destsz);
char* strNcpy(char *dst, const char *src, size_t len);

#endif /* SRS_COMMON_H */
