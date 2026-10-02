#include "maia/storage/storage.hpp"
#include <cassert>
#include <iostream>
#include <filesystem>

int main() {
    using maia::storage::FilesystemStorage;
    using maia::storage::Category;

    std::string test_dir = "/tmp/maia_test_storage_engine";
    std::filesystem::remove_all(test_dir);

    FilesystemStorage storage(test_dir);
    assert(storage.is_accessible());
    assert(std::filesystem::exists(test_dir + "/assets"));
    assert(std::filesystem::exists(test_dir + "/hls"));
    assert(std::filesystem::exists(test_dir + "/recordings"));

    // 1. Write file and verify exists
    std::string key = "sample_test.mp4";
    std::string sample_data = "ABCDEFGHIJ0123456789";
    assert(storage.write_file(Category::Assets, key, sample_data));
    assert(storage.exists(Category::Assets, key));

    // 2. Stat
    auto st = storage.stat(Category::Assets, key);
    assert(st.has_value());
    assert(st->size == sample_data.size());
    assert(!st->etag.empty());

    // 3. Read chunk
    auto r1 = storage.read_chunk(Category::Assets, key, 0, 5);
    assert(r1.has_value());
    std::string s1(r1->begin(), r1->end());
    assert(s1 == "ABCDE");

    auto r2 = storage.read_chunk(Category::Assets, key, 10, 10);
    assert(r2.has_value());
    std::string s2(r2->begin(), r2->end());
    assert(s2 == "0123456789");

    // 4. Path traversal protection
    assert(!storage.write_file(Category::Assets, "../evil.txt", "payload"));
    assert(!storage.exists(Category::Assets, "../evil.txt"));
    assert(!storage.get_path(Category::Assets, "../evil.txt").has_value());

    // 5. List
    storage.write_file(Category::Assets, "extra_1.dat", "data1");
    storage.write_file(Category::Assets, "extra_2.dat", "data2");
    auto listed = storage.list(Category::Assets);
    assert(listed.size() >= 3);

    // 6. Delete file
    assert(storage.delete_file(Category::Assets, key));
    assert(!storage.exists(Category::Assets, key));

    std::filesystem::remove_all(test_dir);
    std::cout << "[Test PASS] FilesystemStorage engine & security validation\n";
    return 0;
}
