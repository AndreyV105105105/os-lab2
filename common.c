#include <stdio.h>
#include "common.h"

/* Печатает текст ошибки по коду GetLastError() */
void print_error(const char *context)
{
    DWORD code = GetLastError();

    wchar_t wmsg[512];
    DWORD wlen = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                NULL, code, 0, wmsg, 512, NULL);

    /* перекодируем в кодировку консоли */
    char msg[1024] = "unknown error";
    if (wlen > 0) {
        UINT cp = GetConsoleOutputCP();
        if (cp == 0) {
            cp = CP_ACP;
        }
        int n = WideCharToMultiByte(cp, 0, wmsg, (int)wlen, msg, (int)sizeof(msg) - 1, NULL, NULL);
        if (n > 0) {
            msg[n] = '\0';
            while (n > 0 && (msg[n - 1] == '\r' || msg[n - 1] == '\n' || msg[n - 1] == ' ')) {
                msg[--n] = '\0';
            }
        }
    }

    fprintf(stderr, "[PID %lu] ERROR: %s: %s (code %lu)\n",
            (unsigned long)GetCurrentProcessId(), context, msg, (unsigned long)code);
}

/* WriteFile может записать не всё сразу, поэтому пишем в цикле */
BOOL write_all(HANDLE h, const char *data, DWORD len)
{
    while (len > 0) {
        DWORD written = 0;
        if (!WriteFile(h, data, len, &written, NULL)) {
            return FALSE;
        }
        if (written == 0) {
            SetLastError(ERROR_WRITE_FAULT);
            return FALSE;
        }
        data += written;
        len -= written;
    }
    return TRUE;
}
