#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <common.h>

#include <string.h>

void
strNcat(char *dest, const char *src, size_t destsz)
{
	size_t len = strlen(dest);
	if (len + 1 >= destsz)
		return;
	size_t rem = destsz - (len + 1);

	char *ptr = dest + len;
	while(rem-- && *src) {
		*ptr++ = *src++;
	}
	*ptr = '\0';
}

char*
strNcpy(char *dst, const char *src, size_t len)
{
	char *r = strncpy(dst, src, len);
	dst[len-1] = '\0';
	return r;
}
