#ifndef COMMON_H
#define COMMON_H

#include <windows.h>

#define BUF_SIZE 4096

void print_error(const char *context);
BOOL write_all(HANDLE h, const char *data, DWORD len);

#endif
