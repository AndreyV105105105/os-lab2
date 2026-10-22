#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include "common.h"

#define TOKEN_MAX 64

typedef struct {
    HANDLE h;
    char buf[BUF_SIZE];
    DWORD pos;
    DWORD len;
} Reader;

typedef struct {
    float sum;
    int count;
    int error;
    char token[TOKEN_MAX + 1];
    int tlen;
    int too_long;
} LineState;

/* одним вызовом WriteFile, чтобы строка не смешалась с выводом родителя */
static void log_stderr(const char *fmt, ...)
{
    char msg[256];
    va_list ap;
    DWORD written;

    va_start(ap, fmt);
    int len = vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (len < 0) {
        return;
    }
    if (len >= (int)sizeof(msg)) {
        len = (int)sizeof(msg) - 1;
    }
    if (!WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, (DWORD)len, &written, NULL)) {
        /* stderr недоступен - сообщить об ошибке некуда */
    }
}

/* 1 - символ прочитан, 0 - конец файла, -1 - ошибка */
static int reader_getc(Reader *r, char *c)
{
    if (r->pos == r->len) {
        if (!ReadFile(r->h, r->buf, sizeof(r->buf), &r->len, NULL)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                return 0;
            }
            return -1;
        }
        r->pos = 0;
        if (r->len == 0) {
            return 0;
        }
    }
    *c = r->buf[r->pos++];
    return 1;
}

static void line_reset(LineState *st)
{
    st->sum = 0.0f;
    st->count = 0;
    st->error = 0;
    st->tlen = 0;
    st->too_long = 0;
}

static void finish_token(LineState *st, unsigned long line_no)
{
    char *end;
    float value;

    if (st->tlen == 0 && !st->too_long) {
        return;
    }
    st->token[st->tlen] = '\0';

    if (st->too_long) {
        log_stderr("[child PID %lu] line %lu: number is too long\n",
                   (unsigned long)GetCurrentProcessId(), line_no);
        st->error = 1;
    } else {
        value = strtof(st->token, &end);
        if (end == st->token || *end != '\0' || !isfinite(value)) {
            log_stderr("[child PID %lu] line %lu: invalid number '%s'\n",
                       (unsigned long)GetCurrentProcessId(), line_no, st->token);
            st->error = 1;
        } else {
            st->sum += value;
            st->count++;
        }
    }
    st->tlen = 0;
    st->too_long = 0;
}

static BOOL finish_line(LineState *st, HANDLE out, unsigned long line_no)
{
    char msg[256];
    int len;

    if (st->count == 0 && !st->error) {
        line_reset(st);
        return TRUE;  /* пустая строка */
    }

    if (st->error) {
        len = snprintf(msg, sizeof(msg), "[child PID %lu] line %lu: ERROR (invalid input)\n",
                       (unsigned long)GetCurrentProcessId(), line_no);
    } else if (!isfinite(st->sum)) {
        len = snprintf(msg, sizeof(msg), "[child PID %lu] line %lu: ERROR (float overflow)\n",
                       (unsigned long)GetCurrentProcessId(), line_no);
    } else {
        len = snprintf(msg, sizeof(msg), "[child PID %lu] line %lu: sum = %g\n",
                       (unsigned long)GetCurrentProcessId(), line_no, (double)st->sum);
    }
    line_reset(st);

    if (len < 0) {
        log_stderr("[child] ERROR: snprintf failed\n");
        return FALSE;
    }
    if (!write_all(out, msg, (DWORD)len)) {
        print_error("WriteFile(stdout)");
        return FALSE;
    }
    return TRUE;
}

int main(void)
{
    static Reader in;
    LineState st;
    HANDLE hOut;
    unsigned long line_no = 1;
    char c;
    int r;

    log_stderr("[child PID %lu] started\n", (unsigned long)GetCurrentProcessId());

    /* stdin и stdout, которые задал родитель (файл и pipe1) */
    in.h = GetStdHandle(STD_INPUT_HANDLE);
    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (in.h == INVALID_HANDLE_VALUE || in.h == NULL) {
        print_error("GetStdHandle(STD_INPUT_HANDLE)");
        return EXIT_FAILURE;
    }
    if (hOut == INVALID_HANDLE_VALUE || hOut == NULL) {
        print_error("GetStdHandle(STD_OUTPUT_HANDLE)");
        return EXIT_FAILURE;
    }
    in.pos = 0;
    in.len = 0;
    line_reset(&st);

    while ((r = reader_getc(&in, &c)) == 1) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            finish_token(&st, line_no);
            if (c == '\n') {
                if (!finish_line(&st, hOut, line_no)) {
                    return EXIT_FAILURE;
                }
                line_no++;
            }
        } else if (st.tlen < TOKEN_MAX) {
            st.token[st.tlen++] = c;
        } else {
            st.too_long = 1;
        }
    }
    if (r < 0) {
        print_error("ReadFile(stdin)");
        return EXIT_FAILURE;
    }

    /* последняя строка без '\n' */
    finish_token(&st, line_no);
    if (!finish_line(&st, hOut, line_no)) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
