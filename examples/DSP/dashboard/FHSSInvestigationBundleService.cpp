// SPDX-License-Identifier: MIT
#include "FHSSInvestigationBundleService.hpp"

#include "FHSSHash.hpp"
#include "FHSSPinnedSchemas.hpp"
#include "FHSSJobController.hpp"
#include "FHSSObservationService.hpp"
#include "graph/dashboard/GraphConfigurationService.hpp"
#include "graph/dashboard/GraphRuntimeSession.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cmath>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <map>
#include <ranges>
#include <regex>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace dsp::fhss::dashboard {
namespace {

using graphx::examples::fhss::Sha256;
using graphx::examples::fhss::Sha256Hasher;
using graphx::examples::fhss::Sha512Hasher;

class PinnedSchemaRegistry {
public:
  PinnedSchemaRegistry() {
    for (const auto text : kPinnedSchemaJson) {
      auto schema = nlohmann::json::parse(text);
      const auto id = schema.value("$id", std::string{});
      if (id.empty() || !schemas_.emplace(id, std::move(schema)).second)
        throw std::logic_error("pinned JSON schema identity is missing or duplicated");
    }
  }

  void Validate(const nlohmann::json &instance, std::string_view id) const {
    const auto found = schemas_.find(std::string(id));
    if (found == schemas_.end())
      throw std::runtime_error("required pinned JSON schema is unavailable");
    ValidateNode(instance, found->second, found->second, "$", 0);
  }

private:
  static bool HasType(const nlohmann::json &value, std::string_view type) {
    if (type == "object") return value.is_object();
    if (type == "array") return value.is_array();
    if (type == "string") return value.is_string();
    if (type == "boolean") return value.is_boolean();
    if (type == "null") return value.is_null();
    if (type == "integer") return value.is_number_integer() || value.is_number_unsigned();
    if (type == "number") return value.is_number();
    return false;
  }

  [[noreturn]] static void Fail(const std::string &path,
                                std::string_view rule) {
    throw std::runtime_error("pinned JSON schema violation at " + path +
                             ": " + std::string(rule));
  }

  const nlohmann::json &Resolve(std::string_view reference,
                                const nlohmann::json &root) const {
    const auto hash = reference.find('#');
    const auto base = reference.substr(0, hash);
    const nlohmann::json *target = &root;
    if (!base.empty()) {
      const auto found = schemas_.find(std::string(base));
      if (found == schemas_.end())
        throw std::runtime_error("pinned JSON schema reference is unavailable");
      target = &found->second;
    }
    if (hash != std::string_view::npos && hash + 1 < reference.size()) {
      const auto pointer = std::string(reference.substr(hash + 1));
      try { target = &target->at(nlohmann::json::json_pointer(pointer)); }
      catch (...) { throw std::runtime_error("pinned JSON schema reference is invalid"); }
    }
    return *target;
  }

  void ValidateNode(const nlohmann::json &value, const nlohmann::json &schema,
                    const nlohmann::json &root, const std::string &path,
                    std::size_t depth) const {
    if (depth > 128 || !schema.is_object()) Fail(path, "schema depth/type");
    if (schema.contains("$ref")) {
      const auto &resolved = Resolve(schema.at("$ref").get<std::string>(), root);
      const auto &resolved_root = resolved.contains("$id") ? resolved : root;
      ValidateNode(value, resolved, resolved_root, path, depth + 1);
    }
    if (schema.contains("type")) {
      bool matches = false;
      const auto &types = schema.at("type");
      if (types.is_string()) matches = HasType(value, types.get<std::string>());
      else if (types.is_array())
        for (const auto &type : types)
          matches = matches || (type.is_string() && HasType(value, type.get<std::string>()));
      if (!matches) Fail(path, "type");
    }
    if (schema.contains("const") && value != schema.at("const")) Fail(path, "const");
    if (schema.contains("enum") &&
        std::ranges::none_of(schema.at("enum"), [&](const auto &entry) { return entry == value; }))
      Fail(path, "enum");
    if (schema.contains("anyOf")) {
      std::size_t matches = 0;
      for (const auto &candidate : schema.at("anyOf")) {
        try { ValidateNode(value, candidate, root, path, depth + 1); ++matches; }
        catch (const std::runtime_error &) {}
      }
      if (matches == 0) Fail(path, "anyOf");
    }
    if (value.is_object()) {
      if (schema.contains("required"))
        for (const auto &key : schema.at("required"))
          if (!value.contains(key.get<std::string>())) Fail(path, "required");
      if (schema.contains("maxProperties") &&
          value.size() > schema.at("maxProperties").get<std::size_t>())
        Fail(path, "maxProperties");
      const auto properties = schema.value("properties", nlohmann::json::object());
      for (const auto &[key, child] : value.items()) {
        if (properties.contains(key))
          ValidateNode(child, properties.at(key), root, path + "/" + key, depth + 1);
        else if (schema.value("additionalProperties", true) == false)
          Fail(path + "/" + key, "additionalProperties");
      }
    }
    if (value.is_array()) {
      if (schema.contains("minItems") && value.size() < schema.at("minItems").get<std::size_t>())
        Fail(path, "minItems");
      if (schema.contains("maxItems") && value.size() > schema.at("maxItems").get<std::size_t>())
        Fail(path, "maxItems");
      if (schema.value("uniqueItems", false)) {
        std::set<std::string> seen;
        for (const auto &entry : value)
          if (!seen.insert(entry.dump()).second) Fail(path, "uniqueItems");
      }
      if (schema.contains("items"))
        for (std::size_t index = 0; index < value.size(); ++index)
          ValidateNode(value.at(index), schema.at("items"), root,
                       path + "/" + std::to_string(index), depth + 1);
    }
    if (value.is_string()) {
      const auto &text = value.get_ref<const std::string &>();
      if (schema.contains("minLength") && text.size() < schema.at("minLength").get<std::size_t>())
        Fail(path, "minLength");
      if (schema.contains("maxLength") && text.size() > schema.at("maxLength").get<std::size_t>())
        Fail(path, "maxLength");
      if (schema.contains("pattern") &&
          !std::regex_search(text, std::regex(schema.at("pattern").get<std::string>())))
        Fail(path, "pattern");
      if (schema.value("format", std::string{}) == "date-time" &&
          !std::regex_match(text, std::regex(
              R"(^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(\.[0-9]+)?Z$)")))
        Fail(path, "date-time format");
      if (schema.value("format", std::string{}) == "uri" &&
          !std::regex_match(text, std::regex(
              R"(^[A-Za-z][A-Za-z0-9+.-]*:[^[:space:]]+$)")))
        Fail(path, "uri format");
      if (schema.value("format", std::string{}) == "uuid" &&
          !std::regex_match(text, std::regex(
              R"(^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-5][0-9a-fA-F]{3}-[89aAbB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$)")))
        Fail(path, "uuid format");
    }
    if (value.is_number()) {
      const auto number = value.get<long double>();
      if (!std::isfinite(number)) Fail(path, "finite number");
      if (schema.contains("minimum") && number < schema.at("minimum").get<long double>())
        Fail(path, "minimum");
      if (schema.contains("maximum") && number > schema.at("maximum").get<long double>())
        Fail(path, "maximum");
      if (schema.contains("exclusiveMinimum") &&
          number <= schema.at("exclusiveMinimum").get<long double>())
        Fail(path, "exclusiveMinimum");
    }
  }

  std::unordered_map<std::string, nlohmann::json> schemas_;
};

const PinnedSchemaRegistry &PinnedSchemas() {
  static const PinnedSchemaRegistry registry;
  return registry;
}

class UniqueFd {
public:
  explicit UniqueFd(int fd = -1) : fd_(fd) {}
  ~UniqueFd() {
    if (fd_ >= 0)
      ::close(fd_);
  }
  UniqueFd(const UniqueFd &) = delete;
  UniqueFd &operator=(const UniqueFd &) = delete;
  UniqueFd(UniqueFd &&other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
  UniqueFd &operator=(UniqueFd &&other) noexcept {
    if (this != &other) {
      if (fd_ >= 0)
        ::close(fd_);
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }
  [[nodiscard]] int Get() const { return fd_; }
  [[nodiscard]] int Release() { return std::exchange(fd_, -1); }
  explicit operator bool() const { return fd_ >= 0; }

private:
  int fd_ = -1;
};

struct FileIdentity {
  dev_t device{};
  ino_t inode{};
  off_t bytes{};
  nlink_t links{};
  timespec modified{};

  friend bool operator==(const FileIdentity &left, const FileIdentity &right) {
    return left.device == right.device && left.inode == right.inode &&
           left.bytes == right.bytes && left.links == right.links &&
           left.modified.tv_sec == right.modified.tv_sec &&
           left.modified.tv_nsec == right.modified.tv_nsec;
  }
};

struct FileDigests {
  std::uint64_t bytes = 0;
  std::string sha256;
  std::string sha512;
};

std::string NowRfc3339() {
  const auto now = std::chrono::system_clock::now();
  const auto seconds = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
  gmtime_r(&seconds, &utc);
  std::array<char, 32> buffer{};
  std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return buffer.data();
}

nlohmann::json Problem(int status, std::string code, std::string detail) {
  return {{"type", "urn:graphx:dashboard:problem:" + code},
          {"title", code},
          {"status", status},
          {"detail", detail},
          {"code", std::move(code)},
          {"instance", "/api/v1/fhss/investigations"}};
}

bool SafeComponent(std::string_view value) {
  return !value.empty() && value.size() <=
                               FHSSInvestigationBundleService::kMaxComponentBytes &&
         value != "." && value != ".." &&
         std::ranges::all_of(value, [](unsigned char character) {
           return std::isalnum(character) || character == '-' ||
                  character == '_' || character == '.';
         });
}

std::string Digest(std::string_view value) {
  return Sha256(std::as_bytes(std::span(value.data(), value.size())));
}

UniqueFd OpenRoot(const std::filesystem::path &path) {
  const auto fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                                           O_NOFOLLOW);
  if (fd < 0)
    throw std::runtime_error("approved root cannot be opened safely");
  return UniqueFd(fd);
}

UniqueFd OpenDirectoryAt(int parent, std::string_view name) {
  if (!SafeComponent(name))
    throw std::invalid_argument("unsafe path component");
  const auto component = std::string(name);
  const auto fd = ::openat(parent, component.c_str(),
                           O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    throw std::runtime_error("directory component is missing or unsafe");
  return UniqueFd(fd);
}

FileIdentity IdentityOf(int fd, std::uint64_t maximum_bytes) {
  struct stat status {};
  if (::fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) ||
      status.st_nlink != 1 || status.st_size < 0 ||
      static_cast<std::uint64_t>(status.st_size) > maximum_bytes)
    throw std::runtime_error("input must be a bounded single-link regular file");
#if defined(__APPLE__)
  const auto modified = status.st_mtimespec;
#else
  const auto modified = status.st_mtim;
#endif
  return {.device = status.st_dev,
          .inode = status.st_ino,
          .bytes = status.st_size,
          .links = status.st_nlink,
          .modified = modified};
}

UniqueFd OpenRegularAt(int parent, std::string_view name,
                       std::uint64_t maximum_bytes,
                       FileIdentity *identity = nullptr) {
  if (!SafeComponent(name))
    throw std::invalid_argument("unsafe file component");
  const auto component = std::string(name);
  const auto fd = ::openat(parent, component.c_str(),
                           O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0)
    throw std::runtime_error("file is missing, linked, or unsafe");
  UniqueFd result(fd);
  const auto file_identity = IdentityOf(fd, maximum_bytes);
  if (identity != nullptr)
    *identity = file_identity;
  return result;
}

void SeekStart(int fd) {
  if (::lseek(fd, 0, SEEK_SET) < 0)
    throw std::runtime_error("regular file seek failed");
}

template <typename Checkpoint>
FileDigests HashFd(int fd, std::uint64_t maximum_bytes,
                   Checkpoint checkpoint) {
  SeekStart(fd);
  Sha256Hasher sha256;
  Sha512Hasher sha512;
  std::vector<std::byte> buffer(FHSSInvestigationBundleService::kChunkBytes);
  std::uint64_t total = 0;
  for (;;) {
    if (!checkpoint())
      throw std::runtime_error("operation interrupted");
    const auto count = ::read(fd, buffer.data(), buffer.size());
    if (count < 0) {
      if (errno == EINTR)
        continue;
      throw std::runtime_error("bounded file hash read failed");
    }
    if (count == 0)
      break;
    const auto amount = static_cast<std::size_t>(count);
    if (total > maximum_bytes - amount)
      throw std::length_error("file exceeds hash quota");
    total += amount;
    const auto bytes = std::span(buffer).first(amount);
    sha256.Update(bytes);
    sha512.Update(bytes);
  }
  return {.bytes = total,
          .sha256 = sha256.Finish(),
          .sha512 = sha512.Finish()};
}

std::string ReadTextFd(int fd, std::size_t maximum_bytes) {
  SeekStart(fd);
  std::string result;
  result.reserve(std::min<std::size_t>(maximum_bytes, 64u * 1024u));
  std::array<char, 16u * 1024u> buffer{};
  for (;;) {
    const auto count = ::read(fd, buffer.data(), buffer.size());
    if (count < 0) {
      if (errno == EINTR)
        continue;
      throw std::runtime_error("JSON read failed");
    }
    if (count == 0)
      break;
    if (result.size() > maximum_bytes - static_cast<std::size_t>(count))
      throw std::length_error("JSON artifact exceeds quota");
    result.append(buffer.data(), static_cast<std::size_t>(count));
  }
  return result;
}

nlohmann::json ReadJsonAt(int parent, std::string_view name) {
  auto file = OpenRegularAt(parent, name,
                            FHSSInvestigationBundleService::kMaxJsonBytes);
  const auto text = ReadTextFd(file.Get(),
                               FHSSInvestigationBundleService::kMaxJsonBytes);
  try {
    return nlohmann::json::parse(text);
  } catch (const std::exception &) {
    throw std::runtime_error("JSON artifact is malformed");
  }
}

void WriteAll(int fd, std::span<const std::byte> bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto written = ::write(fd, bytes.data() + offset,
                                 bytes.size() - offset);
    if (written < 0) {
      if (errno == EINTR)
        continue;
      if (errno == ENOSPC)
        throw std::system_error(ENOSPC, std::generic_category(),
                                "artifact write failed");
      throw std::runtime_error("artifact write failed");
    }
    offset += static_cast<std::size_t>(written);
  }
}

FileDigests WriteBytesAt(int parent, std::string_view name,
                         std::span<const std::byte> bytes) {
  if (!SafeComponent(name))
    throw std::invalid_argument("unsafe output component");
  const auto component = std::string(name);
  const auto fd = ::openat(parent, component.c_str(),
                           O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC |
                               O_NOFOLLOW,
                           0600);
  if (fd < 0)
    throw std::runtime_error("artifact output collision or open failure");
  UniqueFd output(fd);
  WriteAll(fd, bytes);
  if (::fsync(fd) != 0)
    throw std::runtime_error("artifact file synchronization failed");
  return {.bytes = bytes.size(),
          .sha256 = Sha256(bytes),
          .sha512 = graphx::examples::fhss::Sha512(bytes)};
}

FileDigests WriteTextAt(int parent, std::string_view name,
                        std::string_view text) {
  return WriteBytesAt(parent, name,
                      std::as_bytes(std::span(text.data(), text.size())));
}

template <typename Checkpoint>
FileDigests CopyFdToAt(int source, int destination_parent,
                       std::string_view destination_name,
                       std::uint64_t maximum_bytes, Checkpoint checkpoint) {
  if (!SafeComponent(destination_name))
    throw std::invalid_argument("unsafe output component");
  const auto name = std::string(destination_name);
  const auto fd = ::openat(destination_parent, name.c_str(),
                           O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC |
                               O_NOFOLLOW,
                           0600);
  if (fd < 0)
    throw std::runtime_error("copied IQ output collision or open failure");
  UniqueFd output(fd);
  SeekStart(source);
  Sha256Hasher sha256;
  Sha512Hasher sha512;
  std::vector<std::byte> buffer(FHSSInvestigationBundleService::kChunkBytes);
  std::uint64_t total = 0;
  for (;;) {
    if (!checkpoint())
      throw std::runtime_error("operation interrupted");
    const auto count = ::read(source, buffer.data(), buffer.size());
    if (count < 0) {
      if (errno == EINTR)
        continue;
      throw std::runtime_error("IQ copy read failed");
    }
    if (count == 0)
      break;
    const auto amount = static_cast<std::size_t>(count);
    if (total > maximum_bytes - amount)
      throw std::length_error("copied IQ exceeds quota");
    total += amount;
    const auto bytes = std::span(buffer).first(amount);
    sha256.Update(bytes);
    sha512.Update(bytes);
    WriteAll(fd, bytes);
  }
  if (::fsync(fd) != 0)
    throw std::runtime_error("copied IQ synchronization failed");
  return {.bytes = total,
          .sha256 = sha256.Finish(),
          .sha512 = sha512.Finish()};
}

std::set<std::string> DirectoryNames(int fd) {
  // dup(2) shares the directory offset with the original file description,
  // which made a second scan silently observe EOF. Open "." to obtain an
  // independent description for every bounded enumeration.
  const auto duplicate = ::openat(fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (duplicate < 0)
    throw std::runtime_error("directory enumeration failed");
  DIR *directory = ::fdopendir(duplicate);
  if (directory == nullptr) {
    ::close(duplicate);
    throw std::runtime_error("directory enumeration failed");
  }
  std::set<std::string> names;
  errno = 0;
  while (const auto *entry = ::readdir(directory)) {
    const std::string name(entry->d_name);
    if (name != "." && name != "..")
      names.insert(name);
  }
  const auto saved_error = errno;
  ::closedir(directory);
  if (saved_error != 0)
    throw std::runtime_error("directory enumeration failed");
  return names;
}

std::uint64_t RetainedBundleBytesAt(int root,
                                    std::string_view excluded = {}) {
  std::uint64_t total = 0;
  for (const auto &bundle_name : DirectoryNames(root)) {
    if (bundle_name == excluded || bundle_name.starts_with(".tmp-")) continue;
    auto bundle = OpenDirectoryAt(root, bundle_name);
    for (const auto &artifact : DirectoryNames(bundle.Get())) {
      auto file = OpenRegularAt(bundle.Get(), artifact,
                                FHSSInvestigationBundleService::kMaxRetainedBundleBytes);
      const auto bytes = static_cast<std::uint64_t>(
          IdentityOf(file.Get(), FHSSInvestigationBundleService::kMaxRetainedBundleBytes).bytes);
      if (total > FHSSInvestigationBundleService::kMaxRetainedBundleBytes - bytes)
        throw std::length_error("aggregate retained bundle bytes exceed quota");
      total += bytes;
    }
  }
  return total;
}

std::size_t DatatypeStride(std::string_view datatype) {
  if (datatype == "cf32_le")
    return 8;
  if (datatype == "cf64_le")
    return 16;
  return 0;
}

std::optional<std::uint64_t> Unsigned(const nlohmann::json &value) {
  if (value.is_number_unsigned())
    return value.get<std::uint64_t>();
  if (!value.is_number_integer())
    return std::nullopt;
  const auto parsed = value.get<std::int64_t>();
  return parsed < 0 ? std::nullopt : std::optional<std::uint64_t>(parsed);
}

std::uint64_t RequiredUnsignedField(const nlohmann::json &value,
                                    std::string_view field) {
  if (!value.contains(field))
    throw std::runtime_error("required unsigned identity is absent");
  const auto result = Unsigned(value.at(field));
  if (!result) throw std::runtime_error("unsigned identity is out of range");
  return *result;
}

template <typename Checkpoint>
nlohmann::json CurrentExecutableIdentity(Checkpoint checkpoint) {
  std::array<char, 4096> path{};
#if defined(__APPLE__)
  std::uint32_t size = static_cast<std::uint32_t>(path.size());
  if (_NSGetExecutablePath(path.data(), &size) != 0)
    throw std::runtime_error("producer executable path exceeds bound");
#elif defined(__linux__)
  const auto count = ::readlink("/proc/self/exe", path.data(), path.size() - 1);
  if (count <= 0 || static_cast<std::size_t>(count) >= path.size())
    throw std::runtime_error("producer executable path is unavailable");
  path.at(static_cast<std::size_t>(count)) = '\0';
#else
#error "Phase 7 executable identity requires a supported host implementation"
#endif
  const auto executable_fd = ::open(path.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (executable_fd < 0)
    throw std::runtime_error("producer executable cannot be opened");
  UniqueFd executable(executable_fd);
  const auto identity = IdentityOf(executable.Get(),
                                   std::numeric_limits<std::uint64_t>::max());
  const auto digests = HashFd(executable.Get(),
                              std::numeric_limits<std::uint64_t>::max(),
                              std::move(checkpoint));
  return {{"path", std::filesystem::weakly_canonical(path.data()).string()},
          {"bytes", digests.bytes}, {"sha256", digests.sha256},
          {"device", static_cast<std::uint64_t>(identity.device)},
          {"inode", static_cast<std::uint64_t>(identity.inode)}};
}

template <typename Checkpoint>
nlohmann::json BuildApiManifest(Checkpoint checkpoint) {
  nlohmann::json schemas = nlohmann::json::array();
  for (const auto &[path, digest] : kPinnedSchemaDigests)
    schemas.push_back({{"path", path}, {"sha256", digest}});
  const auto sigmf = std::ranges::find_if(kPinnedSchemaDigests, [](const auto &entry) {
    return entry.first.ends_with("sigmf-schema.json");
  });
  if (sigmf == kPinnedSchemaDigests.end())
    throw std::logic_error("pinned SigMF digest is absent");
#if defined(__APPLE__)
  constexpr std::string_view platform = "macOS";
#else
  constexpr std::string_view platform = "Linux";
#endif
  return {{"schema", "graphx.dashboard.fhss_build_api_manifest.v1"},
          {"producer_executable",
           CurrentExecutableIdentity(std::move(checkpoint))},
          {"source_revision", kBuildSourceRevision},
          {"source_dirty", kBuildSourceDirty},
          {"source_state_capture", "configure_time_git_status_porcelain"},
          {"language_standard", "C++26"}, {"platform", platform},
          {"openapi", {{"version", "7.0.0"},
                       {"sha256", kPinnedOpenApiSha256}}},
          {"pinned_sigmf", {{"version", "1.2.6"},
                            {"schema_id", "https://raw.githubusercontent.com/sigmf/SigMF/v1.2.6/sigmf-schema.json"},
                            {"sha256", sigmf->second},
                            {"license", "Apache-2.0"}}},
          {"schemas", std::move(schemas)}};
}

void RemovePrivateDirectoryAt(int parent, std::string_view name) noexcept {
  try {
    auto directory = OpenDirectoryAt(parent, name);
    for (const auto &entry : DirectoryNames(directory.Get()))
      (void)::unlinkat(directory.Get(), entry.c_str(), 0);
    const auto component = std::string(name);
    (void)::unlinkat(parent, component.c_str(), AT_REMOVEDIR);
  } catch (...) {
  }
}

void RenameDirectoryNoReplace(int source_parent, std::string_view source,
                              int destination_parent,
                              std::string_view destination) {
  const auto from = std::string(source);
  const auto to = std::string(destination);
#if defined(__APPLE__)
  if (::renameatx_np(source_parent, from.c_str(), destination_parent,
                     to.c_str(), RENAME_EXCL) != 0)
    throw std::runtime_error(errno == EEXIST ? "bundle name collision"
                                             : "atomic bundle publication failed");
#elif defined(__linux__) && defined(SYS_renameat2)
  if (::syscall(SYS_renameat2, source_parent, from.c_str(), destination_parent,
                to.c_str(), RENAME_NOREPLACE) != 0)
    throw std::runtime_error(errno == EEXIST ? "bundle name collision"
                                             : "atomic bundle publication failed");
#else
#error "Phase 7 requires an atomic no-replace directory rename primitive"
#endif
}

std::string ArtifactSchema(std::string_view name) {
  if (name == "truth.json") return "graphx.fhss.iq-truth.v1";
  if (name == "observation.json") return "graphx.dashboard.fhss_receiver_observation.v1";
  if (name == "comparison.json") return "graphx.dashboard.fhss_comparison_result.v1";
  if (name == "receiver-config.json") return "graphx.dashboard.receiver_graph.v1";
  if (name == "receiver-result.json") return "graphx.dashboard.fhss_receiver_result.v1";
  if (name == "provenance.json") return "graphx.dashboard.fhss_investigation_provenance.v1";
  if (name == "actions.json") return "graphx.dashboard.fhss_operator_actions.v1";
  if (name == "build-api.json") return "graphx.dashboard.fhss_build_api_manifest.v1";
  if (name == "recording.sigmf-meta")
    return "https://raw.githubusercontent.com/sigmf/SigMF/v1.2.6/sigmf-schema.json";
  if (name == "external-iq-reference.json") return "graphx.dashboard.fhss_external_iq_reference.v1";
  return "binary";
}

} // namespace

struct FHSSInvestigationBundleService::Operation {
  std::uint64_t sequence = 0;
  std::string operation_id;
  std::string kind;
  std::string canonical_request;
  std::string idempotency_digest;
  nlohmann::json request;
  std::string state = "queued";
  std::string created_at;
  std::string started_at;
  std::string terminal_at;
  std::string terminal_code;
  std::string terminal_detail;
  nlohmann::json result = nullptr;
  std::chrono::steady_clock::time_point deadline;
  bool cancel_requested = false;
  bool publication_committed = false;
};

struct FHSSInvestigationBundleService::IdempotencyRecord {
  std::string canonical_request;
  std::weak_ptr<Operation> operation;
};

struct FHSSInvestigationBundleService::ValidatedBundle {
  std::string bundle_name;
  std::string iq_mode;
  std::string datatype;
  std::uint64_t iq_bytes = 0;
  std::uint64_t sample_count = 0;
  std::string iq_sha256;
  std::string iq_sha512;
  std::string expected_semantic_hash;
  std::string manifest_sha256;
  nlohmann::json receiver_graph;
  int iq_fd = -1;
};

FHSSInvestigationBundleService::FHSSInvestigationBundleService(
    std::shared_ptr<graph::dashboard::GraphConfigurationService>
        configuration_service,
    std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime_session,
    std::shared_ptr<FHSSJobController> job_controller,
    std::filesystem::path artifact_root,
    std::shared_ptr<TestHooks> test_hooks)
    : configuration_service_(std::move(configuration_service)),
      runtime_session_(std::move(runtime_session)),
      job_controller_(std::move(job_controller)),
      observation_service_(std::make_shared<FHSSObservationService>(
          configuration_service_, runtime_session_)),
      artifact_root_(std::filesystem::absolute(std::move(artifact_root))),
      bundle_root_(artifact_root_ / "fhss-investigations"),
      iq_root_(artifact_root_ / "fhss-jobs"),
      test_hooks_(std::move(test_hooks)) {
  if (!configuration_service_ || !runtime_session_ || !job_controller_)
    throw std::invalid_argument("investigation service dependencies required");
  std::filesystem::create_directories(bundle_root_);
  std::filesystem::create_directories(iq_root_);
  artifact_root_ = std::filesystem::canonical(artifact_root_);
  bundle_root_ = std::filesystem::canonical(bundle_root_);
  iq_root_ = std::filesystem::canonical(iq_root_);
  worker_ = std::jthread(
      [this](std::stop_token stop_token) { Worker(stop_token); });
}

FHSSInvestigationBundleService::~FHSSInvestigationBundleService() { Shutdown(); }

std::string FHSSInvestigationBundleService::CanonicalJson(
    const nlohmann::json &document) {
  return document.dump() + "\n";
}

std::string FHSSInvestigationBundleService::SemanticReceiverResultHash(
    const nlohmann::json &receiver_result) {
  nlohmann::json projection{
      {"accepted", receiver_result.value("accepted", false)},
      {"decoded_pulse_count",
       receiver_result.value("decoded_pulse_count", std::uint64_t{0})},
      {"status", receiver_result.value("status", std::string{})}};
  const auto canonical = CanonicalJson(projection);
  return Digest(canonical);
}

bool FHSSInvestigationBundleService::ContainsForbiddenReceiverKey(
    const nlohmann::json &value) {
  static const std::set<std::string, std::less<>> forbidden{
      "active_frequency_indices", "comparison", "expected", "expected_words",
      "generator", "generator_id", "generator_metadata", "job_id",
      "messages", "request_id", "scenario_correlation_id", "schedule",
      "truth", "truth_hash", "truth_sha256"};
  if (value.is_object()) {
    for (const auto &[key, child] : value.items())
      if (forbidden.contains(key) || ContainsForbiddenReceiverKey(child))
        return true;
  } else if (value.is_array()) {
    for (const auto &child : value)
      if (ContainsForbiddenReceiverKey(child))
        return true;
  }
  return false;
}

bool FHSSInvestigationBundleService::ValidateSigMf(
    const nlohmann::json &metadata, std::uint64_t iq_bytes,
    std::string_view iq_sha512, std::string *error) {
  const auto fail = [&](std::string message) {
    if (error != nullptr)
      *error = std::move(message);
    return false;
  };
  try {
    PinnedSchemas().Validate(
        metadata,
        "https://raw.githubusercontent.com/sigmf/SigMF/v1.2.6/sigmf-schema.json");
  } catch (const std::exception &exception) {
    return fail(std::string("official pinned SigMF schema rejected metadata: ") +
                exception.what());
  }
  if (!metadata.is_object() || !metadata.contains("global") ||
      !metadata.at("global").is_object() || !metadata.contains("captures") ||
      !metadata.at("captures").is_array() ||
      metadata.at("captures").empty() || !metadata.contains("annotations") ||
      !metadata.at("annotations").is_array() ||
      metadata.at("annotations").size() > kMaxAnnotations)
    return fail("SigMF top-level structure is invalid");
  const auto &global = metadata.at("global");
  if (global.value("core:version", std::string{}) != "1.2.6")
    return fail("unsupported SigMF core version");
  if (global.contains("core:metadata_only"))
    return fail("reference status belongs to the investigation manifest");
  const auto datatype = global.value("core:datatype", std::string{});
  const auto stride = DatatypeStride(datatype);
  if (stride == 0 || iq_bytes % stride != 0)
    return fail("SigMF datatype or byte stride is invalid");
  if (!global.contains("core:sample_rate") ||
      !global.at("core:sample_rate").is_number() ||
      !std::isfinite(global.at("core:sample_rate").get<double>()) ||
      global.at("core:sample_rate").get<double>() <= 0.0)
    return fail("SigMF sample rate is invalid");
  const auto declared_hash = global.value("core:sha512", std::string{});
  if (declared_hash.size() != 128 || declared_hash != iq_sha512 ||
      !std::ranges::all_of(declared_hash, [](unsigned char character) {
        return std::isdigit(character) ||
               (character >= static_cast<unsigned char>('a') &&
                character <= static_cast<unsigned char>('f'));
      }))
    return fail("SigMF SHA-512 is invalid");
  const auto sample_count = iq_bytes / stride;
  std::uint64_t previous_capture_start = 0;
  bool first_capture = true;
  for (const auto &capture : metadata.at("captures")) {
    if (!capture.is_object() || !capture.contains("core:sample_start") ||
        !Unsigned(capture.at("core:sample_start")) ||
        *Unsigned(capture.at("core:sample_start")) > sample_count ||
        !capture.contains("core:frequency") ||
        !capture.at("core:frequency").is_number() ||
        !std::isfinite(capture.at("core:frequency").get<double>()) ||
        capture.at("core:frequency").get<double>() < 0.0)
      return fail("SigMF capture range or frequency is invalid");
    const auto start = *Unsigned(capture.at("core:sample_start"));
    if ((first_capture && start != 0) || (!first_capture && start <= previous_capture_start))
      return fail("SigMF captures must start at zero and be strictly ordered");
    previous_capture_start = start;
    first_capture = false;
  }
  for (const auto &annotation : metadata.at("annotations")) {
    if (!annotation.is_object() ||
        !annotation.contains("core:sample_start") ||
        !Unsigned(annotation.at("core:sample_start")))
      return fail("SigMF annotation start is invalid");
    const auto start = *Unsigned(annotation.at("core:sample_start"));
    const auto count = annotation.contains("core:sample_count")
                           ? Unsigned(annotation.at("core:sample_count"))
                           : std::optional<std::uint64_t>(0);
    if (!count || start > sample_count || *count > sample_count - start)
      return fail("SigMF annotation range is invalid");
  }
  return true;
}

FHSSInvestigationBundleService::Result
FHSSInvestigationBundleService::SubmitExport(
    const nlohmann::json &request, std::string_view idempotency_key) {
  return Submit("export", request, idempotency_key);
}

FHSSInvestigationBundleService::Result
FHSSInvestigationBundleService::SubmitValidation(
    const nlohmann::json &request, std::string_view idempotency_key) {
  return Submit("validate", request, idempotency_key);
}

FHSSInvestigationBundleService::Result
FHSSInvestigationBundleService::SubmitReplay(
    const nlohmann::json &request, std::string_view idempotency_key) {
  return Submit("replay", request, idempotency_key);
}

FHSSInvestigationBundleService::Result FHSSInvestigationBundleService::Submit(
    std::string kind, const nlohmann::json &request,
    std::string_view idempotency_key) {
  if (!request.is_object())
    return {400, Problem(400, "invalid_investigation_request",
                         "request must be an object")};
  const std::set<std::string> common{"request_id", "bundle_name",
                                     "timeout_ms"};
  auto allowed = common;
  if (kind == "export") {
    allowed.insert("job_id");
    allowed.insert("iq_mode");
    allowed.insert("confirm_copy");
  }
  for (const auto &[key, unused] : request.items()) {
    (void)unused;
    if (!allowed.contains(key))
      return {400, Problem(400, "unknown_request_field",
                           "request contains an unsupported field")};
  }
  if (!request.contains("request_id") || !request.at("request_id").is_string() ||
      !request.contains("bundle_name") || !request.at("bundle_name").is_string())
    return {400, Problem(400, "invalid_investigation_identifier",
                         "request_id and bundle_name must be strings")};
  const auto request_id = request.at("request_id").get<std::string>();
  const auto bundle_name = request.at("bundle_name").get<std::string>();
  if (!SafeComponent(request_id) || !SafeComponent(bundle_name) ||
      !SafeComponent(idempotency_key))
    return {400, Problem(400, "invalid_investigation_identifier",
                         "request, bundle, and idempotency identifiers must be bounded tokens")};
  if (kind == "export") {
    if (!request.contains("job_id") || !request.at("job_id").is_string() ||
        (request.contains("iq_mode") && !request.at("iq_mode").is_string()) ||
        (request.contains("confirm_copy") &&
         !request.at("confirm_copy").is_boolean()))
      return {400, Problem(400, "invalid_export_request",
                           "job_id, iq_mode, or confirm_copy has the wrong type")};
    const auto job_id = request.at("job_id").get<std::string>();
    const auto mode = request.contains("iq_mode")
                          ? request.at("iq_mode").get<std::string>()
                          : std::string{"reference"};
    if (!SafeComponent(job_id) || (mode != "reference" && mode != "copy"))
      return {400, Problem(400, "invalid_export_request",
                           "job_id or iq_mode is invalid")};
    if (mode == "copy" &&
        (!request.contains("confirm_copy") ||
         request.at("confirm_copy").get<bool>() != true)) {
      auto problem = Problem(428, "copy_confirmation_required",
                             "copied IQ requires explicit confirmation");
      if (const auto source = job_controller_->GetInvestigationSource(job_id)) {
        const auto artifacts = source->job.value("artifacts", nlohmann::json::object());
        if (artifacts.contains("iq") && artifacts.at("iq").is_object() &&
            artifacts.at("iq").contains("bytes"))
          problem["estimated_iq_bytes"] = artifacts.at("iq").at("bytes");
      }
      problem["maximum_copied_iq_bytes"] = kMaxCopiedIqBytes;
      problem["confirmation_field"] = "confirm_copy";
      return {428, std::move(problem)};
    }
  }
  std::uint64_t timeout_ms = kDefaultTimeout.count();
  if (request.contains("timeout_ms")) {
    const auto parsed = Unsigned(request.at("timeout_ms"));
    if (!parsed) return {400, Problem(400, "invalid_operation_timeout",
                                      "timeout_ms must be an unsigned integer")};
    timeout_ms = *parsed;
  }
  if (timeout_ms < static_cast<std::uint64_t>(kCheckpointBound.count()) ||
      timeout_ms > static_cast<std::uint64_t>(kMaxTimeout.count()))
    return {400, Problem(400, "invalid_operation_timeout",
                         "operation timeout is outside the bounded range")};
  const auto canonical = kind + ":" + CanonicalJson(request);
  const auto key_digest = Digest(idempotency_key);
  std::lock_guard lock(mutex_);
  if (shutting_down_)
    return {503, Problem(503, "investigation_service_shutting_down",
                         "service is shutting down")};
  if (const auto found = idempotency_.find(key_digest);
      found != idempotency_.end()) {
    if (found->second.canonical_request != canonical)
      return {409,
              Problem(409, "idempotency_key_reused_with_different_payload",
                      "idempotency key is bound to another request")};
    if (const auto existing = found->second.operation.lock())
      return {200, OperationJson(*existing)};
  }
  PurgeUnlocked();
  if (operations_.size() >= kMaxOperations)
    return {429, Problem(429, "operation_history_full",
                         "bounded operation history is full")};
  auto operation = std::make_shared<Operation>();
  operation->sequence = next_sequence_++;
  operation->operation_id =
      "op-" + Digest(canonical + ":" + std::to_string(operation->sequence))
                    .substr(0, 24);
  operation->kind = std::move(kind);
  operation->canonical_request = canonical;
  operation->idempotency_digest = key_digest;
  operation->request = request;
  operation->created_at = NowRfc3339();
  operation->deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(static_cast<std::int64_t>(timeout_ms));
  operations_.push_back(operation);
  queue_.push_back(operation);
  idempotency_[key_digest] = {canonical, operation};
  cv_.notify_all();
  return {202, OperationJson(*operation)};
}

FHSSInvestigationBundleService::Result
FHSSInvestigationBundleService::Get(std::string_view operation_id) const {
  std::lock_guard lock(mutex_);
  const auto found =
      std::ranges::find(operations_, operation_id, &Operation::operation_id);
  if (found == operations_.end())
    return {404, Problem(404, "operation_not_found", "operation not found")};
  return {200, OperationJson(**found)};
}

FHSSInvestigationBundleService::Result FHSSInvestigationBundleService::List()
    const {
  std::lock_guard lock(mutex_);
  nlohmann::json entries = nlohmann::json::array();
  for (auto iterator = operations_.rbegin(); iterator != operations_.rend();
       ++iterator)
    entries.push_back(OperationJson(**iterator));
  return {200,
          {{"schema", "graphx.dashboard.fhss_investigation_operations.v1"},
           {"entries", std::move(entries)},
           {"bounds", {{"max_entries", kMaxOperations}}}}};
}

FHSSInvestigationBundleService::Result
FHSSInvestigationBundleService::Cancel(std::string_view operation_id) {
  std::lock_guard lock(mutex_);
  const auto found =
      std::ranges::find(operations_, operation_id, &Operation::operation_id);
  if (found == operations_.end())
    return {404, Problem(404, "operation_not_found", "operation not found")};
  auto &operation = **found;
  if (operation.state == "completed" || operation.state == "cancelled" ||
      operation.state == "failed" || operation.state == "timed_out")
    return {200, OperationJson(operation)};
  if (operation.publication_committed)
    return {200, OperationJson(operation)};
  operation.cancel_requested = true;
  if (operation.state == "queued") {
    std::erase(queue_, *found);
    operation.state = "cancelled";
    operation.terminal_code = "cancelled_before_start";
    operation.terminal_detail =
        "queued operation cancelled without artifact work";
    operation.terminal_at = NowRfc3339();
    operation.result = nullptr;
  } else {
    operation.state = "cancelling";
  }
  cv_.notify_all();
  return {202, OperationJson(operation)};
}

FHSSInvestigationBundleService::Result
FHSSInvestigationBundleService::Quota() const {
  std::lock_guard lock(mutex_);
  std::uint64_t retained_bytes = 0;
  try {
    auto root = OpenRoot(bundle_root_);
    retained_bytes = RetainedBundleBytesAt(root.Get());
  } catch (...) {
    retained_bytes = kMaxRetainedBundleBytes;
  }
  return {200,
          {{"schema", "graphx.dashboard.fhss_investigation_quota.v1"},
           {"active_operations", active_operation_.expired() ? 0 : 1},
           {"retained_operations", operations_.size()},
           {"retained_bundle_bytes", retained_bytes},
           {"remaining_bundle_bytes",
            retained_bytes >= kMaxRetainedBundleBytes
                ? 0
                : kMaxRetainedBundleBytes - retained_bytes},
           {"limits",
            {{"concurrent_operations", 1},
             {"operations", kMaxOperations},
             {"bundles", kMaxBundles},
             {"artifacts_per_bundle", kMaxArtifacts},
             {"json_bytes", kMaxJsonBytes},
             {"copied_iq_bytes", kMaxCopiedIqBytes},
             {"referenced_iq_bytes", kMaxReferencedIqBytes},
             {"retained_bundle_bytes", kMaxRetainedBundleBytes},
             {"chunk_bytes", kChunkBytes},
             {"checkpoint_bound_ms", kCheckpointBound.count()}}}}};
}

void FHSSInvestigationBundleService::Worker(std::stop_token stop_token) {
  while (!stop_token.stop_requested()) {
    std::shared_ptr<Operation> operation;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, stop_token,
               [this] { return shutting_down_ || !queue_.empty(); });
      if (shutting_down_ || stop_token.stop_requested())
        break;
      operation = queue_.front();
      queue_.pop_front();
      active_operation_ = operation;
    }
    Process(operation, stop_token);
    std::lock_guard lock(mutex_);
    active_operation_.reset();
  }
}

bool FHSSInvestigationBundleService::CancelledOrTimedOut(
    const Operation &operation, std::stop_token stop_token) const {
  return operation.cancel_requested || stop_token.stop_requested() ||
         std::chrono::steady_clock::now() >= operation.deadline;
}

void FHSSInvestigationBundleService::Process(
    const std::shared_ptr<Operation> &operation, std::stop_token stop_token) {
  try {
    bool cancelled_before_start = false;
    {
      std::lock_guard lock(mutex_);
      if (operation->cancel_requested) {
        cancelled_before_start = true;
      } else {
        operation->started_at = NowRfc3339();
      }
    }
    if (cancelled_before_start) {
      Terminal(operation, "cancelled", "cancelled_before_start",
               "operation cancelled before validation");
      return;
    }
    if (test_hooks_ && test_hooks_->before_processing)
      test_hooks_->before_processing();
    if (operation->kind == "export")
      Export(operation, stop_token);
    else if (operation->kind == "validate") {
      auto validated = ValidateBundle(operation, stop_token);
      if (validated.iq_fd >= 0)
        ::close(validated.iq_fd);
      Terminal(operation, "completed", "bundle_validated",
               "bundle passed ordered validation",
               {{"schema",
                 "graphx.dashboard.fhss_investigation_validation_result.v1"},
                {"bundle_name", validated.bundle_name},
                {"iq_mode", validated.iq_mode},
                {"datatype", validated.datatype},
                {"iq_bytes", validated.iq_bytes},
                {"sample_count", validated.sample_count},
                {"iq_sha512", validated.iq_sha512},
                {"manifest_sha256", validated.manifest_sha256},
                {"receiver_truth_access", "none"}});
    } else
      Replay(operation, stop_token);
  } catch (const std::system_error &error) {
    Terminal(operation, "failed",
             error.code().value() == ENOSPC ? "artifact_enospc"
                                            : "artifact_io_failed",
             error.what());
  } catch (const std::length_error &error) {
    Terminal(operation, "failed", "investigation_quota_exceeded",
             error.what());
  } catch (const std::exception &error) {
    const bool interrupted = std::string_view(error.what()) ==
                             "operation interrupted";
    bool cancelled = false;
    bool timed_out = false;
    {
      std::lock_guard lock(mutex_);
      cancelled = operation->cancel_requested || stop_token.stop_requested();
      timed_out = std::chrono::steady_clock::now() >= operation->deadline;
    }
    if (interrupted && cancelled)
      Terminal(operation, "cancelled", "operation_cancelled",
               "operation cancelled cooperatively");
    else if (interrupted && timed_out)
      Terminal(operation, "timed_out", "operation_timeout",
               "operation exceeded its deadline");
    else
      Terminal(operation, "failed", "investigation_validation_failed",
               error.what());
  }
}

void FHSSInvestigationBundleService::Export(
    const std::shared_ptr<Operation> &operation, std::stop_token stop_token) {
  const auto checkpoint = [&] {
    std::lock_guard lock(mutex_);
    return !CancelledOrTimedOut(*operation, stop_token);
  };
  Transition(operation, "validating_source");
  const auto job_id = operation->request.at("job_id").get<std::string>();
  const auto mode = operation->request.value("iq_mode", "reference");
  const auto bundle_name = operation->request.at("bundle_name").get<std::string>();
  const auto qualification_step = test_hooks_ && test_hooks_->qualification_sequence
                                      ? ++qualification_export_sequence_
                                      : 0;
  const auto qualification_pause = [&](std::uint64_t step) {
    if (qualification_step != step) return;
    const auto until = std::chrono::steady_clock::now() +
                       std::chrono::milliseconds(1500);
    while (std::chrono::steady_clock::now() < until) {
      if (!checkpoint()) throw std::runtime_error("operation interrupted");
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  };
  const auto source = test_hooks_ && test_hooks_->source_lookup
                          ? test_hooks_->source_lookup(job_id)
                          : job_controller_->GetInvestigationSource(job_id);
  if (!source)
    throw std::runtime_error("export source must be a completed retained job");

  auto iq_root = OpenRoot(iq_root_);
  auto source_directory = OpenDirectoryAt(iq_root.Get(), job_id);
  const auto datatype = source->job.at("sample_format").get<std::string>();
  const auto stride = DatatypeStride(datatype);
  if (stride == 0)
    throw std::runtime_error("source IQ datatype is unsupported");
  const std::string iq_name = datatype == "cf32_le" ? "iq.cf32" : "iq.cf64";
  FileIdentity source_identity;
  auto iq = OpenRegularAt(source_directory.Get(), iq_name,
                          mode == "copy" ? kMaxCopiedIqBytes
                                         : kMaxReferencedIqBytes,
                          &source_identity);
  Transition(operation, "hashing");
  // Qualification steps 4, 7, and 8 expose a bounded hashing checkpoint for
  // cancel, deadline, and process-shutdown tests respectively.
  if (qualification_step == 4 || qualification_step == 7 ||
      qualification_step == 8)
    qualification_pause(qualification_step);
  if (qualification_step == 1)
    throw std::length_error("injected bundle retention quota exceeded");
  if (qualification_step == 2)
    throw std::length_error("injected copied-IQ size limit exceeded");
  const auto iq_hashes = HashFd(iq.Get(), mode == "copy" ? kMaxCopiedIqBytes
                                                           : kMaxReferencedIqBytes,
                                checkpoint);
  const auto &committed_artifacts = source->job.at("artifacts");
  const auto &committed_iq = committed_artifacts.at("iq");
  if (committed_iq.value("relative_path", "") != iq_name ||
      Unsigned(committed_iq.at("bytes")) != iq_hashes.bytes ||
      committed_iq.value("sha256", "") != iq_hashes.sha256)
    throw std::runtime_error("source IQ does not match completed job manifest");
  if (iq_hashes.bytes == 0 || iq_hashes.bytes % stride != 0)
    throw std::runtime_error("IQ byte length does not match datatype stride");
  if (test_hooks_ && test_hooks_->after_hashing) test_hooks_->after_hashing();
  if (test_hooks_ && test_hooks_->source_identity_change)
    test_hooks_->source_identity_change();
  FileIdentity current_source_identity;
  auto current_source = OpenRegularAt(
      source_directory.Get(), iq_name, kMaxReferencedIqBytes,
      &current_source_identity);
  if (!(IdentityOf(iq.Get(), kMaxReferencedIqBytes) == source_identity) ||
      !(current_source_identity == source_identity))
    throw std::runtime_error("source IQ identity changed during export");

  struct CommittedJson {
    UniqueFd file;
    FileIdentity identity;
    nlohmann::json document;
  };
  const auto read_committed_json = [&](std::string_view key,
                                       std::string_view expected_name) {
    const auto &record = committed_artifacts.at(key);
    if (record.value("relative_path", "") != expected_name)
      throw std::runtime_error("completed job artifact path is inconsistent");
    FileIdentity identity;
    auto file = OpenRegularAt(source_directory.Get(), expected_name,
                              kMaxJsonBytes, &identity);
    const auto text = ReadTextFd(file.Get(), kMaxJsonBytes);
    if (Digest(text) != record.value("sha256", ""))
      throw std::runtime_error("source document does not match completed job manifest");
    nlohmann::json document;
    try { document = nlohmann::json::parse(text); }
    catch (...) { throw std::runtime_error("committed source JSON is malformed"); }
    return CommittedJson{std::move(file), identity, std::move(document)};
  };
  auto committed_truth = read_committed_json("truth", "truth.withheld.json");
  auto committed_sigmf = read_committed_json("sigmf", "iq.sigmf-meta");
  auto committed_receiver =
      read_committed_json("receiver_config", "receiver-minimal.json");
  auto truth = committed_truth.document;
  auto sigmf = committed_sigmf.document;
  auto receiver_graph = committed_receiver.document;
  if (ContainsForbiddenReceiverKey(receiver_graph))
    throw std::runtime_error("receiver configuration contains generator truth");
  std::string sigmf_error;
  if (!ValidateSigMf(sigmf, iq_hashes.bytes, iq_hashes.sha512, &sigmf_error))
    throw std::runtime_error(sigmf_error);
  sigmf["global"]["core:sha512"] = iq_hashes.sha512;
  sigmf["global"].erase("core:metadata_only");

  auto bundle_root = OpenRoot(bundle_root_);
  const auto existing = DirectoryNames(bundle_root.Get());
  if (RetainedBundleBytesAt(bundle_root.Get()) >= kMaxRetainedBundleBytes)
    throw std::length_error("aggregate bundle retention quota is exhausted");
  if (existing.contains(bundle_name))
    throw std::runtime_error("bundle name collision; overwrite is forbidden");
  if (std::ranges::count_if(existing, [](const auto &name) {
        return !name.starts_with(".tmp-");
      }) >= static_cast<std::ptrdiff_t>(kMaxBundles))
    throw std::length_error("bundle retention count exceeds quota");
  const auto temporary_name = ".tmp-" + operation->operation_id;
  if (::mkdirat(bundle_root.Get(), temporary_name.c_str(), 0700) != 0)
    throw std::runtime_error("private bundle staging directory collision");
  bool published = false;
  try {
    auto temporary = OpenDirectoryAt(bundle_root.Get(), temporary_name);
    struct Entry { std::string name; std::string classification; FileDigests digest; };
    std::vector<Entry> entries;
    std::uint64_t retained_bytes = 0;
    const auto add_json = [&](std::string name, std::string classification,
                              const nlohmann::json &document) {
      const auto text = CanonicalJson(document);
      if (text.size() > kMaxJsonBytes)
        throw std::length_error("bundle JSON artifact exceeds quota");
      auto digest = WriteTextAt(temporary.Get(), name, text);
      retained_bytes += digest.bytes;
      entries.push_back({std::move(name), std::move(classification),
                         std::move(digest)});
    };

    add_json("truth.json", "generator_truth", truth);
    add_json("observation.json", "receiver_observation", source->observation);
    add_json("comparison.json", "offline_evaluation", source->comparison);
    add_json("receiver-config.json", "receiver_input",
             {{"schema", "graphx.dashboard.receiver_graph.v1"},
              {"config_revision", source->job.value("config_revision", 0)},
              {"etag", source->job.value("config_etag", "")},
              {"graph", receiver_graph}});
    add_json("receiver-result.json", "receiver_result",
             {{"schema", "graphx.dashboard.fhss_receiver_result.v1"},
              {"result", source->receiver_result},
              {"semantic_sha256", SemanticReceiverResultHash(source->receiver_result)}});
    add_json("recording.sigmf-meta", "signal_metadata", sigmf);
    add_json("provenance.json", "provenance",
             {{"schema", "graphx.dashboard.fhss_investigation_provenance.v1"},
              {"created_at", NowRfc3339()},
              {"synthetic_only", true}, {"hwil", "unavailable"},
              {"production_rf_qualification", "not_qualified"},
              {"source_job_id", job_id},
              {"source_job_request_id", source->job.value("request_id", "")},
              {"controller_epoch", RequiredUnsignedField(source->job, "controller_epoch")},
              {"graph_generation", RequiredUnsignedField(source->job, "graph_generation")},
              {"run_epoch", RequiredUnsignedField(source->job, "run_epoch")},
              {"scenario_correlation_id",
               source->job.value("scenario_correlation_id", "")},
              {"source_config_revision", RequiredUnsignedField(source->job, "config_revision")},
              {"source_config_etag", source->job.value("config_etag", "")},
              {"sample_format", datatype},
              {"sample_count", iq_hashes.bytes / stride},
              {"sample_rate_hz", sigmf.at("global").at("core:sample_rate")},
              {"center_frequency_hz",
               sigmf.at("captures").at(0).at("core:frequency")},
              {"producer", "graphx-dsp-fhss-demo"},
              {"build", {{"language_standard", "C++26"},
                          {"compiler", __VERSION__},
                          {"platform", "local-host"}}},
              {"api_version", "v1"},
              {"bundle_schema_version", 1},
              {"sigmf_core_version", "1.2.6"},
              {"publisher_epoch", nullptr},
              {"event_sequence_range", nullptr}});
    add_json("actions.json", "operator_actions",
             {{"schema", "graphx.dashboard.fhss_operator_actions.v1"},
              {"actions", nlohmann::json::array({
                   {{"sequence", 1}, {"action", "export"},
                   {"request_id", operation->request.value("request_id", "")},
                    {"timestamp", NowRfc3339()}}})}});
    const auto executable_checkpoint = [&] {
      if (test_hooks_ && test_hooks_->executable_hash_checkpoint)
        test_hooks_->executable_hash_checkpoint();
      return checkpoint();
    };
    auto build_api = BuildApiManifest(executable_checkpoint);
    PinnedSchemas().Validate(
        build_api, "urn:graphx:dashboard:fhss-build-api-manifest:v1");
    add_json("build-api.json", "build_api_manifest", build_api);

    if (mode == "copy") {
      Transition(operation, "copying");
      qualification_pause(5);
      if ((test_hooks_ && test_hooks_->inject_enospc) || qualification_step == 3)
        throw std::system_error(ENOSPC, std::generic_category(), "injected disk full");
      auto copied = CopyFdToAt(iq.Get(), temporary.Get(), "recording.sigmf-data",
                               kMaxCopiedIqBytes, checkpoint);
      if (copied.sha256 != iq_hashes.sha256 || copied.sha512 != iq_hashes.sha512 ||
          copied.bytes != iq_hashes.bytes)
        throw std::runtime_error("copied IQ changed during publication");
      FileIdentity copied_source_identity;
      auto copied_source = OpenRegularAt(source_directory.Get(), iq_name,
                                          kMaxReferencedIqBytes,
                                          &copied_source_identity);
      if (!(IdentityOf(iq.Get(), kMaxReferencedIqBytes) == source_identity) ||
          !(copied_source_identity == source_identity))
        throw std::runtime_error("source IQ identity changed during copy");
      retained_bytes += copied.bytes;
      entries.push_back({"recording.sigmf-data", "raw_iq", copied});
      if (test_hooks_ && test_hooks_->after_copying) test_hooks_->after_copying();
    } else {
      add_json("external-iq-reference.json", "external_iq_reference",
               {{"schema", "graphx.dashboard.fhss_external_iq_reference.v1"},
                {"approved_root_id", "fhss-jobs"},
                {"relative_components", nlohmann::json::array({job_id, iq_name})},
                {"bytes", iq_hashes.bytes}, {"sha256", iq_hashes.sha256},
                {"sha512", iq_hashes.sha512},
                {"identity", {{"device", source_identity.device},
                              {"inode", source_identity.inode}}}});
    }
    if (entries.size() > kMaxArtifacts || retained_bytes > kMaxRetainedBundleBytes)
      throw std::length_error("bundle artifact quota exceeded");

    nlohmann::json artifacts = nlohmann::json::array();
    for (const auto &entry : entries) {
      const bool receiver_visible = entry.classification == "receiver_input" ||
                                    entry.classification == "raw_iq";
      artifacts.push_back({{"path", entry.name},
                           {"schema", ArtifactSchema(entry.name)},
                           {"media_type", entry.name.ends_with(".json") ||
                                                  entry.name.ends_with("-meta")
                                              ? "application/json"
                                              : "application/octet-stream"},
                           {"classification", entry.classification},
                           {"retention_class", "bundle"},
                           {"receiver_visible", receiver_visible},
                           {"evaluator_visible", true},
                           {"replay_use", receiver_visible},
                           {"storage", entry.name == "recording.sigmf-data"
                                           ? "copied"
                                           : "bundled"},
                           {"bytes", entry.digest.bytes},
                           {"sha256", entry.digest.sha256},
                           {"sha512", entry.digest.sha512}});
    }
    nlohmann::json manifest{
        {"schema", "graphx.dashboard.fhss_investigation_manifest.v1"},
        {"bundle_name", bundle_name}, {"bundle_format_version", 1},
        {"created_at", NowRfc3339()}, {"iq_mode", mode},
        {"self_contained", mode == "copy"}, {"synthetic_only", true},
        {"receiver_truth_access", "none"},
        {"source_job_id", job_id},
        {"source_job_request_id", source->job.value("request_id", "")},
        {"controller_epoch", RequiredUnsignedField(source->job, "controller_epoch")},
        {"scenario_correlation_id",
         source->job.value("scenario_correlation_id", "")},
        {"datatype", datatype},
        {"iq_bytes", iq_hashes.bytes}, {"sample_count", iq_hashes.bytes / stride},
        {"iq_sha256", iq_hashes.sha256}, {"iq_sha512", iq_hashes.sha512},
        {"expected_receiver_result_sha256",
         SemanticReceiverResultHash(source->receiver_result)},
        {"artifacts", std::move(artifacts)},
        {"manifest_integrity",
         {{"algorithm", "sha256"}, {"detached_file", "manifest.sha256"},
          {"scope", "exact canonical manifest.json bytes"}}}};
    Transition(operation, "publishing");
    qualification_pause(6);
    const auto manifest_text = CanonicalJson(manifest);
    const auto manifest_digest = WriteTextAt(temporary.Get(), "manifest.json", manifest_text);
    (void)WriteTextAt(temporary.Get(), "manifest.sha256", manifest_digest.sha256 + "\n");
    retained_bytes += manifest_digest.bytes + 65;
    if (retained_bytes > kMaxRetainedBundleBytes)
      throw std::length_error("bundle retention byte quota exceeded");
    const auto aggregate_retained = RetainedBundleBytesAt(bundle_root.Get());
    if (aggregate_retained > kMaxRetainedBundleBytes - retained_bytes)
      throw std::length_error("aggregate bundle retention quota exceeded");
    if (::fsync(temporary.Get()) != 0)
      throw std::runtime_error("bundle directory synchronization failed");
    if (test_hooks_ && test_hooks_->before_publish) test_hooks_->before_publish();
    const nlohmann::json result{
        {"schema", "graphx.dashboard.fhss_investigation_export_result.v1"},
        {"bundle_name", bundle_name}, {"iq_mode", mode},
        {"self_contained", mode == "copy"},
        {"manifest_sha256", manifest_digest.sha256},
        {"iq_sha512", iq_hashes.sha512}, {"iq_bytes", iq_hashes.bytes},
        {"datatype", datatype}, {"sample_count", iq_hashes.bytes / stride}};
    // Atomic rename is the point of no return. The service lock covers only
    // the cancellation check, rename, and state marker; durability I/O remains
    // outside so Get/List/Cancel/Shutdown are never held behind fsync.
    {
      std::lock_guard lock(mutex_);
      if (CancelledOrTimedOut(*operation, stop_token))
        throw std::runtime_error("operation interrupted");
      const auto publish_retained = RetainedBundleBytesAt(bundle_root.Get());
      if (publish_retained > kMaxRetainedBundleBytes - retained_bytes)
        throw std::length_error("aggregate bundle retention quota changed before publication");
      RenameDirectoryNoReplace(bundle_root.Get(), temporary_name,
                               bundle_root.Get(), bundle_name);
      published = true;
      operation->publication_committed = true;
      operation->state = "completed";
      operation->terminal_code = "bundle_exported";
      operation->terminal_detail = "investigation bundle published atomically";
      operation->terminal_at = NowRfc3339();
      operation->result = result;
    }
    if (test_hooks_ && test_hooks_->after_publish_rename)
      test_hooks_->after_publish_rename();
    if (::fsync(bundle_root.Get()) != 0) {
      // The fully synchronized staging directory was already atomically
      // published. A root-directory sync failure cannot safely roll back a
      // name that is externally visible, so retain the completed bundle.
      // Filesystem durability across sudden host power loss is consequently
      // not claimed when the host rejects this advisory sync.
    }
  } catch (...) {
    if (!published) RemovePrivateDirectoryAt(bundle_root.Get(), temporary_name);
    throw;
  }
}

FHSSInvestigationBundleService::ValidatedBundle
FHSSInvestigationBundleService::ValidateBundle(
    const std::shared_ptr<Operation> &operation, std::stop_token stop_token) {
  const auto checkpoint = [&] {
    std::lock_guard lock(mutex_);
    return !CancelledOrTimedOut(*operation, stop_token);
  };
  Transition(operation, "validating");
  const auto bundle_name = operation->request.at("bundle_name").get<std::string>();
  auto bundle_root = OpenRoot(bundle_root_);
  auto bundle = OpenDirectoryAt(bundle_root.Get(), bundle_name);
  const auto names = DirectoryNames(bundle.Get());
  auto manifest_file = OpenRegularAt(bundle.Get(), "manifest.json", kMaxJsonBytes);
  const auto manifest_text = ReadTextFd(manifest_file.Get(), kMaxJsonBytes);
  nlohmann::json manifest;
  try { manifest = nlohmann::json::parse(manifest_text); }
  catch (...) { throw std::runtime_error("manifest JSON is malformed"); }
  if (CanonicalJson(manifest) != manifest_text)
    throw std::runtime_error("manifest is not in canonical encoding");
  PinnedSchemas().Validate(
      manifest, "urn:graphx:dashboard:fhss-investigation-manifest:v1");
  if (!manifest.is_object() ||
      manifest.value("schema", "") != "graphx.dashboard.fhss_investigation_manifest.v1" ||
      manifest.value("bundle_name", "") != bundle_name ||
      manifest.value("bundle_format_version", 0) != 1 ||
      !manifest.contains("artifacts") || !manifest.at("artifacts").is_array() ||
      manifest.at("artifacts").size() > kMaxArtifacts)
    throw std::runtime_error("manifest contract is invalid");
  const auto manifest_sha256 = Digest(manifest_text);
  auto detached = OpenRegularAt(bundle.Get(), "manifest.sha256", 129);
  if (ReadTextFd(detached.Get(), 129) != manifest_sha256 + "\n")
    throw std::runtime_error("detached manifest hash mismatch");

  std::set<std::string> expected{"manifest.json", "manifest.sha256"};
  struct HeldArtifact {
    std::string path;
    UniqueFd file;
    FileIdentity identity;
    FileDigests digests;
    std::optional<nlohmann::json> document;
  };
  std::vector<HeldArtifact> held;
  std::unordered_map<std::string, std::size_t> held_index;
  std::uint64_t retained = manifest_text.size() + 65;
  for (const auto &entry : manifest.at("artifacts")) {
    PinnedSchemas().Validate(
        entry, "urn:graphx:dashboard:fhss-investigation-artifact-entry:v1");
    if (!entry.is_object()) throw std::runtime_error("artifact entry is invalid");
    const auto path = entry.value("path", "");
    if (!SafeComponent(path) || !expected.insert(path).second)
      throw std::runtime_error("artifact path is unsafe or duplicated");
    if (entry.value("schema", "") != ArtifactSchema(path) ||
        !entry.contains("classification") ||
        !entry.at("classification").is_string() ||
        !entry.contains("media_type") || !entry.at("media_type").is_string() ||
        !entry.contains("receiver_visible") ||
        !entry.at("receiver_visible").is_boolean() ||
        !entry.contains("evaluator_visible") ||
        !entry.at("evaluator_visible").is_boolean() ||
        !entry.contains("replay_use") || !entry.at("replay_use").is_boolean() ||
        !entry.contains("storage") || !entry.at("storage").is_string())
      throw std::runtime_error("artifact semantic contract is invalid");
    const auto receiver_visible =
        entry.at("classification") == "receiver_input" ||
        entry.at("classification") == "raw_iq";
    if (entry.at("receiver_visible").get<bool>() != receiver_visible ||
        entry.at("replay_use").get<bool>() != receiver_visible)
      throw std::runtime_error("artifact receiver visibility is invalid");
    const auto bytes = Unsigned(entry.at("bytes"));
    if (!bytes || *bytes > kMaxRetainedBundleBytes)
      throw std::runtime_error("artifact byte declaration is invalid");
    FileIdentity identity;
    auto file = OpenRegularAt(bundle.Get(), path, kMaxRetainedBundleBytes,
                              &identity);
    auto digests = HashFd(file.Get(), kMaxRetainedBundleBytes, checkpoint);
    if (digests.bytes != *bytes || digests.sha256 != entry.value("sha256", "") ||
        digests.sha512 != entry.value("sha512", ""))
      throw std::runtime_error("artifact length or digest mismatch");
    retained += digests.bytes;
    if (retained > kMaxRetainedBundleBytes)
      throw std::length_error("retained bundle exceeds validation quota");
    std::optional<nlohmann::json> document;
    if (entry.at("media_type") == "application/json") {
      const auto text = ReadTextFd(file.Get(), kMaxJsonBytes);
      try { document = nlohmann::json::parse(text); }
      catch (...) { throw std::runtime_error("JSON artifact is malformed"); }
    }
    held_index.emplace(path, held.size());
    held.push_back({path, std::move(file), identity, std::move(digests),
                    std::move(document)});
  }
  if (names != expected)
    throw std::runtime_error("bundle contains missing or undeclared artifacts");

  const auto mode = manifest.value("iq_mode", "");
  if ((mode != "copy" && mode != "reference") ||
      manifest.value("self_contained", false) != (mode == "copy"))
    throw std::runtime_error("bundle IQ mode declaration is inconsistent");
  const std::set<std::string> common_required{
      "manifest.json", "manifest.sha256", "truth.json", "observation.json",
      "comparison.json", "receiver-config.json", "receiver-result.json",
      "recording.sigmf-meta", "provenance.json", "actions.json",
      "build-api.json"};
  auto required = common_required;
  required.insert(mode == "copy" ? "recording.sigmf-data"
                                  : "external-iq-reference.json");
  if (expected != required)
    throw std::runtime_error("bundle required artifact inventory is incomplete");
  const auto datatype = manifest.value("datatype", "");
  const auto stride = DatatypeStride(datatype);
  const auto iq_bytes = Unsigned(manifest.at("iq_bytes"));
  const auto sample_count = Unsigned(manifest.at("sample_count"));
  if (stride == 0 || !iq_bytes || !sample_count || *iq_bytes % stride != 0 ||
      *sample_count != *iq_bytes / stride)
    throw std::runtime_error("manifest datatype, length, or sample count mismatch");

  UniqueFd iq;
  std::optional<FileIdentity> reference_identity;
  nlohmann::json reference;
  if (mode == "copy") {
    const auto duplicate = ::dup(held.at(held_index.at("recording.sigmf-data")).file.Get());
    if (duplicate < 0) throw std::runtime_error("IQ descriptor duplication failed");
    iq = UniqueFd(duplicate);
  } else {
    reference = *held.at(held_index.at("external-iq-reference.json")).document;
    PinnedSchemas().Validate(
        reference, "urn:graphx:dashboard:fhss-external-iq-reference:v1");
    if (reference.value("schema", "") != "graphx.dashboard.fhss_external_iq_reference.v1" ||
        reference.value("approved_root_id", "") != "fhss-jobs" ||
        !reference.contains("relative_components") ||
        !reference.at("relative_components").is_array() ||
        reference.at("relative_components").size() != 2)
      throw std::runtime_error("external IQ reference contract is invalid");
    const auto first = reference.at("relative_components").at(0).get<std::string>();
    const auto second = reference.at("relative_components").at(1).get<std::string>();
    auto root = OpenRoot(iq_root_);
    auto directory = OpenDirectoryAt(root.Get(), first);
    FileIdentity identity;
    iq = OpenRegularAt(directory.Get(), second, kMaxReferencedIqBytes, &identity);
    reference_identity = identity;
  }
  auto iq_digests = HashFd(iq.Get(), mode == "copy" ? kMaxCopiedIqBytes
                                                      : kMaxReferencedIqBytes,
                               checkpoint);
  if (iq_digests.bytes != *iq_bytes ||
      iq_digests.sha256 != manifest.value("iq_sha256", "") ||
      iq_digests.sha512 != manifest.value("iq_sha512", ""))
    throw std::runtime_error("IQ dataset does not match manifest");
  if (mode == "reference") {
    if (Unsigned(reference.at("bytes")) != iq_digests.bytes ||
        reference.value("sha256", "") != iq_digests.sha256 ||
        reference.value("sha512", "") != iq_digests.sha512 ||
        !reference.contains("identity") || !reference.at("identity").is_object() ||
        Unsigned(reference.at("identity").at("device")) !=
            static_cast<std::uint64_t>(reference_identity->device) ||
        Unsigned(reference.at("identity").at("inode")) !=
            static_cast<std::uint64_t>(reference_identity->inode) ||
        !(IdentityOf(iq.Get(), kMaxReferencedIqBytes) == *reference_identity))
      throw std::runtime_error("external IQ reference identity or digest mismatch");
  }
  if (test_hooks_ && test_hooks_->after_bundle_artifacts_hashed)
    test_hooks_->after_bundle_artifacts_hashed();
  // Bind every later semantic read to the exact descriptor bytes that were
  // hashed, and also reject a path replacement or in-place mutation before
  // receiver construction.
  for (auto &artifact : held) {
    FileIdentity current_identity;
    auto current = OpenRegularAt(bundle.Get(), artifact.path,
                                 kMaxRetainedBundleBytes, &current_identity);
    const auto current_digests = HashFd(artifact.file.Get(),
                                       kMaxRetainedBundleBytes, checkpoint);
    if (!(current_identity == artifact.identity) ||
        !(IdentityOf(artifact.file.Get(), kMaxRetainedBundleBytes) ==
          artifact.identity) || current_digests.bytes != artifact.digests.bytes ||
        current_digests.sha256 != artifact.digests.sha256 ||
        current_digests.sha512 != artifact.digests.sha512)
      throw std::runtime_error("artifact identity changed during validation");
  }
  auto sigmf = *held.at(held_index.at("recording.sigmf-meta")).document;
  std::string sigmf_error;
  if (!ValidateSigMf(sigmf, *iq_bytes, iq_digests.sha512, &sigmf_error))
    throw std::runtime_error(sigmf_error);
  const auto truth = *held.at(held_index.at("truth.json")).document;
  const auto observation = *held.at(held_index.at("observation.json")).document;
  const auto comparison = *held.at(held_index.at("comparison.json")).document;
  PinnedSchemas().Validate(truth, "urn:graphx:fhss:iq-truth:v1");
  PinnedSchemas().Validate(
      observation, "urn:graphx:dashboard:fhss-receiver-observation:v1");
  PinnedSchemas().Validate(
      comparison, "urn:graphx:dashboard:fhss-comparison-result:v1");
  if (truth.value("schema", "") != "graphx.fhss.iq-truth.v1" ||
      observation.value("schema", "") !=
          "graphx.dashboard.fhss_receiver_observation.v1" ||
      comparison.value("schema", "") !=
          "graphx.dashboard.fhss_comparison_result.v1")
    throw std::runtime_error("separated evidence schema identity is invalid");
  auto receiver = *held.at(held_index.at("receiver-config.json")).document;
  PinnedSchemas().Validate(receiver,
                           "urn:graphx:dashboard:fhss-receiver-graph:v1");
  if (receiver.value("schema", "") != "graphx.dashboard.receiver_graph.v1" ||
      !receiver.contains("graph") || ContainsForbiddenReceiverKey(receiver.at("graph")))
    throw std::runtime_error("receiver configuration violates truth isolation");
  const auto expected_hash = manifest.value("expected_receiver_result_sha256", "");
  const auto receiver_result = *held.at(held_index.at("receiver-result.json")).document;
  PinnedSchemas().Validate(
      receiver_result, "urn:graphx:dashboard:fhss-receiver-result:v1");
  if (receiver_result.value("schema", "") !=
          "graphx.dashboard.fhss_receiver_result.v1" ||
      !receiver_result.contains("result") ||
      !receiver_result.at("result").is_object() || expected_hash.size() != 64 ||
      receiver_result.value("semantic_sha256", "") != expected_hash ||
      SemanticReceiverResultHash(receiver_result.at("result")) != expected_hash)
    throw std::runtime_error("expected semantic receiver hash is invalid");
  const auto provenance = *held.at(held_index.at("provenance.json")).document;
  PinnedSchemas().Validate(
      provenance, "urn:graphx:dashboard:fhss-investigation-provenance:v1");
  if (provenance.value("schema", "") !=
          "graphx.dashboard.fhss_investigation_provenance.v1" ||
      provenance.value("synthetic_only", false) != true ||
      provenance.value("hwil", "") != "unavailable")
    throw std::runtime_error("bundle provenance contract is invalid");
  const auto actions = *held.at(held_index.at("actions.json")).document;
  PinnedSchemas().Validate(actions,
                           "urn:graphx:dashboard:fhss-operator-actions:v1");
  if (actions.value("schema", "") !=
          "graphx.dashboard.fhss_operator_actions.v1" ||
      !actions.contains("actions") || !actions.at("actions").is_array() ||
      actions.at("actions").size() > 128)
    throw std::runtime_error("operator action log contract is invalid");
  const auto build_api = *held.at(held_index.at("build-api.json")).document;
  PinnedSchemas().Validate(
      build_api, "urn:graphx:dashboard:fhss-build-api-manifest:v1");
  std::map<std::string, std::string, std::less<>> declared_schemas;
  for (const auto &entry : build_api.at("schemas"))
    if (!declared_schemas.emplace(entry.at("path").get<std::string>(),
                                  entry.at("sha256").get<std::string>()).second)
      throw std::runtime_error("build/API schema inventory is duplicated");
  for (const auto &[path, digest] : kPinnedSchemaDigests)
    if (declared_schemas[std::string(path)] != digest)
      throw std::runtime_error("build/API pinned schema digest is incompatible");
  if (declared_schemas.size() != kPinnedSchemaDigests.size() ||
      build_api.at("openapi").value("sha256", "") != kPinnedOpenApiSha256 ||
      build_api.at("pinned_sigmf").value("sha256", "") !=
          declared_schemas["dashboard/sigmf/official-v1.2.6/sigmf-schema.json"])
    throw std::runtime_error("build/API dependency identity is incompatible");

  const auto agrees = [](const nlohmann::json &left, std::string_view left_key,
                         const nlohmann::json &right,
                         std::string_view right_key) {
    return left.contains(left_key) && right.contains(right_key) &&
           left.at(left_key) == right.at(right_key);
  };
  if (!agrees(truth, "sample_format", manifest, "datatype") ||
      !agrees(truth, "sample_count", manifest, "sample_count") ||
      !agrees(truth, "iq_sha256", manifest, "iq_sha256") ||
      !agrees(truth, "sample_rate_hz", sigmf.at("global"), "core:sample_rate") ||
      !agrees(provenance, "sample_format", manifest, "datatype") ||
      !agrees(provenance, "sample_count", manifest, "sample_count") ||
      !agrees(provenance, "source_job_id", manifest, "source_job_id") ||
      !agrees(provenance, "source_job_request_id", manifest,
              "source_job_request_id") ||
      !agrees(provenance, "controller_epoch", manifest, "controller_epoch") ||
      !agrees(provenance, "scenario_correlation_id", manifest,
              "scenario_correlation_id") ||
      !agrees(provenance, "sample_rate_hz", sigmf.at("global"), "core:sample_rate") ||
      !agrees(provenance, "center_frequency_hz", sigmf.at("captures").at(0),
              "core:frequency") ||
      !agrees(provenance, "graph_generation", observation, "generation") ||
      !agrees(provenance, "run_epoch", observation, "run_epoch") ||
      !agrees(provenance, "source_config_revision", observation,
              "config_revision") ||
      !agrees(provenance, "source_config_etag", observation, "config_etag") ||
      !agrees(provenance, "source_config_revision", receiver,
              "config_revision") ||
      !agrees(provenance, "source_config_etag", receiver, "etag") ||
      !agrees(provenance, "graph_generation", comparison, "generation") ||
      !agrees(provenance, "run_epoch", comparison, "run_epoch") ||
      comparison.at("config_identity").at("agrees") != true ||
      !agrees(provenance, "source_config_revision",
              comparison.at("config_identity"), "observed_config_revision") ||
      !agrees(provenance, "source_config_etag",
              comparison.at("config_identity"), "observed_config_etag"))
    throw std::runtime_error("bundle cross-artifact identity is inconsistent");
  return {.bundle_name = bundle_name, .iq_mode = mode, .datatype = datatype,
          .iq_bytes = *iq_bytes, .sample_count = *sample_count,
          .iq_sha256 = iq_digests.sha256, .iq_sha512 = iq_digests.sha512,
          .expected_semantic_hash = expected_hash,
          .manifest_sha256 = manifest_sha256,
          .receiver_graph = receiver.at("graph"), .iq_fd = iq.Release()};
}

void FHSSInvestigationBundleService::Replay(
    const std::shared_ptr<Operation> &operation, std::stop_token stop_token) {
  auto validated = ValidateBundle(operation, stop_token);
  UniqueFd iq(validated.iq_fd);
  validated.iq_fd = -1;
  if (test_hooks_ && test_hooks_->before_replay_construction)
    test_hooks_->before_replay_construction();
  {
    std::lock_guard lock(mutex_);
    if (CancelledOrTimedOut(*operation, stop_token))
      throw std::runtime_error("operation interrupted");
  }
  Transition(operation, "rebuilding_receiver");
  auto graph = validated.receiver_graph;
  auto source = std::ranges::find_if(graph.at("nodes"), [](const auto &node) {
    return node.value("id", "") == "source";
  });
  if (source == graph.at("nodes").end())
    throw std::runtime_error("receiver source node is missing");
  (*source)["node_config"]["file_path"] = "/dev/fd/" + std::to_string(iq.Get());
  (*source)["node_config"]["sample_format"] = validated.datatype;
  (*source)["node_config"]["max_read_complex_samples"] = validated.sample_count;
  if (ContainsForbiddenReceiverKey(graph))
    throw std::runtime_error("replay graph violates receiver truth isolation");
  const auto rebuild = runtime_session_->Rebuild(
      {.receiver_graph = graph,
       .config_revision = 0,
       .config_etag = "bundle-" + validated.manifest_sha256});
  if (rebuild.status_code >= 400)
    throw std::runtime_error("receiver rebuild failed: " + rebuild.message);
  const auto start = runtime_session_->Start();
  if (start.status_code >= 400)
    throw std::runtime_error("receiver start failed: " + start.message);
  Transition(operation, "running_receiver");
  for (;;) {
    bool interrupted = false;
    {
      std::lock_guard lock(mutex_);
      interrupted = CancelledOrTimedOut(*operation, stop_token);
    }
    if (interrupted) {
      if (runtime_session_->GetState() ==
          graph::dashboard::GraphRuntimeSession::State::running)
        (void)runtime_session_->Stop();
      throw std::runtime_error("operation interrupted");
    }
    const auto state = runtime_session_->GetState();
    if (state == graph::dashboard::GraphRuntimeSession::State::completed ||
        state == graph::dashboard::GraphRuntimeSession::State::failed)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  const auto status = runtime_session_->SnapshotStatus();
  if (status.state != graph::dashboard::GraphRuntimeSession::State::completed)
    throw std::runtime_error("receiver replay execution failed");
  const auto observation = observation_service_->ReceiverObservation().document;
  if (!observation.contains("receiver_message_result") ||
      !observation.at("receiver_message_result").is_object())
    throw std::runtime_error("receiver replay produced no semantic result");
  const auto actual = SemanticReceiverResultHash(
      observation.at("receiver_message_result"));
  if (actual != validated.expected_semantic_hash)
    throw std::runtime_error("receiver semantic result hash mismatch");
  Terminal(operation, "completed", "bundle_replayed",
           "validated bundle reproduced its receiver result",
           {{"schema", "graphx.dashboard.fhss_investigation_replay_result.v1"},
            {"bundle_name", validated.bundle_name},
            {"manifest_sha256", validated.manifest_sha256},
            {"semantic_receiver_result_sha256", actual},
            {"matches_expected", true}, {"receiver_truth_access", "none"},
            {"receiver_message_result", observation.at("receiver_message_result")}});
}

void FHSSInvestigationBundleService::Transition(
    const std::shared_ptr<Operation> &operation, std::string state) {
  std::lock_guard lock(mutex_);
  if (operation->cancel_requested && state != "cancelled")
    operation->state = "cancelling";
  else
    operation->state = std::move(state);
}

void FHSSInvestigationBundleService::Terminal(
    const std::shared_ptr<Operation> &operation, std::string state,
    std::string code, std::string detail, nlohmann::json result) {
  std::lock_guard lock(mutex_);
  if (operation->state == "completed" || operation->state == "cancelled" ||
      operation->state == "failed" || operation->state == "timed_out")
    return;
  if (operation->cancel_requested && state != "cancelled") {
    state = "cancelled";
    code = "cancelled_with_terminal_precedence";
    detail = "accepted cancellation took precedence over terminal work";
    result = nullptr;
  }
  operation->state = std::move(state);
  operation->terminal_code = std::move(code);
  operation->terminal_detail = std::move(detail);
  operation->terminal_at = NowRfc3339();
  operation->result = std::move(result);
}

nlohmann::json FHSSInvestigationBundleService::OperationJson(
    const Operation &operation) const {
  return {{"schema", "graphx.dashboard.fhss_investigation_operation.v1"},
          {"operation_id", operation.operation_id},
          {"operation", operation.kind},
          {"request_id", operation.request.value("request_id", "")},
          {"bundle_name", operation.request.value("bundle_name", "")},
          {"state", operation.state},
          {"created_at", operation.created_at},
          {"started_at", operation.started_at.empty()
                             ? nlohmann::json(nullptr)
                             : nlohmann::json(operation.started_at)},
          {"terminal_at", operation.terminal_at.empty()
                              ? nlohmann::json(nullptr)
                              : nlohmann::json(operation.terminal_at)},
          {"terminal",
           {{"code", operation.terminal_code.empty()
                         ? nlohmann::json(nullptr)
                         : nlohmann::json(operation.terminal_code)},
            {"detail", operation.terminal_detail.empty()
                           ? nlohmann::json(nullptr)
                           : nlohmann::json(operation.terminal_detail)}}},
          {"result", operation.result},
          {"bounds",
           {{"timeout_ms",
             operation.request.value("timeout_ms", kDefaultTimeout.count())},
            {"checkpoint_bound_ms", kCheckpointBound.count()}}}};
}

void FHSSInvestigationBundleService::PurgeUnlocked() {
  while (operations_.size() >= kMaxOperations) {
    const auto found = std::ranges::find_if(operations_, [](const auto &item) {
      return item->state == "completed" || item->state == "cancelled" ||
             item->state == "failed" || item->state == "timed_out";
    });
    if (found == operations_.end())
      break;
    idempotency_.erase((*found)->idempotency_digest);
    operations_.erase(found);
  }
}

void FHSSInvestigationBundleService::Shutdown() {
  {
    std::lock_guard lock(mutex_);
    if (shutting_down_)
      return;
    shutting_down_ = true;
    for (const auto &operation : queue_) {
      operation->cancel_requested = true;
      operation->state = "cancelled";
      operation->terminal_code = "application_shutdown";
      operation->terminal_detail =
          "queued operation cancelled during shutdown";
      operation->terminal_at = NowRfc3339();
      operation->result = nullptr;
    }
    queue_.clear();
    if (const auto active = active_operation_.lock();
        active && !active->publication_committed)
      active->cancel_requested = true;
  }
  cv_.notify_all();
  worker_.request_stop();
  if (worker_.joinable())
    worker_.join();
}

} // namespace dsp::fhss::dashboard
