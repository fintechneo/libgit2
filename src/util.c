#define GIT__NO_HIDE_MALLOC
#include "common.h"
#include <stdarg.h>
#include <stdio.h>

void *git__malloc(size_t n)
{
	void *r = malloc(n);
	if (!r)
		return git_ptr_error(GIT_ENOMEM);
	return r;
}

void *git__calloc(size_t a, size_t b)
{
	void *r = calloc(a, b);
	if (!r)
		return git_ptr_error(GIT_ENOMEM);
	return r;
}

char *git__strdup(const char *s)
{
	char *r = strdup(s);
	if (!s)
		return git_ptr_error(GIT_ENOMEM);
	return r;
}

int git__fmt(char *buf, size_t buf_sz, const char *fmt, ...)
{
	va_list va;
	int r;

	va_start(va, fmt);
	r = vsnprintf(buf, buf_sz, fmt, va);
	va_end(va);
	if (r < 0 || r >= buf_sz)
		return GIT_ERROR;
	return r;
}

int git__prefixcmp(const char *str, const char *prefix)
{
	for (;;) {
		char p = *(prefix++), s;
		if (!p)
			return 0;
		if ((s = *(str++)) != p)
			return s - p;
	}
}

int git__suffixcmp(const char *str, const char *suffix)
{
	size_t a = strlen(str);
	size_t b = strlen(suffix);
	if (a < b)
		return -1;
	return strcmp(str + (a - b), suffix);
}
