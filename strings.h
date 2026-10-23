#ifndef LOADER_STRINGS_H
#define LOADER_STRINGS_H

#include "types.h"

void* memset(void*, int, u32);
u32 strlen(const char*);
char* strchr(const char*, int);
void* memchr(const void*, int, u32);
char* strcpy(char*, const char*);
char* strcat(char*, const char*);
#endif
