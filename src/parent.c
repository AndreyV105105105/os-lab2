#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

#define CHILD_EXE "child.exe"

static int read_file_name(char *buf, size_t size)
{
    printf("[parent PID %lu] Enter file name: ", (unsigned long)GetCurrentProcessId());
    fflush(stdout);

    if (fgets(buf, (int)size, stdin) == NULL) {
        fprintf(stderr, "[parent] ERROR: no file name (EOF)\n");
        return -1;
    }
    buf[strcspn(buf, "\r\n")] = '\0';
    if (buf[0] == '\0') {
        fprintf(stderr, "[parent] ERROR: empty file name\n");
        return -1;
    }
    return 0;
}

int main(void)
{
    char file_name[MAX_PATH];
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hPipeRead = NULL;
    HANDLE hPipeWrite = NULL;
    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    SECURITY_ATTRIBUTES sa;
    char cmd_line[] = CHILD_EXE;
    char buf[BUF_SIZE];
    HANDLE hStdout;
    DWORD child_code = 0;
    int result = EXIT_FAILURE;

    ZeroMemory(&pi, sizeof(pi));

    printf("[parent PID %lu] started\n", (unsigned long)GetCurrentProcessId());

    if (read_file_name(file_name, sizeof(file_name)) != 0) {
        return EXIT_FAILURE;
    }

    /* дескрипторы с этими атрибутами наследуются дочерним процессом */
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    hFile = CreateFileA(file_name, GENERIC_READ, FILE_SHARE_READ, &sa,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        print_error("CreateFile");
        goto cleanup;
    }

    if (!CreatePipe(&hPipeRead, &hPipeWrite, &sa, 0)) {
        print_error("CreatePipe");
        goto cleanup;
    }
    /* конец для чтения остаётся только у родителя */
    if (!SetHandleInformation(hPipeRead, HANDLE_FLAG_INHERIT, 0)) {
        print_error("SetHandleInformation");
        goto cleanup;
    }

    /* stdin ребёнка - файл, stdout - pipe1 */
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hFile;
    si.hStdOutput = hPipeWrite;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    if (!CreateProcessA(NULL, cmd_line, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        print_error("CreateProcess(" CHILD_EXE ")");
        goto cleanup;
    }
    printf("[parent PID %lu] created child PID %lu\n",
           (unsigned long)GetCurrentProcessId(), (unsigned long)pi.dwProcessId);
    fflush(stdout);

    CloseHandle(pi.hThread);
    pi.hThread = NULL;

    /* закрываем свои копии, иначе ReadFile не дождётся конца канала */
    CloseHandle(hPipeWrite);
    hPipeWrite = NULL;
    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;

    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdout == INVALID_HANDLE_VALUE) {
        print_error("GetStdHandle(STD_OUTPUT_HANDLE)");
        goto cleanup;
    }
    for (;;) {
        DWORD n = 0;
        if (!ReadFile(hPipeRead, buf, sizeof(buf), &n, NULL)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                break;  /* ребёнок закрыл канал */
            }
            print_error("ReadFile(pipe1)");
            goto cleanup;
        }
        if (n == 0) {
            break;
        }
        if (!write_all(hStdout, buf, n)) {
            print_error("WriteFile(stdout)");
            goto cleanup;
        }
    }

    if (WaitForSingleObject(pi.hProcess, INFINITE) == WAIT_FAILED) {
        print_error("WaitForSingleObject");
        goto cleanup;
    }
    if (!GetExitCodeProcess(pi.hProcess, &child_code)) {
        print_error("GetExitCodeProcess");
        goto cleanup;
    }
    printf("[parent PID %lu] child PID %lu exited with code %lu\n",
           (unsigned long)GetCurrentProcessId(), (unsigned long)pi.dwProcessId,
           (unsigned long)child_code);
    result = (child_code == 0) ? EXIT_SUCCESS : EXIT_FAILURE;

cleanup:
    if (hPipeWrite != NULL)            CloseHandle(hPipeWrite);
    if (hPipeRead != NULL)             CloseHandle(hPipeRead);
    if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    if (pi.hThread != NULL)            CloseHandle(pi.hThread);
    if (pi.hProcess != NULL) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
    }
    return result;
}
