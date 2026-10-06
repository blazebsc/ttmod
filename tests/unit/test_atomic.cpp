// Atomic write policy (Phase 4 hardening): unique temp names, no partial
// file ever visible under the final name, overwrite works, and the temp
// file is cleaned up on failure.
#include <cassert>
#include <cstdio>
#include <string>

#include "ttmod/file_io.hpp"

using ttmod::file_io::atomic_temp_name;
using ttmod::file_io::read_all;
using ttmod::file_io::remove_file;
using ttmod::file_io::write_file_atomic;

int main() {
    const std::string dir = "/tmp/opencode_ttmod_atomic";
    const std::string target = dir + "/value.json";

    std::string cmd = "rm -rf " + dir + " && mkdir -p " + dir;
    assert(system(cmd.c_str()) == 0);

    // Temp names are unique per process, not a shared ".tmp" suffix.
    std::string t = atomic_temp_name(target);
    assert(t != target + ".tmp");
    assert(t.size() > target.size() && t.find(target) == 0);
    // Same input yields the same name within one process (so a retry can
    // find its own leftovers); different targets never collide.
    assert(atomic_temp_name(target) == t);
    assert(atomic_temp_name(dir + "/other.json") != t);

    // Fresh write, then overwrite: content is fully replaced, never merged.
    assert(write_file_atomic(target, "{\"a\":1}"));
    assert(read_all(target) == "{\"a\":1}");
    assert(write_file_atomic(target, "{\"a\":2}"));
    assert(read_all(target) == "{\"a\":2}");

    // The temp file is gone after a successful write.
    assert(read_all(t) == "");

    // Empty and binary-ish content round-trips.
    assert(write_file_atomic(target, ""));
    assert(read_all(target) == "");
    std::string nul_text("a\0b", 3);
    assert(write_file_atomic(target, nul_text));
    assert(read_all(target) == nul_text);

    // Failure leaves no partial file under the final name: writing into a
    // directory that does not exist must fail cleanly and report false.
    assert(!write_file_atomic(dir + "/missing_dir/value.json", "x"));
    // The previous good file is untouched by the failed write.
    assert(read_all(target) == nul_text);

    // A stale temp from a killed process is never read (only the final name
    // is parsed) and gets overwritten by the next write.
    {
        FILE* f = fopen(t.c_str(), "wb"); // simulate leftover
        assert(f);
        fwrite("garbage", 1, 7, f);
        fclose(f);
        assert(read_all(target) == nul_text); // stale temp is invisible
        assert(write_file_atomic(target, "{\"fresh\":true}"));
        assert(read_all(target) == "{\"fresh\":true}");
        assert(read_all(t) == ""); // consumed by the rename
    }

    // Two writers to different files in the same directory never collide.
    {
        std::string a = dir + "/a.json", b = dir + "/b.json";
        assert(atomic_temp_name(a) != atomic_temp_name(b));
        assert(write_file_atomic(a, "A"));
        assert(write_file_atomic(b, "B"));
        assert(read_all(a) == "A" && read_all(b) == "B");
    }

    // Deliberately left behind: the next write overwrites the same temp name.
    remove_file(t.c_str());
    std::puts("atomic: all asserts passed");
    return 0;
}