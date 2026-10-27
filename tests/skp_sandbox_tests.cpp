// R109 S1 (ADR 0183): the .skp helper under its sandbox. The checks:
//   - every committed fixture, imported, probed and dry-run by the sandboxed helper, gives the same
//     package bytes and the same R line as the same code run unsandboxed in this process;
//   - a sandboxed helper's policy refuses a socket and an exec (the process is killed with SIGSYS), a
//     fork (EPERM), a read outside the granted file, and a write outside the private directory, and it
//     allows a write inside it;
//   - a layer that cannot be installed refuses with sandboxUnavailable and parses nothing (exit 70),
//     both for a simulated failure and for the real helper under a filter that refuses the layer's calls
//     (ADR 0183 section 6.7);
//   - the host classifies a helper killed by SIGSYS as sandboxViolation, and a helper that refuses with
//     sandboxUnavailable as that refusal, while a crash stays a crash.
// Needs sketchyup-skp-worker (SKP_WORKER_PATH). Landlock must be available: the checks fail, not skip,
// when it is not (ADR 0183 decision 9).
#include "io/ifc_import.hpp"
#include "io/skp_worker.hpp"
#include "sandbox/sandbox.hpp"
#include "sandbox/syscalls_x86_64.hpp"
#include "skp/import.hpp"
#include "skp/progress.hpp"
#include "skp/sandbox_policy.hpp"

#include <QCoreApplication>
#include <QTemporaryDir>

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
#include <string>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace sketchyup;

namespace {

int failures = 0;

void check(bool ok, const std::string& message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << "\n";
        ++failures;
    }
}

std::string readAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The last "R " line of a helper's stdout: its result, as the application reads it.
std::string resultLine(const std::string& text) {
    std::string line;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const std::string current = text.substr(start, end - start);
        if (current.rfind("R ", 0) == 0) line = current;
        start = end + 1;
    }
    return line;
}

std::string fixture(const std::string& name) { return std::string(SOURCE_DIR) + "/tests/fixtures/skp/" + name; }

struct Outcome {
    bool signaled = false;
    int signal = 0;
    int code = -1;
    std::string out;  // the child's stdout
};

// Runs body in a forked child whose stdout is a file, and reports how it ended. body returns the exit code.
Outcome inChild(const std::string& stdoutFile, const std::function<int()>& body) {
    std::fflush(nullptr);
    const pid_t pid = fork();
    if (pid == 0) {
        const int fd = open(stdoutFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd >= 0) dup2(fd, 1);
        _exit(body());
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

// The helper, run with the given argument list as the application runs it (the same binary, no shell).
// An optional filter is installed in the child before exec: the test's own refusal of some calls.
Outcome runHelper(const std::string& stdoutFile, const std::vector<std::string>& args,
                  const std::vector<long>& refused = {}, int refusedErrno = 0) {
    std::vector<sock_filter> filter;
    if (!refused.empty()) {
        // [0] load the call number; one jump per refused call to the refusal; then allow; then refuse.
        const std::size_t count = refused.size();
        filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 0));
        for (std::size_t i = 0; i < count; ++i)
            filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<unsigned>(refused[i]), static_cast<unsigned char>(count - i), 0));
        filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
        filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | static_cast<unsigned>(refusedErrno)));
    }
    std::vector<std::string> argv{SKP_WORKER_PATH};
    argv.insert(argv.end(), args.begin(), args.end());
    std::vector<char*> cargv;
    for (std::string& argument : argv) cargv.push_back(argument.data());
    cargv.push_back(nullptr);
    return inChild(stdoutFile, [&] {
        if (!filter.empty()) {
            prctl(PR_SET_NO_NEW_PRIVS, 1UL, 0UL, 0UL, 0UL);
            sock_fprog program{static_cast<unsigned short>(filter.size()), filter.data()};
            if (syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0UL, &program) != 0) return 99;
        }
        execv(SKP_WORKER_PATH, cargv.data());
        return 98;
    });
}

// The same code the helper runs, unsandboxed, in a forked child (so the package is written by it).
Outcome baseline(const std::string& stdoutFile, const std::string& mode, const std::string& input,
                 const std::string& output) {
    return inChild(stdoutFile, [&] {
        skp::Progress progress;
        if (mode == "probe") return skp::runProbe(input, progress);
        skp::SkpImportOptions options;
        options.input = input;
        options.output = output;
        options.dryRun = mode == "dry";
        options.deadlineSeconds = 0;  // no watch thread in the baseline: it does not change the output
        options.rlimitAs = 0;
        return skp::runImport(options, progress);
    });
}

void testFixturesIdentical(const std::string& scratch) {
    const std::vector<std::string> names = {
        "attribute_reporting.skp", "box.skp",           "component_instances.skp", "face_with_hole.skp",
        "hidden_tag.skp",          "mirrored_instance.skp", "named_attributes.skp",   "nested_component.skp",
        "scaled_instance.skp",     "soft_smooth_edges.skp", "textured_face.skp",      "two_materials.skp"};
    for (const std::string& name : names) {
        const std::string input = fixture(name);
        const std::string stem = scratch + "/" + name;

        // import: the package bytes and the R line
        const std::string sandboxedPackage = stem + ".sandboxed.skskpp";
        const std::string plainPackage = stem + ".plain.skskpp";
        const Outcome sandboxed = runHelper(stem + ".sandboxed.out", {"--import", input, "--output", sandboxedPackage});
        const Outcome plain = baseline(stem + ".plain.out", "import", input, plainPackage);
        check(!sandboxed.signaled && sandboxed.code == 0, name + ": the sandboxed import exits 0 (code " +
                                                             std::to_string(sandboxed.code) + ", signal " +
                                                             std::to_string(sandboxed.signal) + ")");
        check(plain.code == 0, name + ": the unsandboxed import exits 0");
        check(readAll(sandboxedPackage) == readAll(plainPackage) && !readAll(plainPackage).empty(),
              name + ": the sandboxed package is byte-identical to the unsandboxed one");
        check(resultLine(sandboxed.out) == resultLine(plain.out) && !resultLine(plain.out).empty(),
              name + ": the sandboxed R line equals the unsandboxed one");

        // probe: the developer mode, sandboxed as well
        const Outcome probed = runHelper(stem + ".probe.out", {"--probe", input});
        const Outcome probePlain = baseline(stem + ".probe-plain.out", "probe", input, "");
        check(!probed.signaled && probed.code == 0, name + ": the sandboxed probe exits 0");
        check(resultLine(probed.out) == resultLine(probePlain.out) && !resultLine(probed.out).empty(),
              name + ": the sandboxed probe's R line equals the unsandboxed one");

        // dry run: parses, writes nothing, so it has no writable directory
        const Outcome dry = runHelper(stem + ".dry.out", {"--import", input, "--dry-run"});
        const Outcome dryPlain = baseline(stem + ".dry-plain.out", "dry", input, "");
        check(!dry.signaled && dry.code == 0, name + ": the sandboxed dry run exits 0");
        check(resultLine(dry.out) == resultLine(dryPlain.out) && !resultLine(dry.out).empty(),
              name + ": the sandboxed dry run's R line equals the unsandboxed one");
    }
}

void testPolicyRefusesAttacks(const std::string& scratch) {
    // A private directory with the input copied into it, as the application does, and a sibling file
    // that is outside it.
    const std::string privateDir = scratch + "/private";
    std::filesystem::create_directories(privateDir);
    const std::string input = privateDir + "/box.skp";
    std::filesystem::copy_file(fixture("box.skp"), input, std::filesystem::copy_options::overwrite_existing);
    const std::string output = privateDir + "/package.skskpp";
    const std::string sibling = scratch + "/sibling.txt";
    { std::ofstream(sibling) << "outside"; }

    const auto armed = [&](const std::function<int()>& attack) {
        return [&, attack] {
            // The policy the helper applies, before any of the attacks.
            if (applySkpSandbox(input, output)) return 3;
            return attack();
        };
    };

    Outcome socketCall = inChild(scratch + "/attack-socket.out", armed([] {
        socket(AF_INET, SOCK_STREAM, 0);
        return 0;
    }));
    check(socketCall.signaled && socketCall.signal == SIGSYS, "the sandboxed helper cannot create a socket (killed with SIGSYS)");

    Outcome execCall = inChild(scratch + "/attack-exec.out", armed([] {
        char* const argv[] = {const_cast<char*>("/bin/true"), nullptr};
        execv("/bin/true", argv);
        return 0;
    }));
    check(execCall.signaled && execCall.signal == SIGSYS, "the sandboxed helper cannot exec another program (killed with SIGSYS)");

    Outcome forkCall = inChild(scratch + "/attack-fork.out", armed([] {
        const pid_t pid = fork();
        if (pid == 0) _exit(0);
        return (pid == -1 && errno == EPERM) ? 0 : 1;
    }));
    check(!forkCall.signaled && forkCall.code == 0, "the sandboxed helper's fork is EPERM");

    Outcome readOutside = inChild(scratch + "/attack-read.out", armed([&] {
        const int fd = open(sibling.c_str(), O_RDONLY);
        return (fd < 0 && errno == EACCES) ? 0 : 1;
    }));
    check(!readOutside.signaled && readOutside.code == 0, "the sandboxed helper cannot read a file outside its directory (EACCES)");

    Outcome writeOutside = inChild(scratch + "/attack-write.out", armed([&] {
        const int fd = open((scratch + "/escaped.txt").c_str(), O_WRONLY | O_CREAT, 0600);
        return (fd < 0 && errno == EACCES) ? 0 : 1;
    }));
    check(!writeOutside.signaled && writeOutside.code == 0, "the sandboxed helper cannot create a file outside its directory (EACCES)");

    Outcome writeInside = inChild(scratch + "/attack-inside.out", armed([&] {
        const int fd = open((privateDir + "/inside.txt").c_str(), O_WRONLY | O_CREAT, 0600);
        return fd >= 0 ? 0 : 1;
    }));
    check(!writeInside.signaled && writeInside.code == 0, "the sandboxed helper can write inside its private directory");

    Outcome readInput = inChild(scratch + "/attack-input.out", armed([&] {
        const int fd = open(input.c_str(), O_RDONLY);
        return fd >= 0 ? 0 : 1;
    }));
    check(!readInput.signaled && readInput.code == 0, "the sandboxed helper can read its input file");
}

void testRefusalsFailClosed(const std::string& scratch) {
    const std::string input = fixture("box.skp");
    // In-process: each layer that cannot be installed is named, and nothing is applied after it.
    for (const sandbox::Layer layer :
         {sandbox::Layer::noNewPrivileges, sandbox::Layer::landlock, sandbox::Layer::seccomp}) {
        Outcome outcome = inChild(scratch + "/simulate.out", [&] {
            sandbox::ApplyOptions options;
            options.simulate = sandbox::Failure{layer, ENOSYS};
            const auto failure = applySkpSandbox(input, scratch + "/simulated.skskpp", options);
            if (!failure || failure->layer != layer) return 1;
            return skp::refuseSandboxUnavailable(*failure) == skp::kSandboxUnavailableExit ? 0 : 2;
        });
        const std::string expected = std::string("\"message\":\"sandboxUnavailable: ") + sandbox::layerName(layer) +
                                     " (Function not implemented)\"";
        check(!outcome.signaled && outcome.code == 0 && outcome.out.find(expected) != std::string::npos,
              std::string("a simulated ") + sandbox::layerName(layer) + " failure refuses with sandboxUnavailable");
        check(!std::filesystem::exists(scratch + "/simulated.skskpp"), "a refusal writes no package");
    }

    // The real helper, with a filter that refuses the layer's calls (the measured method of ADR 0183 6.7).
    const Outcome noLandlock = runHelper(scratch + "/no-landlock.out", {"--import", input, "--output", scratch + "/no-landlock.skskpp"},
                                         {444, 445, 446}, ENOSYS);
    check(!noLandlock.signaled && noLandlock.code == skp::kSandboxUnavailableExit,
          "the helper without Landlock exits " + std::to_string(skp::kSandboxUnavailableExit) + " (code " +
              std::to_string(noLandlock.code) + ")");
    check(noLandlock.out.find("\"code\":\"sandboxUnavailable\"") != std::string::npos &&
              noLandlock.out.find("sandboxUnavailable: landlock (Function not implemented)") != std::string::npos,
          "the helper without Landlock names the layer: " + resultLine(noLandlock.out));
    check(!std::filesystem::exists(scratch + "/no-landlock.skskpp"), "the helper without Landlock writes no package");

    const Outcome noSeccomp = runHelper(scratch + "/no-seccomp.out", {"--import", input, "--output", scratch + "/no-seccomp.skskpp"},
                                        {317}, EPERM);
    check(!noSeccomp.signaled && noSeccomp.code == skp::kSandboxUnavailableExit,
          "the helper without a seccomp filter exits " + std::to_string(skp::kSandboxUnavailableExit));
    check(noSeccomp.out.find("sandboxUnavailable: seccomp filter (Operation not permitted)") != std::string::npos,
          "the helper without a seccomp filter names the layer: " + resultLine(noSeccomp.out));
    check(!std::filesystem::exists(scratch + "/no-seccomp.skskpp"), "the helper without a seccomp filter writes no package");
}

// A stand-in helper: a shell script that ends the way the test needs. The host runs it with the import's argv.
std::string standIn(const std::string& directory, const std::string& name, const std::string& body) {
    const std::string path = directory + "/" + name;
    {
        std::ofstream script(path);
        script << "#!/bin/sh\n" << body << "\n";
    }
    chmod(path.c_str(), 0755);
    return path;
}

void testHostClassifies(const std::string& scratch) {
    const std::string input = fixture("box.skp");
    const auto importWith = [&](const std::string& worker) -> std::pair<bool, std::string> {
        sketchy::SkpWorkerOptions options;
        options.workerPath = QString::fromStdString(worker);
        options.scratchRoot = QString::fromStdString(scratch);
        try {
            sketchy::importSkpFile(QString::fromStdString(input), options);
        } catch (const sketchy::IfcImportError& error) {
            return {true, error.code()};
        }
        return {false, "imported"};
    };

    // killed by SIGSYS, the seccomp filter's kill
    const auto violation = importWith(standIn(scratch, "sigsys-helper.sh", "kill -31 $$"));
    check(violation.first && violation.second == "sandboxViolation",
          "a helper killed by SIGSYS is sandboxViolation, not a crash (got " + violation.second + ")");

    // refused with the sandbox's reason and exit 70
    const auto unavailable = importWith(standIn(
        scratch, "unavailable-helper.sh",
        "printf 'R {\"status\":\"refused\",\"code\":\"sandboxUnavailable\",\"message\":\"sandboxUnavailable: landlock (Function not implemented)\"}\\n'\nexit 70"));
    check(unavailable.first && unavailable.second == "sandboxUnavailable",
          "a helper refused with exit 70 is the refusal sandboxUnavailable (got " + unavailable.second + ")");

    // a crash that is not the sandbox's: SIGABRT stays a crash
    const auto crash = importWith(standIn(scratch, "abort-helper.sh", "kill -6 $$"));
    check(crash.first && crash.second == "workerFailed", "a helper killed by SIGABRT is still a crash (workerFailed)");
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        std::cerr << "FAIL: no temporary directory\n";
        return 1;
    }
    const std::string scratch = temporary.path().toStdString();
    testFixturesIdentical(scratch);
    testPolicyRefusesAttacks(scratch);
    testRefusalsFailClosed(scratch);
    testHostClassifies(scratch);
    if (failures == 0) std::cout << "skp sandbox tests passed\n";
    return failures == 0 ? 0 : 1;
}
