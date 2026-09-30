// Tests for the eye log (recorder.cpp): names, and starting, stopping and reaping a child that stands in for
// `frameeyeosc --record`. Built with the panel as recorder-test; exits non-zero on failure. Given the path of a real
// frameeyeosc, it also records a few seconds with it (reads the eye server read-only) and checks the CSV:
//   panel/build/recorder-test target/release/frameeyeosc
// Files go to a folder made under the current one, removed at the end.
#include "recorder.h"

#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

namespace {

int gFailures = 0;

/**
 * Record a failed check.
 * @param ok the check
 * @param what what was checked
 * @param line where
 */
void check(bool ok, const char* what, int line) {
    if (ok) return;
    ++gFailures;
    std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
}

#define CHECK(condition) check((condition), #condition, __LINE__)

using recorder::Recorder;

/**
 * Monotonic seconds, as the panel's loop passes them.
 * @return seconds
 */
double now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

/**
 * Write an executable shell script.
 * @param path where
 * @param body the script after the #! line
 */
void script(const std::string& path, const std::string& body) {
    std::ofstream(path) << "#!/bin/sh\n" << body;
    ::chmod(path.c_str(), 0755);
}

/**
 * Poll until the child is reaped, at most a few seconds.
 * @param r the recorder
 * @param offset added to the time passed (to get past the recorder's own limits)
 * @return true if it was reaped
 */
bool waitReaped(Recorder& r, double offset = 0.0) {
    const double end = now() + 5.0;
    while (r.busy() && now() < end) {
        r.poll(now() + offset);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return !r.busy();
}

/**
 * Whether a file exists.
 * @param path the file
 * @return true if it does
 */
bool exists(const std::string& path) {
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0;
}

/**
 * A file's text.
 * @param path the file
 * @return its contents
 */
std::string read(const std::string& path) {
    std::ifstream file(path);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void testNames() {
    ::setenv("TZ", "UTC", 1);
    ::tzset();
    CHECK(recorder::fileStamp(0) == "1970-01-01_00-00-00");
    CHECK(recorder::fileStamp(1790000000) == "2026-09-21_14-13-20");
    CHECK(recorder::elapsedText(0) == "0:00" && recorder::elapsedText(83.9) == "1:23");
    CHECK(recorder::elapsedText(recorder::kMaxSec) == "60:00");
    ::setenv("HOME", "/home/steamos", 1);
    CHECK(recorder::shortPath("/home/steamos/.local/share/frameeyeosc/recordings") ==
          "~/.local/share/frameeyeosc/recordings");
    CHECK(recorder::shortPath("/tmp/x") == "/tmp/x");
    ::unsetenv("XDG_DATA_HOME");
    CHECK(recorder::defaultDir() == "/home/steamos/.local/share/frameeyeosc/recordings");
    ::setenv("XDG_DATA_HOME", "/data", 1);
    CHECK(recorder::defaultDir() == "/data/frameeyeosc/recordings");
}

void testUniqueNames(const std::string& dir) {
    // A stop and a start within the same second: never over an earlier recording's files
    const std::string names = dir + "/names";
    ::mkdir(names.c_str(), 0755);
    const std::string stamp = "2026-10-01_12-00-00";
    CHECK(recorder::uniqueBase(names, stamp) == names + "/eyes_" + stamp);
    std::ofstream(names + "/eyes_" + stamp + ".csv") << "x";
    CHECK(recorder::uniqueBase(names, stamp) == names + "/eyes_" + stamp + "_2");
    // Any of the three files counts
    std::ofstream(names + "/eyes_" + stamp + "_2.config.json") << "{}";
    CHECK(recorder::uniqueBase(names, stamp) == names + "/eyes_" + stamp + "_3");
}

void testChild(const std::string& dir) {
    const std::string config = dir + "/config.json";
    std::ofstream(config) << "{\"gaze_roll_deg\": 2.0}\n";
    // Stops on SIGINT like frameeyeosc --record, saying how many samples it wrote
    const std::string good = dir + "/good.sh";
    script(good, "trap 'echo \"Stopped: 3 samples in $2\" >&2; exit 0' INT\n"
                 "echo sample_time > \"$2\"\necho \"Recording to $2\" >&2\n"
                 "while :; do sleep 0.05; done\n");
    {
        Recorder r;
        const double start = now();
        CHECK(r.start(good, dir + "/out", config, start));
        CHECK(r.recording() && r.busy());
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        CHECK(!r.poll(now()) && r.recording());
        const recorder::View v = r.view(start + 83.5);
        CHECK(v.recording && v.elapsedSec > 83 && v.error.empty());
        // eyes_<local time>.csv, the settings copied next to it, and the child's messages in a .log
        const std::string base = v.path.substr(0, v.path.size() - 4);
        CHECK(v.path.rfind(dir + "/out/eyes_", 0) == 0 && v.path.size() == dir.size() + 33);
        CHECK(read(base + ".config.json") == "{\"gaze_roll_deg\": 2.0}\n");
        // A second start while one runs is refused
        CHECK(!r.start(good, dir + "/out", config, now()));
        r.stop(now());
        CHECK(!r.recording() && r.busy());
        CHECK(waitReaped(r));
        CHECK(r.view(now()).error.empty() && !r.view(now()).recording);
        CHECK(read(v.path) == "sample_time\n");
        CHECK(read(base + ".log").find("Stopped: 3 samples") != std::string::npos);
    }
    {
        // Ends by itself (no eye server): its last words are the error
        const std::string failing = dir + "/failing.sh";
        script(failing, "echo \"Recording to $2\" >&2\necho 'Error: the eye server is not running' >&2\nexit 1\n");
        Recorder r;
        CHECK(r.start(failing, dir + "/out2", config, now()));
        CHECK(waitReaped(r));
        CHECK(r.view(now()).error == "the eye server is not running");
        // Silent: the exit code
        const std::string silent = dir + "/silent.sh";
        script(silent, "exit 3\n");
        CHECK(r.start(silent, dir + "/out2", config, now() + 1));
        CHECK(waitReaped(r));
        CHECK(r.view(now()).error == "frameeyeosc stopped (exit code 3)");
    }
    {
        // Can't be run at all, or not found
        Recorder r;
        CHECK(!r.start(dir + "/missing", dir + "/out3", config, now()) && !r.busy());
        CHECK(r.view(now()).error.find("can't run " + dir + "/missing: ") == 0);
        CHECK(!r.start("", dir + "/out3", config, now()));
        CHECK(r.view(now()).error.find("not found") != std::string::npos);
    }
    {
        // Stops by itself after an hour, and a child that ignores SIGINT is killed after kStopWaitSec
        const std::string stubborn = dir + "/stubborn.sh";
        script(stubborn, "trap '' INT\nexec sleep 30\n");
        Recorder r;
        const double start = now();
        CHECK(r.start(stubborn, dir + "/out4", config, start));
        CHECK(r.poll(start + recorder::kMaxSec + 0.01) && !r.recording() && r.busy());
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK(r.busy());
        CHECK(waitReaped(r, recorder::kMaxSec + recorder::kStopWaitSec + 1));
        CHECK(r.view(now()).error.empty());
        // The row then says it stopped at the limit, until the next start
        CHECK(r.view(now()).autoStopped);
        CHECK(r.start(good, dir + "/out4", config, now()) && !r.view(now()).autoStopped);
        r.stop(now());
        CHECK(waitReaped(r) && !r.view(now()).autoStopped);
    }
    {
        // Two recordings started within the same second keep their own files
        Recorder r;
        CHECK(r.start(good, dir + "/out6", config, now()));
        const std::string first = r.view(now()).path;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        r.stop(now());
        CHECK(waitReaped(r));
        CHECK(r.start(good, dir + "/out6", config, now()));
        const std::string second = r.view(now()).path;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        r.stop(now());
        CHECK(waitReaped(r));
        CHECK(first != second && read(first) == "sample_time\n" && read(second) == "sample_time\n");
    }
    {
        // A descriptor of the panel's above 1024 does not reach the recorder
        const std::string leak = dir + "/leak";
        const std::string peek = dir + "/peek.sh";
        script(peek, "if [ -e /proc/$$/fd/2000 ]; then echo leaked > " + leak + "; else echo closed > " + leak +
                         "; fi\ntrap 'exit 0' INT\nwhile :; do sleep 0.05; done\n");
        // Past the usual soft limit of 1024 (the hard limit is far higher; SteamVR's libraries may raise it)
        rlimit limit {};
        ::getrlimit(RLIMIT_NOFILE, &limit);
        if (limit.rlim_cur < 4096 && limit.rlim_max >= 4096) {
            limit.rlim_cur = 4096;
            ::setrlimit(RLIMIT_NOFILE, &limit);
        }
        const int high = ::dup2(1, 2000);
        CHECK(high == 2000);
        Recorder r;
        CHECK(r.start(peek, dir + "/out7", config, now()));
        for (int i = 0; i < 100 && !exists(leak); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK(read(leak) == "closed\n");
        r.stop(now());
        CHECK(waitReaped(r));
        ::close(2000);
    }
    {
        // Exiting the panel stops and reaps it: nothing is left running
        const std::string pidFile = dir + "/pid";
        const std::string waiting = dir + "/waiting.sh";
        script(waiting, "echo $$ > " + pidFile + "\ntrap 'exit 0' INT\nwhile :; do sleep 0.05; done\n");
        pid_t child = 0;
        {
            Recorder r;
            CHECK(r.start(waiting, dir + "/out5", config, now()));
            for (int i = 0; i < 100 && !exists(pidFile); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            child = std::atoi(read(pidFile).c_str());
        }
        CHECK(child > 0 && ::kill(child, 0) != 0);
    }
}

/**
 * Record a few seconds with the real frameeyeosc and check the CSV is whole up to the stop.
 * @param program frameeyeosc
 * @param dir the scratch folder
 */
void testReal(const std::string& program, const std::string& dir) {
    Recorder r;
    CHECK(r.start(program, dir + "/real", dir + "/config.json", now()));
    std::this_thread::sleep_for(std::chrono::seconds(3));
    CHECK(!r.poll(now()) && r.recording());
    const std::string path = r.view(now()).path;
    r.stop(now());
    CHECK(waitReaped(r));
    CHECK(r.view(now()).error.empty());
    std::ifstream file(path);
    std::string line;
    std::getline(file, line);
    const auto fields = [](const std::string& text) {
        size_t count = 1;
        for (char c : text) count += c == ',' ? 1 : 0;
        return count;
    };
    const size_t columns = fields(line);
    size_t rows = 0;
    bool whole = line.rfind("sample_time", 0) == 0;
    while (std::getline(file, line)) {
        whole &= fields(line) == columns;
        ++rows;
    }
    const std::string text = read(path);
    whole &= !text.empty() && text.back() == '\n';
    std::printf("recorder-test: frameeyeosc recorded %zu samples in 3 s (%zu columns), %s; its log: %s\n", rows,
                columns, whole ? "every line whole" : "BROKEN LINES",
                read(path.substr(0, path.size() - 4) + ".log").c_str());
    CHECK(whole);
}

}  // namespace

/**
 * Run the tests.
 * @param argc argument count
 * @param argv optionally the path of a real frameeyeosc
 * @return 0 if all passed
 */
int main(int argc, char** argv) {
    char dir[] = "recorder-test-XXXXXX";
    if (::mkdtemp(dir) == nullptr) {
        std::perror("mkdtemp");
        return 1;
    }
    char cwd[4096];
    const std::string scratch = std::string(::getcwd(cwd, sizeof(cwd))) + "/" + dir;
    testNames();
    testUniqueNames(scratch);
    testChild(scratch);
    if (argc > 1) testReal(argv[1], scratch);
    const std::string remove = "rm -rf '" + scratch + "'";
    if (std::system(remove.c_str()) != 0) std::fprintf(stderr, "could not remove %s\n", scratch.c_str());
    if (gFailures == 0) std::printf("recorder-test: all passed\n");
    return gFailures == 0 ? 0 : 1;
}
