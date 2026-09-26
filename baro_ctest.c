/* CTest adapter: validate completion and clean up descendants on termination. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
/* CreateProcess command-line quoting follows the Microsoft CRT argv rules. */
static char *quote(char *out, const char *arg) {
    *out++ = '"';
    for (;;) {
        size_t slashes = 0;
        while (*arg == '\\') { slashes++; arg++; }
        size_t count = (*arg == '"' || !*arg) ? slashes * 2 : slashes;
        while (count--) *out++ = '\\';
        if (!*arg) break;
        if (*arg == '"') *out++ = '\\';
        *out++ = *arg++;
    }
    *out++ = '"'; *out++ = ' '; *out = 0;
    return out;
}
#else
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#endif
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "Usage: baro_ctest <executable> [arguments]\n"); return 1; }
    char path[4096];
    int status = -1;
#ifdef _WIN32
    char temporary[MAX_PATH];
    DWORD length = GetTempPathA(sizeof(temporary), temporary);
    if (!length || length >= sizeof(temporary) || !GetTempFileNameA(temporary, "bro", 0, path)) return 1;
    if (!SetEnvironmentVariableA("BARO_CTEST_RESULT", path)) { DeleteFileA(path); return 1; }
    size_t size = 1;
    for (int i = 1; i < argc; i++) size += 2 * strlen(argv[i]) + 4;
    char *command = (char *)malloc(size);
    if (!command) { DeleteFileA(path); return 1; }
    char *end = command;
    for (int i = 1; i < argc; i++) end = quote(end, argv[i]);
    STARTUPINFOA startup = {0}; PROCESS_INFORMATION process;
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE job = CreateJobObjectA(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        if (job) CloseHandle(job);
        free(command); DeleteFileA(path); return 1;
    }
    if (CreateProcessA(NULL, command, NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &startup, &process)) {
        if (!AssignProcessToJobObject(job, process.hProcess)) {
            TerminateProcess(process.hProcess, 1);
        } else {
            ResumeThread(process.hThread);
        }
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD code = 1;
        if (GetExitCodeProcess(process.hProcess, &code)) status = code == 0 ? 0 : 1;
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
    }
    CloseHandle(job);
    free(command);
#else
    const char *temporary = getenv("TMPDIR");
    if (!temporary) temporary = "/tmp";
    int length = snprintf(path, sizeof(path), "%s/baro-ctest-XXXXXX", temporary);
    if (length < 0 || (size_t)length >= sizeof(path)) return 1;
    int fd = mkstemp(path);
    if (fd < 0) return 1;
    close(fd);
    if (setenv("BARO_CTEST_RESULT", path, 1)) { remove(path); return 1; }
    int lifetime[2];
    if (pipe(lifetime)) { remove(path); return 1; }
    pid_t child = fork();
    if (!child) {
        close(lifetime[0]); close(lifetime[1]);
        if (setpgid(0, 0)) _exit(127);
        execvp(argv[1], argv + 1); perror("Baro test launch"); _exit(127);
    }
    if (child > 0) setpgid(child, child);
    // CTest may kill only the adapter. A guard outside the child's group sees
    // EOF when the adapter dies, and kills the whole test process group.
    pid_t guard = child > 0 ? fork() : -1;
    if (!guard) {
        close(lifetime[1]);
        close(0); close(1); close(2);
        char ignored;
        while (read(lifetime[0], &ignored, 1) < 0 && errno == EINTR) {}
        kill(-child, SIGKILL);
        remove(path);
        _exit(0);
    }
    close(lifetime[0]);
    if (child > 0 && guard < 0) kill(-child, SIGKILL);
    if (child > 0) {
        int result;
        pid_t waited;
        do { waited = waitpid(child, &result, 0); } while (waited < 0 && errno == EINTR);
        if (waited == child) {
            if (WIFEXITED(result)) status = WEXITSTATUS(result);
            else if (WIFSIGNALED(result)) fprintf(stderr, "Baro test terminated by signal %d\n", WTERMSIG(result));
        }
    }
    // Keep the guard alive until after reading the result below.
#endif
    char record[32] = {0};
    FILE *file = fopen(path, "rb");
    size_t size_read = file ? fread(record, 1, sizeof(record), file) : 0;
    if (file) fclose(file);
    remove(path);
#ifndef _WIN32
    close(lifetime[1]);
    if (guard > 0) {
        while (waitpid(guard, NULL, 0) < 0 && errno == EINTR) {}
    }
    if (guard < 0) status = -1;
#endif
    if (status || size_read != 14 || memcmp(record, "BARO_CTEST_OK\n", 14)) {
        fprintf(stderr, "Baro test failed: process=%d, completion=%s\n", status,
                size_read == 14 && !memcmp(record, "BARO_CTEST_OK\n", 14) ? "valid" : "missing or invalid");
        return 1;
    }
    return 0;
}
