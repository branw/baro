/* Private process backend; compiled only into baro.c. */
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
extern char **environ;
#endif

static const char *baro__executable(char *buffer, size_t size, const char *fallback) {
#ifdef _WIN32
    DWORD length = GetModuleFileNameA(NULL, buffer, (DWORD)size);
    if (length && length < size) return buffer;
#elif defined(__APPLE__)
    uint32_t length = (uint32_t)size;
    if (_NSGetExecutablePath(buffer, &length) == 0) return buffer;
#elif defined(__linux__)
    ssize_t length = readlink("/proc/self/exe", buffer, size - 1);
    if (length > 0 && (size_t)length < size - 1) { buffer[length] = 0; return buffer; }
#endif
    return fallback;
}

struct baro__process {
#ifdef _WIN32
    HANDLE process, job;
#else
    pid_t pid;
#endif
    FILE *out, *err;
    char result_path[1024];
    int status;
    double started;
    int timed_out;
};

static double baro__monotonic(void) {
#ifdef _WIN32
    return (double)GetTickCount64() / 1000.0;
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) { perror("clock_gettime"); exit(EXIT_FAILURE); }
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
#endif
}

static void baro__process_pause(void) {
#ifdef _WIN32
    Sleep(5);
#else
    struct timespec delay = {0, 5000000};
    nanosleep(&delay, NULL);
#endif
}

static void baro__process_dispose(struct baro__process *p) {
    if (p->out) fclose(p->out);
    if (p->err) fclose(p->err);
    if (*p->result_path) remove(p->result_path);
    p->out = p->err = NULL;
}

#ifdef _WIN32
/* Quote one argument using the Windows CRT's backslash/quote convention. */
static int baro__quote(char *dest, size_t capacity, const char *arg) {
    size_t n = 0, slashes = 0;
#define BARO__PUT(c) do { if (n + 1 >= capacity) return 0; dest[n++] = (c); } while (0)
    BARO__PUT('"');
    for (;;) {
        char c = *arg++;
        if (c == '\\') { slashes++; continue; }
        if (c == '"' || c == '\0') slashes *= 2;
        while (slashes) { BARO__PUT('\\'); slashes--; }
        if (!c) break;
        if (c == '"') BARO__PUT('\\');
        BARO__PUT(c);
    }
    BARO__PUT('"'); dest[n] = 0;
#undef BARO__PUT
    return (int)n;
}
#endif

static int baro__process_start(struct baro__process *p, const char *exe, size_t id, int compiler_diagnostics) {
    memset(p, 0, sizeof(*p));
    p->out = tmpfile(); p->err = tmpfile();
    if (!p->out || !p->err) goto failed;
#ifdef _WIN32
    char temp[MAX_PATH];
    if (!GetTempPathA(sizeof(temp), temp) || !GetTempFileNameA(temp, "bar", 0, p->result_path)) goto failed;
#else
    const char *temp = getenv("TMPDIR");
    if (!temp || !*temp) temp = "/tmp";
    if (snprintf(p->result_path, sizeof(p->result_path), "%s/baro-result-XXXXXX", temp) >= (int)sizeof(p->result_path)) goto failed;
    int fd = mkstemp(p->result_path);
    if (fd < 0) goto failed;
    close(fd);
#endif
    char index[32]; snprintf(index, sizeof(index), "%zu", id);
    char *args[] = {(char *)exe, "--baro-child", index, "--baro-result", p->result_path, "-o", "--diagnostics", compiler_diagnostics ? "compiler" : "plain", NULL};
#ifdef _WIN32
    char command[32768]; size_t used = 0;
    for (int i = 0; args[i]; i++) {
        int n = baro__quote(command + used, sizeof(command) - used, args[i]);
        if (!n) goto failed;
        used += n; command[used++] = ' '; command[used] = 0;
    }
    STARTUPINFOA startup; PROCESS_INFORMATION info;
    memset(&startup, 0, sizeof(startup)); startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = (HANDLE)_get_osfhandle(_fileno(p->out));
    startup.hStdError = (HANDLE)_get_osfhandle(_fileno(p->err));
    SetHandleInformation(startup.hStdOutput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(startup.hStdError, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    p->job = CreateJobObjectA(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    memset(&limits, 0, sizeof(limits));
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!p->job || !SetInformationJobObject(p->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) goto failed;
    BOOL ok = CreateProcessA(NULL, command, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, NULL, &startup, &info);
    SetHandleInformation(startup.hStdOutput, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(startup.hStdError, HANDLE_FLAG_INHERIT, 0);
    if (!ok) goto failed;
    if (!AssignProcessToJobObject(p->job, info.hProcess)) {
        TerminateProcess(info.hProcess, 1);
        WaitForSingleObject(info.hProcess, INFINITE);
        CloseHandle(info.hThread); CloseHandle(info.hProcess); goto failed;
    }
    ResumeThread(info.hThread);
    CloseHandle(info.hThread); p->process = info.hProcess;
#else
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fileno(p->out), STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fileno(p->err), STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, fileno(p->out));
    posix_spawn_file_actions_addclose(&actions, fileno(p->err));
    fcntl(fileno(p->out), F_SETFD, FD_CLOEXEC);
    fcntl(fileno(p->err), F_SETFD, FD_CLOEXEC);
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attributes, 0);
    int error = posix_spawnp(&p->pid, exe, &actions, &attributes, args, environ);
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    if (error) { errno = error; goto failed; }
#endif
    p->started = baro__monotonic();
    return 1;
failed:
#ifdef _WIN32
    if (p->job) CloseHandle(p->job);
#endif
    perror("Unable to start isolated test");
    baro__process_dispose(p);
    return 0;
}

static int baro__process_poll(struct baro__process *p, double timeout) {
#ifdef _WIN32
    DWORD wait = WaitForSingleObject(p->process, 0);
    if (wait == WAIT_TIMEOUT) {
        if (timeout <= 0 || baro__monotonic() - p->started < timeout) return 0;
        p->timed_out = 1;
        TerminateJobObject(p->job, 1);
        wait = WaitForSingleObject(p->process, INFINITE);
    }
    DWORD code;
    if (wait != WAIT_OBJECT_0 || !GetExitCodeProcess(p->process, &code)) code = (DWORD)-1;
    p->status = (int)code;
    CloseHandle(p->process); CloseHandle(p->job);
#else
    int status;
    pid_t result = waitpid(p->pid, &status, WNOHANG);
    if (!result) {
        if (timeout <= 0 || baro__monotonic() - p->started < timeout) return 0;
        p->timed_out = 1;
        kill(-p->pid, SIGKILL);
        do { result = waitpid(p->pid, &status, 0); } while (result < 0 && errno == EINTR);
    }
    if (result < 0 && errno == EINTR) return 0;
    if (result < 0) p->status = -1;
    else p->status = WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status);
    // Do not leave grandchildren running after their test has completed.
    kill(-p->pid, SIGKILL);
#endif
    return 1;
}

static void baro__process_output(FILE *file, FILE *dest, int tail) {
    if (fflush(file) || fseek(file, 0, SEEK_END)) return;
    long size = ftell(file);
    if (size < 0) return;
    fseek(file, tail && size > BARO__STDOUT_BUF_SIZE ? size - BARO__STDOUT_BUF_SIZE : 0, SEEK_SET);
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), file)) != 0) fwrite(buf, 1, n, dest);
}

static struct baro__result baro__process_result(struct baro__process *p, size_t id) {
    struct baro__result result = {1, 1, 0, 0, "Abnormal termination"};
    if (p->timed_out) { result.reason = "Timeout"; return result; }
    FILE *file = fopen(p->result_path, "rb");
    if (!file) return result;
    char magic[32] = {0}; size_t received_id, assertions, failed;
    int flag;
    if (fscanf(file, "%31s", magic) == 1 && !strcmp(magic, "BARO_ABORT") && p->status == 128 + SIGABRT) {
        result.reason = "SIGABRT";
    } else if (!strcmp(magic, "BARO_ABORT_FAILED")) {
        result.reason = "Assertions failed before SIGABRT";
    } else if (!strcmp(magic, "BARO1") &&
        fscanf(file, "%zu %d %zu %zu", &received_id, &flag, &assertions, &failed) == 4 &&
        received_id == id && (flag == 0 || flag == 1) && failed <= assertions &&
        p->status == (flag ? EXIT_FAILURE : EXIT_SUCCESS)) {
        result.failed = flag; result.asserts = assertions; result.asserts_failed = failed;
        result.reason = flag ? "Assertion failure" : NULL;
    }
    fclose(file);
    return result;
}
