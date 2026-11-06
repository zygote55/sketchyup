#pragma once
// Fault children as a new process image: fork, then exec this test again with --child. macOS
// aborts a forked child of a multithreaded process that touches the file system before exec
// (CoreFoundation: "fork without exec is not supported"), and these children save files. The
// child is still a separate process, killed or failed at the same boundary a forked child was.
#include <QCoreApplication>
#include <QFile>
#include <QStringList>
#include <optional>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

// Starts this executable as "<self> --child <arguments...>" and returns its wait status, or -1 when
// it could not be started. beforeExec runs in the child between fork and exec, so it may issue
// system calls only (a resource limit, an ignored signal or a user ID survive the exec).
inline int runChild(const QStringList &arguments, void (*beforeExec)() = nullptr) {
    std::vector<QByteArray> bytes{QFile::encodeName(QCoreApplication::applicationFilePath()),
                                  "--child"};
    for (const auto &argument : arguments)
        bytes.push_back(argument.toUtf8());
    std::vector<char *> argv;
    for (auto &argument : bytes)
        argv.push_back(argument.data());
    argv.push_back(nullptr);
    const pid_t pid = ::fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        if (beforeExec)
            beforeExec();
        ::execv(argv[0], argv.data());
        ::_exit(127);
    }
    int status = 0;
    return ::waitpid(pid, &status, 0) == pid ? status : -1;
}

// The arguments after --child when this process is a fault child started by runChild.
inline std::optional<QStringList> childArguments() {
    const auto arguments = QCoreApplication::arguments();
    if (arguments.size() < 2 || arguments[1] != "--child")
        return std::nullopt;
    return arguments.mid(2);
}

inline bool killedBy(int status, int signal) {
    return status != -1 && WIFSIGNALED(status) && WTERMSIG(status) == signal;
}
inline bool exitedWith(int status, int code) {
    return status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == code;
}
