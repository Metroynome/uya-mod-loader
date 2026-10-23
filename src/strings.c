#include "strings.h"

void* memset(void* p, int c, u32 n)
{
	u8* d = p;
	while (n--)
		*d++ = c;
	return p;
}

u32 strlen(const char* s)
{
	const char* p = s;
	while (*p)
		++p;
	return p - s;
}

char* strchr(const char* s, int c)
{
	do {
		if (*s == c)
			return (char*)s;
	} while (*s++);
	return 0;
}

void* memchr(const void* p, int c, u32 n)
{
	const u8* s = p;
	while (n--) {
		if (*s == (u8)c)
			return (void*)s;
		++s;
	}
	return 0;
}

char* strcpy(char* d, const char* s)
{
	char* p = d;
	while ((*d++ = *s++)) {
	}
	return p;
}

char* strcat(char* d, const char* s)
{
	strcpy(d + strlen(d), s);
	return d;
}
