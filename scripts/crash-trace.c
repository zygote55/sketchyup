// Crash reporter for native tests (D-034), preloaded by scripts/test-deadline.sh.
//
// On SIGSEGV, SIGBUS, SIGFPE, SIGILL or SIGABRT it prints the signal and every thread's stack
// to standard error (gdb attaches to the crashing process; glibc's backtrace() is the fallback),
// flushes the process's C stdio buffers so block-buffered test output is not lost, and then
// dies with the same signal, so the exit status is unchanged. CI containers have no usable core
// dumps. Processes built with AddressSanitizer keep ASan's own report: nothing is installed.
// Set SKETCHYUP_CRASH_TRACE=0 to disable it.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t reporting;

static void say(const char *text) {
    ssize_t ignored = write(2, text, strlen(text));
    (void)ignored;
}

static void report(int sig, siginfo_t *info, void *context) {
    (void)context;
    if (reporting) // A second thread crashing while the first reports waits for the end.
        pause();
    reporting = 1;
    char line[256], pid_text[32];
    snprintf(pid_text, sizeof pid_text, "%d", (int)getpid());
    char exe[160] = {0};
    ssize_t length = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (length < 0)
        exe[0] = 0;
    snprintf(line, sizeof line, "\n=== %s (pid %s, thread %d) received signal %d (%s), code %d ===\n",
             exe, pid_text, (int)gettid(), sig, strsignal(sig), info ? info->si_code : 0);
    say(line);
    pid_t child = fork();
    if (child == 0) {
        dup2(2, 1);
        execlp("timeout", "timeout", "60", "gdb", "-p", pid_text, "-batch", "-nx", "-q", "-iex",
               "set debuginfod enabled off", "-ex", "set pagination off", "-ex",
               "set print frame-arguments none", "-ex", "thread apply all bt", "-ex",
               "call (int) ((int (*)(void *)) fflush)(0)", (char *)0);
        _exit(127);
    }
    int status = 1;
    if (child > 0)
        waitpid(child, &status, 0);
    if (child <= 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        say("--- gdb unavailable; backtrace() of the crashing thread:\n");
        void *frames[128];
        backtrace_symbols_fd(frames, backtrace(frames, 128), 2);
    }
    say("=== End of crash report ===\n");
    signal(sig, SIG_DFL);
    raise(sig);
}

__attribute__((constructor)) static void install(void) {
    const char *enabled = getenv("SKETCHYUP_CRASH_TRACE");
    if (enabled && strcmp(enabled, "0") == 0)
        return;
    // gdb (here, or test-deadline.sh's at a deadline) is a sibling, not an ancestor: allow it
    // to attach under Yama's ptrace_scope 1.
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
    if (dlsym(RTLD_DEFAULT, "__asan_init"))
        return;
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_sigaction = report;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    const int signals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
    for (size_t i = 0; i < sizeof signals / sizeof signals[0]; ++i) {
        struct sigaction previous;
        // Leave a handler the program inherited (not the default) alone.
        if (sigaction(signals[i], NULL, &previous) == 0 && previous.sa_handler == SIG_DFL)
            sigaction(signals[i], &action, NULL);
    }
}
