// R109 S2 (ADR 0183): the IFC and STEP helper under its sandbox. The checks:
//   - every committed IFC and STEP fixture, run by the sandboxed helper in the modes the
//   application uses (the
//     import, the check, the STEP import and its metadata, the STEP probe), gives the same package
//     bytes, the same return code and the same R line as the same code run unsandboxed in this
//     process;
//   - the diagnosis case (--test-fault crash:N with --diagnose-from 0, whose children fork) is
//   identical under the
//     sandbox, and the fork-like clone of its children runs;
//   - the attack probes: an exec and ptrace are killed with SIGSYS, a socket is EPERM; a fork-like
//   child runs and cannot exec;
//     a clone into a new namespace, an ioctl other than TCGETS2 and a prctl other than PDEATHSIG
//     are EPERM; a read of /etc/passwd and of a sibling file, and a write to a sibling, are EACCES;
//     a write inside the private directory and a read of the input work; a parser's abort is
//     SIGABRT, a crash and not a violation;
//   - a layer that cannot be installed refuses with sandboxUnavailable and parses nothing (exit
//   70), for a simulated
//     failure and for the real helper under a filter that refuses the layer's calls (ADR 0183
//     section 6.7);
//   - the host classifies a helper killed by SIGSYS as sandboxViolation and does not restart it, a
//   refusal with exit
//     70 as sandboxUnavailable, and SIGABRT as a crash, for the IFC import and for the STEP import.
// Needs sketchyup-ifc-worker (IFC_WORKER_PATH). Landlock must be available: the checks fail, not
// skip (ADR 0183 decision 9).
#include "ifc/check.hpp"
#include "ifc/import.hpp"
#include "ifc/probe.hpp"
#include "ifc/sandbox_policy.hpp"
#include "ifc/step_import.hpp"
#include "ifc/step_probe.hpp"
#include "io/ifc_worker.hpp"
#include "io/step_worker.hpp"
#include "sandbox/sandbox.hpp"
#include "sandbox/syscalls_x86_64.hpp"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sched.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace sketchyup;

namespace {

int failures = 0;

void check(bool ok, const std::string &message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << "\n";
        ++failures;
    }
}

std::string readAll(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The last "R " line of a helper's stdout: its result, as the application reads it.
std::string resultLine(const std::string &text) {
    std::string line;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        const std::string current = text.substr(start, end - start);
        if (current.rfind("R ", 0) == 0)
            line = current;
        start = end + 1;
    }
    return line;
}

std::string ifcFixture(const std::string &name) {
    return std::string(SOURCE_DIR) + "/tests/fixtures/ifc/" + name;
}

std::string stepFixture(const std::string &name) {
    return std::string(SOURCE_DIR) + "/tests/fixtures/step/" + name;
}

// The fixtures of one kind in one directory, in name order.
std::vector<std::string> fixturesIn(const std::string &directory, const std::string &extension) {
    std::vector<std::string> paths;
    for (const auto &entry : std::filesystem::directory_iterator(directory))
        if (entry.is_regular_file() && entry.path().extension() == extension)
            paths.push_back(entry.path().string());
    std::sort(paths.begin(), paths.end());
    return paths;
}

struct Outcome {
    bool signaled = false;
    int signal = 0;
    int code = -1;
    std::string out; // the child's stdout
};

// Runs body in a forked child whose stdout is a file, and reports how it ended. body returns the
// exit code.
Outcome inChild(const std::string &stdoutFile, const std::function<int()> &body) {
    std::fflush(nullptr);
    const pid_t pid = fork();
    if (pid == 0) {
        const int fd = open(stdoutFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd >= 0)
            dup2(fd, 1);
        const int code = body();
        // The helper's code writes with std::cout as well as stdio: both are flushed before the
        // child ends.
        std::cout.flush();
        std::fflush(nullptr);
        _exit(code);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    Outcome outcome;
    outcome.signaled = WIFSIGNALED(status);
    outcome.signal = outcome.signaled ? WTERMSIG(status) : 0;
    outcome.code = outcome.signaled ? -1 : WEXITSTATUS(status);
    outcome.out = readAll(stdoutFile);
    return outcome;
}

// The helper, run with the given argument list as the application runs it (the same binary, no
// shell). An optional filter is installed in the child before exec: the test's own refusal of some
// calls.
Outcome runHelper(const std::string &stdoutFile, const std::vector<std::string> &args,
                  const std::vector<long> &refused = {}, int refusedErrno = 0) {
    std::vector<sock_filter> filter;
    if (!refused.empty()) {
        // [0] load the call number; one jump per refused call to the refusal; then allow; then
        // refuse.
        const std::size_t count = refused.size();
        filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 0));
        for (std::size_t i = 0; i < count; ++i)
            filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<unsigned>(refused[i]),
                                      static_cast<unsigned char>(count - i), 0));
        filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
        filter.push_back(
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | static_cast<unsigned>(refusedErrno)));
    }
    std::vector<std::string> argv{IFC_WORKER_PATH};
    argv.insert(argv.end(), args.begin(), args.end());
    std::vector<char *> cargv;
    for (std::string &argument : argv)
        cargv.push_back(argument.data());
    cargv.push_back(nullptr);
    return inChild(stdoutFile, [&] {
        if (!filter.empty()) {
            prctl(PR_SET_NO_NEW_PRIVS, 1UL, 0UL, 0UL, 0UL);
            sock_fprog program{static_cast<unsigned short>(filter.size()), filter.data()};
            if (syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0UL, &program) != 0)
                return 99;
        }
        execv(IFC_WORKER_PATH, cargv.data());
        return 98;
    });
}

// The IFC import, the check and the STEP modes run unsandboxed in this process: the same code, the
// same results.
Outcome baselineImport(const std::string &stdoutFile, const std::string &input,
                       const std::string &output, int threads) {
    return inChild(stdoutFile, [&] {
        ifc::ImportOptions options;
        options.input = input;
        options.output = output;
        options.threads = threads;
        return ifc::runImport(options);
    });
}

Outcome baselineCheck(const std::string &stdoutFile, const std::string &input) {
    return inChild(stdoutFile, [&] { return ifc::runCheck(input); });
}

Outcome baselineStepPackage(const std::string &stdoutFile, const std::string &input,
                            const std::string &output) {
    return inChild(stdoutFile, [&] {
        ifc::StepImportOptions options;
        options.file = input;
        options.output = output;
        return ifc::runStepImportPackage(options);
    });
}

Outcome baselineStepMetadata(const std::string &stdoutFile, const std::string &input) {
    return inChild(stdoutFile, [&] {
        ifc::StepImportOptions options;
        options.file = input;
        return ifc::runStepImport(options);
    });
}

Outcome baselineStepProbe(const std::string &stdoutFile, const std::string &input) {
    return inChild(stdoutFile, [&] {
        ifc::StepProbeOptions options;
        options.file = input;
        std::puts(ifc::probeStep(options).c_str());
        return ifc::runStepProbe(options);
    });
}

// Each fixture, run by the sandboxed helper and by the unsandboxed code: the same return code,
// signal, output bytes and R line. A crash must be the same crash in both (a sandbox violation
// would show up as SIGSYS in the sandboxed run).
void compareRuns(const std::string &label, const Outcome &sandboxed, const Outcome &plain,
                 const std::string &sandboxedOutput, const std::string &plainOutput) {
    check(!sandboxed.signaled || sandboxed.signal != SIGSYS,
          label + ": the sandboxed run was not killed by the sandbox");
    check(sandboxed.signaled == plain.signaled && sandboxed.signal == plain.signal &&
              sandboxed.code == plain.code,
          label + ": the same return code and signal (sandboxed " + std::to_string(sandboxed.code) +
              "/" + std::to_string(sandboxed.signal) + ", unsandboxed " +
              std::to_string(plain.code) + "/" + std::to_string(plain.signal) + ")");
    check(resultLine(sandboxed.out) == resultLine(plain.out),
          label + ": the sandboxed R line equals the unsandboxed one (sandboxed '" +
              resultLine(sandboxed.out) + "', unsandboxed '" + resultLine(plain.out) + "')");
    check(sandboxedOutput == plainOutput,
          label + ": the sandboxed output bytes equal the unsandboxed ones");
}

void testFixturesIdentical(const std::string &scratch) {
    // IFC: import and check, for every fixture and the export fixtures. The import runs at four
    // threads, the application's default, so the sandbox sees the threaded path; there the package
    // lists elements in the order the threads finish (ADR 0171 F8; the document does not depend on
    // it), so those runs compare the return code, signal and R line. A one-thread import, which is
    // in file order, compares the package bytes too.
    std::vector<std::string> ifcFiles =
        fixturesIn(std::string(SOURCE_DIR) + "/tests/fixtures/ifc", ".ifc");
    for (const std::string &path :
         fixturesIn(std::string(SOURCE_DIR) + "/tests/fixtures/ifc/export", ".ifc"))
        ifcFiles.push_back(path);
    check(ifcFiles.size() == 66, "the IFC fixtures are the 66 committed .ifc files (found " +
                                     std::to_string(ifcFiles.size()) + ")");
    for (std::size_t index = 0; index < ifcFiles.size(); ++index) {
        const std::string &input = ifcFiles[index];
        const std::string name = std::filesystem::path(input).filename().string();
        const std::string stem = scratch + "/ifc" + std::to_string(index) + "-" + name;

        const std::string sandboxedPackage = stem + ".sandboxed.skifc";
        const std::string plainPackage = stem + ".plain.skifc";
        const Outcome sandboxed =
            runHelper(stem + ".sandboxed.out",
                      {"--import", input, "--output", sandboxedPackage, "--threads", "4"});
        const Outcome plain = baselineImport(stem + ".plain.out", input, plainPackage, 4);
        compareRuns("import " + name + " (four threads)", sandboxed, plain, "", "");

        const std::string sandboxedSerial = stem + ".sandboxed-1.skifc";
        const std::string plainSerial = stem + ".plain-1.skifc";
        const Outcome serial =
            runHelper(stem + ".sandboxed-1.out",
                      {"--import", input, "--output", sandboxedSerial, "--threads", "1"});
        const Outcome serialPlain = baselineImport(stem + ".plain-1.out", input, plainSerial, 1);
        compareRuns("import " + name + " (one thread)", serial, serialPlain,
                    readAll(sandboxedSerial), readAll(plainSerial));

        const Outcome checked = runHelper(stem + ".check.out", {"--check", input});
        const Outcome checkedPlain = baselineCheck(stem + ".check-plain.out", input);
        compareRuns("check " + name, checked, checkedPlain, "", "");
    }

    // STEP: the import into a package, the metadata, and the probe, for every STEP fixture.
    const std::vector<std::string> stepFiles =
        fixturesIn(std::string(SOURCE_DIR) + "/tests/fixtures/step", ".step");
    check(stepFiles.size() == 14, "the STEP fixtures are the 14 committed .step files (found " +
                                      std::to_string(stepFiles.size()) + ")");
    for (std::size_t index = 0; index < stepFiles.size(); ++index) {
        const std::string &input = stepFiles[index];
        const std::string name = std::filesystem::path(input).filename().string();
        const std::string stem = scratch + "/step" + std::to_string(index) + "-" + name;

        const std::string sandboxedPackage = stem + ".sandboxed.skifc";
        const std::string plainPackage = stem + ".plain.skifc";
        const Outcome sandboxed = runHelper(stem + ".sandboxed.out",
                                            {"--import-step", input, "--output", sandboxedPackage});
        const Outcome plain = baselineStepPackage(stem + ".plain.out", input, plainPackage);
        compareRuns("STEP import " + name, sandboxed, plain, readAll(sandboxedPackage),
                    readAll(plainPackage));

        const Outcome metadata = runHelper(stem + ".meta.out", {"--import-step", input});
        const Outcome metadataPlain = baselineStepMetadata(stem + ".meta-plain.out", input);
        compareRuns("STEP metadata " + name, metadata, metadataPlain, "", "");

        const std::string roundTrip = stem + ".rt";
        const Outcome probed = runHelper(
            stem + ".probe.out", {"--probe-step", input, "--round-trip-directory", roundTrip});
        const Outcome probedPlain = baselineStepProbe(stem + ".probe-plain.out", input);
        compareRuns("STEP probe " + name, probed, probedPlain, "", "");
    }
}

// The diagnosis (ADR 0183 section 6.5, item 3): a fault on one element with --diagnose-from 0,
// whose children fork.
void testDiagnosisIdentical(const std::string &scratch) {
    const std::string input = ifcFixture("extrusion-profiles.ifc");
    const std::string stem = scratch + "/diagnosis";
    const std::vector<std::string> args = {
        "--import",        input, "--output",           stem + ".sandboxed.skifc",
        "--threads",       "1",   "--test-fault",       "crash:69",
        "--diagnose-from", "0",   "--diagnose-seconds", "2"};
    const Outcome sandboxed = runHelper(stem + ".sandboxed.out", args);
    const Outcome plain = inChild(stem + ".plain.out", [&] {
        ifc::ImportOptions options;
        options.input = input;
        options.output = stem + ".plain.skifc";
        options.threads = 1;
        options.testFault = "crash:69";
        options.diagnoseFrom = 0;
        options.diagnoseSeconds = 2;
        return ifc::runImport(options);
    });
    // The whole progress and result stream, not only the R line: the diagnosis's own progress (the
    // tasks it tries and the element it leaves out) is part of the result.
    compareRuns("the diagnosis case", sandboxed, plain, readAll(stem + ".sandboxed.skifc"),
                readAll(stem + ".plain.skifc"));
    check(sandboxed.out == plain.out,
          "the diagnosis case's progress lines equal the unsandboxed ones (sandboxed '" +
              sandboxed.out + "', unsandboxed '" + plain.out + "')");
}

// The policy the helper applies to one command, in a child: the attacks run under it.
ifc::IfcSandboxScope attackScope(const std::string &input, const std::string &output) {
    return ifc::IfcSandboxScope{ifc::pinPluginDirectory(), {input}, {ifc::parentDirectory(output)}};
}

void testPolicyRefusesAttacks(const std::string &scratch) {
    // A private directory with the input copied into it, as the application does, and a sibling
    // file outside it.
    const std::string privateDir = scratch + "/private";
    std::filesystem::create_directories(privateDir);
    const std::string input = privateDir + "/units-mm.ifc";
    std::filesystem::copy_file(ifcFixture("units-mm.ifc"), input,
                               std::filesystem::copy_options::overwrite_existing);
    const std::string output = privateDir + "/package.skifc";
    const std::string sibling = scratch + "/sibling.txt";
    {
        std::ofstream(sibling) << "outside";
    }

    const auto armed = [&](const std::function<int()> &attack) {
        return [&, attack] {
            // The policy the helper applies, before any of the attacks.
            if (ifc::applyIfcSandbox(attackScope(input, output)))
                return 3;
            return attack();
        };
    };

    // The network is refused with EPERM, not killed (ADR 0183 section 4.3): OpenCASCADE asks for
    // the host's address when it writes an AP203 header, and that call must fail cleanly.
    Outcome socketCall = inChild(scratch + "/attack-socket.out", armed([] {
                                     const int fd = socket(AF_INET, SOCK_STREAM, 0);
                                     return (fd == -1 && errno == EPERM) ? 0 : 1;
                                 }));
    check(!socketCall.signaled && socketCall.code == 0,
          "the sandboxed helper cannot create a socket (EPERM, code " +
              std::to_string(socketCall.code) + ")");

    Outcome execCall = inChild(scratch + "/attack-exec.out", armed([] {
                                   char *const argv[] = {const_cast<char *>("/bin/true"), nullptr};
                                   execv("/bin/true", argv);
                                   return 0;
                               }));
    check(execCall.signaled && execCall.signal == SIGSYS,
          "the sandboxed helper cannot exec another program (killed with SIGSYS)");

    Outcome ptraceCall = inChild(scratch + "/attack-ptrace.out", armed([] {
                                     ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
                                     return 0;
                                 }));
    check(ptraceCall.signaled && ptraceCall.signal == SIGSYS,
          "the sandboxed helper cannot use ptrace (killed with SIGSYS)");

    // fork-like clone (ADR 0183 decision 5): the child runs, and an exec in the child is killed.
    Outcome forkCall =
        inChild(scratch + "/attack-fork.out", armed([] {
                    const pid_t pid = fork();
                    if (pid == 0) {
                        char *const argv[] = {const_cast<char *>("/bin/true"), nullptr};
                        execv("/bin/true", argv);
                        _exit(0);
                    }
                    if (pid == -1)
                        return 1;
                    int status = 0;
                    if (waitpid(pid, &status, 0) != pid)
                        return 2;
                    return (WIFSIGNALED(status) && WTERMSIG(status) == SIGSYS) ? 0 : 4;
                }));
    check(!forkCall.signaled && forkCall.code == 0,
          "a fork-like child of the sandboxed helper runs, and its exec is killed with SIGSYS "
          "(code " +
              std::to_string(forkCall.code) + ")");

    Outcome namespaceCall =
        inChild(scratch + "/attack-namespace.out", armed([] {
                    const long result = syscall(
                        SYS_clone, static_cast<long>(CLONE_NEWUSER | SIGCHLD), 0L, 0L, 0L, 0L);
                    return (result == -1 && errno == EPERM) ? 0 : 1;
                }));
    check(!namespaceCall.signaled && namespaceCall.code == 0,
          "a clone into a new user namespace is EPERM");

    Outcome ioctlOther = inChild(scratch + "/attack-ioctl.out", armed([] {
                                     struct winsize size{};
                                     const int result = ioctl(1, TIOCGWINSZ, &size);
                                     return (result == -1 && errno == EPERM) ? 0 : 1;
                                 }));
    check(!ioctlOther.signaled && ioctlOther.code == 0, "an ioctl other than TCGETS2 is EPERM");

    // glibc's isatty asks TCGETS2 (0x802c542a); the stdout here is a file, so the kernel answers
    // ENOTTY.
    Outcome ioctlTty = inChild(scratch + "/attack-tcgets2.out", armed([] {
                                   unsigned char attributes[64] = {};
                                   const int result = ioctl(1, 0x802c542a, attributes);
                                   return (result == -1 && errno == ENOTTY) ? 0 : 1;
                               }));
    check(!ioctlTty.signaled && ioctlTty.code == 0,
          "TCGETS2 (isatty) runs: the stdout here is a file, so ENOTTY");

    Outcome prctlOther = inChild(scratch + "/attack-prctl.out", armed([] {
                                     const int result = prctl(PR_SET_DUMPABLE, 1UL, 0UL, 0UL, 0UL);
                                     return (result == -1 && errno == EPERM) ? 0 : 1;
                                 }));
    check(!prctlOther.signaled && prctlOther.code == 0,
          "a prctl other than PR_SET_PDEATHSIG is EPERM");

    Outcome readOutside = inChild(scratch + "/attack-read.out", armed([&] {
                                      const int fd = open(sibling.c_str(), O_RDONLY);
                                      return (fd < 0 && errno == EACCES) ? 0 : 1;
                                  }));
    check(!readOutside.signaled && readOutside.code == 0,
          "the sandboxed helper cannot read a file outside its directory (EACCES)");

    Outcome readPasswd = inChild(scratch + "/attack-passwd.out", armed([] {
                                     const int fd = open("/etc/passwd", O_RDONLY);
                                     return (fd < 0 && errno == EACCES) ? 0 : 1;
                                 }));
    check(!readPasswd.signaled && readPasswd.code == 0,
          "the sandboxed helper cannot read /etc/passwd (EACCES)");

    Outcome writeOutside = inChild(scratch + "/attack-write.out", armed([&] {
                                       const int fd = open((scratch + "/escaped.txt").c_str(),
                                                           O_WRONLY | O_CREAT, 0600);
                                       return (fd < 0 && errno == EACCES) ? 0 : 1;
                                   }));
    check(!writeOutside.signaled && writeOutside.code == 0,
          "the sandboxed helper cannot create a file outside its directory (EACCES)");

    Outcome writeInside = inChild(scratch + "/attack-inside.out", armed([&] {
                                      const int fd = open((privateDir + "/inside.txt").c_str(),
                                                          O_WRONLY | O_CREAT, 0600);
                                      return fd >= 0 ? 0 : 1;
                                  }));
    check(!writeInside.signaled && writeInside.code == 0,
          "the sandboxed helper can write inside its private directory");

    Outcome readInput = inChild(scratch + "/attack-input.out", armed([&] {
                                    const int fd = open(input.c_str(), O_RDONLY);
                                    return fd >= 0 ? 0 : 1;
                                }));
    check(!readInput.signaled && readInput.code == 0,
          "the sandboxed helper can read its input file");

    Outcome abortCall = inChild(scratch + "/attack-abort.out", armed([] {
                                    std::abort();
                                    return 0;
                                }));
    check(abortCall.signaled && abortCall.signal == SIGABRT,
          "a parser's abort under the policy is SIGABRT, a crash and not a violation");
}

void testRefusalsFailClosed(const std::string &scratch) {
    const std::string input = ifcFixture("units-mm.ifc");
    // In-process: each layer that cannot be installed is named, and nothing is applied after it.
    for (const sandbox::Layer layer :
         {sandbox::Layer::noNewPrivileges, sandbox::Layer::landlock, sandbox::Layer::seccomp}) {
        Outcome outcome = inChild(scratch + "/simulate.out", [&] {
            sandbox::ApplyOptions options;
            options.simulate = sandbox::Failure{layer, ENOSYS};
            const auto failure = ifc::applyIfcSandbox(
                ifc::IfcSandboxScope{ifc::pinPluginDirectory(), {input}, {scratch}}, options);
            if (!failure || failure->layer != layer)
                return 1;
            return ifc::refuseSandboxUnavailable(*failure) == ifc::kSandboxUnavailableExit ? 0 : 2;
        });
        const std::string expected = std::string("\"message\":\"sandboxUnavailable: ") +
                                     sandbox::layerName(layer) + " (Function not implemented)\"";
        check(!outcome.signaled && outcome.code == 0 &&
                  outcome.out.find(expected) != std::string::npos,
              std::string("a simulated ") + sandbox::layerName(layer) +
                  " failure refuses with sandboxUnavailable");
    }

    // The real helper, with a filter that refuses the layer's calls (the measured method of ADR
    // 0183 section 6.7).
    const std::string output = scratch + "/no-landlock.skifc";
    const Outcome noLandlock =
        runHelper(scratch + "/no-landlock.out", {"--import", input, "--output", output},
                  {444, 445, 446}, ENOSYS);
    check(!noLandlock.signaled && noLandlock.code == ifc::kSandboxUnavailableExit,
          "the helper without Landlock exits " + std::to_string(ifc::kSandboxUnavailableExit) +
              " (code " + std::to_string(noLandlock.code) + ")");
    check(noLandlock.out.find("sandboxUnavailable: landlock (Function not implemented)") !=
              std::string::npos,
          "the helper without Landlock names the layer: " + resultLine(noLandlock.out));
    check(!std::filesystem::exists(output), "the helper without Landlock writes no package");

    const std::string probeOutput = scratch + "/no-landlock-probe.out";
    const Outcome noLandlockProbe = runHelper(probeOutput, {"--probe"}, {444, 445, 446}, ENOSYS);
    check(!noLandlockProbe.signaled && noLandlockProbe.code == ifc::kSandboxUnavailableExit,
          "the probe without Landlock refuses too (code " + std::to_string(noLandlockProbe.code) +
              ")");

    const std::string seccompOutput = scratch + "/no-seccomp.skifc";
    const Outcome noSeccomp = runHelper(
        scratch + "/no-seccomp.out", {"--import", input, "--output", seccompOutput}, {317}, EPERM);
    check(!noSeccomp.signaled && noSeccomp.code == ifc::kSandboxUnavailableExit,
          "the helper without a seccomp filter exits " +
              std::to_string(ifc::kSandboxUnavailableExit));
    check(noSeccomp.out.find("sandboxUnavailable: seccomp filter (Operation not permitted)") !=
              std::string::npos,
          "the helper without a seccomp filter names the layer: " + resultLine(noSeccomp.out));
    check(!std::filesystem::exists(seccompOutput),
          "the helper without a seccomp filter writes no package");
}

// A stand-in helper: a shell script that ends the way the test needs. The host runs it with the
// command's argv.
std::string standIn(const std::string &directory, const std::string &name,
                    const std::string &body) {
    const std::string path = directory + "/" + name;
    {
        std::ofstream script(path);
        script << "#!/bin/sh\n" << body << "\n";
    }
    chmod(path.c_str(), 0755);
    return path;
}

void testHostClassifies(const std::string &scratch) {
    const std::string input = ifcFixture("units-mm.ifc");
    const std::string stepInput = stepFixture("sketchyup-step-box.step");
    const std::string runs = scratch + "/runs.log";

    // The IFC host: the helper runs once per attempt, so a violation that is restarted would leave
    // more lines.
    const auto importWith = [&](const std::string &worker) -> std::pair<std::string, int> {
        sketchy::IfcWorkerOptions options;
        options.workerPath = QString::fromStdString(worker);
        options.scratchRoot = QString::fromStdString(scratch);
        options.threads = 1;
        options.addressSpaceBytes = 0;
        try {
            sketchy::importIfcFile(QString::fromStdString(input), {}, options);
        } catch (const sketchy::IfcImportError &error) {
            return {error.code(), 0};
        }
        return {"imported", 0};
    };
    const auto importStepWith = [&](const std::string &worker) -> std::string {
        sketchy::StepWorkerOptions options;
        options.workerPath = QString::fromStdString(worker);
        options.scratchRoot = QString::fromStdString(scratch);
        try {
            sketchy::importStepFile(QString::fromStdString(stepInput), options);
        } catch (const sketchy::IfcImportError &error) {
            return error.code();
        }
        return "imported";
    };

    // killed by SIGSYS, the seccomp filter's kill (QProcess reports it as the exit code 31)
    std::filesystem::remove(runs);
    const auto violation =
        importWith(standIn(scratch, "sigsys-helper.sh", "echo run >> '" + runs + "'\nkill -31 $$"));
    check(violation.first == "sandboxViolation",
          "IFC: a helper killed by SIGSYS is sandboxViolation (got " + violation.first + ")");
    check(readAll(runs) == "run\n",
          "IFC: a sandbox violation is not restarted (the helper ran once)");
    check(importStepWith(standIn(scratch, "sigsys-step.sh", "kill -31 $$")) == "sandboxViolation",
          "STEP: a helper killed by SIGSYS is sandboxViolation");

    // refused with the sandbox's reason and exit 70
    const std::string unavailableBody =
        "printf 'R "
        "{\"status\":\"refused\",\"code\":\"sandboxUnavailable\",\"message\":"
        "\"sandboxUnavailable: landlock (Function not implemented)\"}\\n'\nexit 70";
    check(importWith(standIn(scratch, "unavailable-helper.sh", unavailableBody)).first ==
              "sandboxUnavailable",
          "IFC: a helper refused with exit 70 is the refusal sandboxUnavailable");
    check(importStepWith(standIn(scratch, "unavailable-step.sh", unavailableBody)) ==
              "sandboxUnavailable",
          "STEP: a helper refused with exit 70 is the refusal sandboxUnavailable");

    // a crash that is not the sandbox's: SIGABRT stays a crash, and the host's restart path handles
    // it
    const auto crash = importWith(standIn(scratch, "abort-helper.sh", "kill -6 $$"));
    check(crash.first == "workerFailed",
          "IFC: a helper killed by SIGABRT is still a crash (workerFailed, got " + crash.first +
              ")");
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    // The plugins are pinned as the helper pins them, so the in-process baseline loads the same
    // libraries.
    ifc::pinPluginDirectory();
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        std::cerr << "FAIL: no temporary directory\n";
        return 1;
    }
    const std::string scratch = temporary.path().toStdString();
    testFixturesIdentical(scratch);
    testDiagnosisIdentical(scratch);
    testPolicyRefusesAttacks(scratch);
    testRefusalsFailClosed(scratch);
    testHostClassifies(scratch);
    if (failures == 0)
        std::cout << "ifc sandbox tests passed\n";
    return failures == 0 ? 0 : 1;
}
