#!/usr/bin/env python3
"""Derive the API 28 private linker sources with the Android 10 platform contract.

Android 10's `container-probe/runtime/dynamic/platform.patch` makes four things
true for the private linker.  API 28 needs the same three that are independent
of the AOSP-10 source layout:

1. `get_ld_config_file_path()` reads `A10VM_LD_CONFIG` and nothing else.
2. `parse_config_file()` matches `dir.<section>` against the *physical*
   executable path, with a st_dev/st_ino fallback for execute-only APK
   install directories whose realpath differs from the packaging path.
3. `resolve_paths()` accepts namespace search paths in host coordinates
   (`A10VM_HOST_ROOT` / `A10VM_STATE_ROOT`) and hands them back in guest
   coordinates, which is what `GuestRuntime` writes into `ld.config.txt`.

Every edit below is anchored on literal upstream text and fails loudly when the
anchor is missing, so a source refresh cannot silently produce an unpatched
linker.
"""

import pathlib

RT = pathlib.Path(__file__).resolve().parent
UP = RT / "upstream" / "bionic" / "linker"
OUT = RT / "build" / "derived" / "bionic" / "linker"

SOURCES = ("linker.cpp", "linker_config.cpp", "linker_utils.cpp")

# --------------------------------------------------------------------------
# 1. linker.cpp: the private configuration is the only source of namespaces.
# --------------------------------------------------------------------------
LINKER_CPP = [
    (
        # A10 does the same in platform.patch; keep the private includes first.
        '#include "linker.h"\n',
        '#include "linker.h"\n#include "LinkerFileStat.h"\n',
    ),
    (
        # Host-coordinate paths (A10VM_LD_CONFIG lives under the state root)
        # must not be prefixed twice by the guest path layer.
        """// Checks if the file exists and not a directory.
static bool file_exists(const char* path) {
  struct stat s;

  if (stat(path, &s) != 0) {""",
        """// Checks if the file exists and not a directory.
static bool file_exists(const char* path) {
  struct stat s;

  if (linkerPathStat(path, &s) != 0) {""",
    ),
    (
        """static std::string get_ld_config_file_path() {
#ifdef USE_LD_CONFIG_FILE
  // This is a debugging/testing only feature. Must not be available on
  // production builds.
  const char* ld_config_file_env = getenv("LD_CONFIG_FILE");
  if (ld_config_file_env != nullptr && file_exists(ld_config_file_env)) {
    return ld_config_file_env;
  }
#endif

  if (file_exists(kLdConfigArchFilePath)) {
    return kLdConfigArchFilePath;
  }

  std::string ld_config_file_vndk = get_ld_config_file_vndk_path();
  if (file_exists(ld_config_file_vndk.c_str())) {
    return ld_config_file_vndk;
  }

  return kLdConfigFilePath;
}""",
        """static std::string get_ld_config_file_path() {
  // Android 10's private linker contract: only A10VM_LD_CONFIG is consulted,
  // the ROM's ld.config.28.txt is never a fallback.  Redroid 9's configuration
  // selects sections by dir.system/dir.vendor, but every guest binary executes
  // below /data/app, so that file can never describe a private runtime.
  const char* private_config = getenv("A10VM_LD_CONFIG");
  if (private_config != nullptr && private_config[0] == '/' && file_exists(private_config)) {
    return private_config;
  }
  async_safe_fatal("A10VM_LD_CONFIG requires an existing absolute private configuration");
}""",
    ),
    (
        # Android 10 turns a rejected configuration into a fatal error so a
        # mis-selected section can never degrade into the default search paths.
        """      DL_WARN("Warning: couldn't read \\"%s\\" for \\"%s\\" (using default configuration instead): %s",
              ld_config_file_path.c_str(),
              executable_path,
              error_msg.c_str());""",
        """      DL_WARN("Warning: couldn't read private config \\"%s\\" for \\"%s\\": %s",
              ld_config_file_path.c_str(),
              executable_path,
              error_msg.c_str());""",
    ),
    (
        """  if (config == nullptr) {
    return init_default_namespace_no_config(g_is_asan);
  }""",
        """  if (config == nullptr) {
    async_safe_fatal("A10VM private linker configuration was rejected: "
                     "config=%s executable=%s reason=%s",
                     ld_config_file_path.c_str(), executable_path,
                     error_msg.empty() ? "no matching executable section" : error_msg.c_str());
  }""",
    ),
]

# --------------------------------------------------------------------------
# 2. linker_config.cpp: match dir sections in physical coordinates.
# --------------------------------------------------------------------------
CONFIG_MATCHER = """static bool config_matches_binary(const char* binary_path, const std::string& directory) {
  if (file_is_under_dir(binary_path, directory)) return true;
  // Execute-only install directories and bind aliases may retain different path names.
  struct stat expected;
  if (linkerPathStat(directory.c_str(), &expected) != 0 || !S_ISDIR(expected.st_mode)) return false;
  std::string parent = dirname(binary_path);
  while (parent != "/" && parent != ".") {
    struct stat actual;
    if (linkerPathStat(parent.c_str(), &actual) == 0 &&
        actual.st_dev == expected.st_dev && actual.st_ino == expected.st_ino) return true;
    std::string next = dirname(parent.c_str());
    if (next == parent) break;
    parent = std::move(next);
  }
  return false;
}

"""

LINKER_CONFIG_CPP = [
    (
        '#include "linker_utils.h"\n',
        '#include "linker_utils.h"\n'
        '#include "GuestPaths.h"\n'
        '#include "LinkerFileStat.h"\n'
        "#include <fcntl.h>\n"
        "#include <sys/stat.h>\n",
    ),
    (
        "static bool parse_config_file(const char* ld_config_file_path,",
        CONFIG_MATCHER + "static bool parse_config_file(const char* ld_config_file_path,",
    ),
    (
        """  ConfigParser cp(std::move(content));
""",
        """  ConfigParser cp(std::move(content));

  // /proc fd links expose guest paths; configuration directories use physical paths.
  char physical_binary[PATH_MAX];
  const char* config_binary = guestPath(AT_FDCWD, binary_realpath, physical_binary);
  if (config_binary == nullptr) {
    *error_msg = std::string("error resolving executable \\"") + binary_realpath +
                 "\\": " + strerror(errno);
    return false;
  }
""",
    ),
    (
        """    if (result == ConfigParser::kSection || result == ConfigParser::kEndOfFile) {
      return false;
    }""",
        """    if (result == ConfigParser::kSection || result == ConfigParser::kEndOfFile) {
      *error_msg = create_error_msg(ld_config_file_path, cp.lineno(),
          std::string("no dir section matches executable \\"") + config_binary + "\\"");
      return false;
    }""",
    ),
    (
        "      if (file_is_under_dir(binary_realpath, value)) {",
        "      if (config_matches_binary(config_binary, value)) {",
    ),
]

# --------------------------------------------------------------------------
# 3. linker_utils.cpp: namespace search paths keep guest coordinates.
# --------------------------------------------------------------------------
LINKER_UTILS_CPP = [
    (
        '#include "android-base/strings.h"\n',
        '#include "android-base/strings.h"\n'
        '#include "GuestPaths.h"\n'
        '#include "LinkerFileStat.h"\n',
    ),
    (
        """    char resolved_path[PATH_MAX];
    const char* original_path = path.c_str();
    if (realpath(original_path, resolved_path) != nullptr) {
      struct stat s;
      if (stat(resolved_path, &s) == -1) {""",
        """    char resolved_path[PATH_MAX];
    const char* original_path = path.c_str();
    // Namespace paths are written in host coordinates by GuestRuntime, while
    // realpath() works in guest coordinates.  Strip the guest root before
    // resolving so the same directory is not addressed twice.
    if (guestPhysicalPath(original_path)) {
      original_path += strlen(guestRoot());
      if (!original_path[0]) original_path = "/";
    }
    if (realpath(original_path, resolved_path) != nullptr) {
      struct stat s;
      if (linkerPathStat(resolved_path, &s) == -1) {""",
    ),
]

EDITS = {
    "linker.cpp": LINKER_CPP,
    "linker_config.cpp": LINKER_CONFIG_CPP,
    "linker_utils.cpp": LINKER_UTILS_CPP,
}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name in SOURCES:
        source = (UP / name).read_text(encoding="utf-8")
        text = source
        for old, new in EDITS[name]:
            count = text.count(old)
            if count != 1:
                raise SystemExit(
                    "patch-linker-a9: anchor matched %d times in %s\n--- anchor ---\n%s"
                    % (count, name, old[:400])
                )
            text = text.replace(old, new, 1)
        if text == source:
            raise SystemExit("patch-linker-a9: %s unchanged" % name)
        (OUT / name).write_text(text, encoding="utf-8")
        print("patched: %s" % (OUT / name))


if __name__ == "__main__":
    main()
