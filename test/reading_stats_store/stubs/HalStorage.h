#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fake {
inline std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> files;
}  // namespace fake

class HalFile {
 public:
  std::shared_ptr<std::vector<uint8_t>> bytes;
  size_t pos = 0;

  size_t fileSize() const { return bytes ? bytes->size() : 0; }
  int read(void* out, size_t size) {
    if (!bytes) return -1;
    size = std::min(size, bytes->size() - std::min(pos, bytes->size()));
    std::memcpy(out, bytes->data() + pos, size);
    pos += size;
    return static_cast<int>(size);
  }
  size_t write(const uint8_t* data, const size_t size) {
    if (!bytes) return 0;
    bytes->resize(std::max(bytes->size(), pos + size));
    std::memcpy(bytes->data() + pos, data, size);
    pos += size;
    return size;
  }
  void flush() {}
  void close() { bytes.reset(); }
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }

  bool ensureDirectoryExists(const char*) { return true; }
  bool exists(const char* path) const { return fake::files.count(path) != 0; }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    const auto found = fake::files.find(path);
    if (found == fake::files.end()) return false;
    file.bytes = found->second;
    file.pos = 0;
    return true;
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    fake::files[path] = std::make_shared<std::vector<uint8_t>>();
    return openFileForRead("", path, file);
  }
  bool remove(const char* path) { return fake::files.erase(path) != 0; }
  bool rename(const char* from, const char* to) {
    if (!exists(from) || exists(to)) return false;
    fake::files[to] = fake::files[from];
    fake::files.erase(from);
    return true;
  }
};

#define Storage HalStorage::getInstance()
