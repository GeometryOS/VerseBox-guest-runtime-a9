#include <vendor/xiaomi/hardware/mtdservice/1.1/IMTService.h>
#include <android-base/file.h>
#include <errno.h>
#include <fcntl.h>
#include <mutex>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

using android::hardware::Return;
using android::hardware::Void;
using android::hardware::hidl_string;
using android::hardware::hidl_vec;
using vendor::xiaomi::hardware::mtdservice::V1_1::IMTService;

namespace {
constexpr const char* kRoot = "/data/system/a10vm-mtd";
constexpr size_t kMaxBytes = 1024 * 1024;
// The ROM treats 41 as absent data; other errors trigger its corruption notification.
constexpr int kMissing = 41;

class GuestMtdStorage final : public IMTService {
    std::mutex lock;

    std::string path(int32_t directory, const hidl_string& name) {
        std::string value(name.c_str(), name.size());
        if (directory < 0 || value.empty() || value.size() > 200 || value == "." ||
                value == ".." || value.find('/') != std::string::npos ||
                value.find('\0') != std::string::npos) return {};
        return std::string(kRoot) + "/" + std::to_string(directory) + "/" + value;
    }

public:
    Return<void> persist_read(int32_t directory, const hidl_string& name, persist_read_cb callback) override {
        std::lock_guard<std::mutex> guard(lock);
        std::string file = path(directory, name), bytes;
        hidl_vec<int8_t> data;
        int result = EINVAL;
        struct stat info;
        if (!file.empty()) {
            if (stat(file.c_str(), &info) < 0) result = errno == ENOENT ? kMissing : errno;
            else if (!S_ISREG(info.st_mode) || info.st_size < 0 || info.st_size > kMaxBytes) result = EINVAL;
            else if (!android::base::ReadFileToString(file, &bytes)) result = errno ? errno : EIO;
            else {
                data.resize(bytes.size());
                if (!bytes.empty()) memcpy(data.data(), bytes.data(), bytes.size());
                result = 0;
            }
        }
        callback(result, data);
        return Void();
    }

    Return<int32_t> persist_write(int32_t directory, const hidl_string& name,
            const hidl_vec<int8_t>& data, int32_t length) override {
        std::lock_guard<std::mutex> guard(lock);
        std::string file = path(directory, name);
        if (file.empty() || length < 0 || size_t(length) != data.size() || data.size() > kMaxBytes) return EINVAL;
        if (mkdir(kRoot, 0700) < 0 && errno != EEXIST) return errno;
        std::string parent = file.substr(0, file.rfind('/'));
        if (mkdir(parent.c_str(), 0700) < 0 && errno != EEXIST) return errno;
        std::string temporary = file + ".tmp";
        int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0) return errno;
        bool written = android::base::WriteFully(fd, data.data(), data.size()) && fsync(fd) == 0;
        int result = written ? 0 : (errno ? errno : EIO);
        close(fd);
        if (result == 0 && rename(temporary.c_str(), file.c_str()) < 0) result = errno;
        if (result != 0) unlink(temporary.c_str());
        return result;
    }

    Return<int32_t> persist_remove(int32_t directory, const hidl_string& name) override {
        std::lock_guard<std::mutex> guard(lock);
        std::string file = path(directory, name);
        if (file.empty()) return EINVAL;
        return unlink(file.c_str()) == 0 || errno == ENOENT ? 0 : errno;
    }

    Return<void> getFid(getFid_cb cb) override { cb(""); return Void(); }
    Return<void> eccSign(int32_t, const hidl_string&, eccSign_cb cb) override { cb(""); return Void(); }
    Return<int32_t> reload(const hidl_string&, const hidl_string&) override { return -EOPNOTSUPP; }
    Return<void> enroll(const hidl_string&, int32_t, enroll_cb cb) override { cb(""); return Void(); }
    Return<int32_t> ifaa_key_get_version() override { return -EOPNOTSUPP; }
    Return<void> ifaa_key_prepare(ifaa_key_prepare_cb cb) override { cb(""); return Void(); }
    Return<int32_t> ifaa_key_load(const hidl_string&, const hidl_string&) override { return -EOPNOTSUPP; }
    Return<int32_t> fido_key_get_version() override { return -EOPNOTSUPP; }
    Return<void> fido_key_prepare(fido_key_prepare_cb cb) override { cb(""); return Void(); }
    Return<int32_t> fido_key_load(const hidl_string&, const hidl_string&) override { return -EOPNOTSUPP; }
    Return<void> soter_generate(soter_generate_cb cb) override { cb(""); return Void(); }
    Return<int32_t> soter_get_state() override { return -EOPNOTSUPP; }
    Return<void> soter_set_state(int32_t) override { return Void(); }
    Return<void> ifaa_key_dump(ifaa_key_dump_cb cb) override { cb(""); return Void(); }
};
}

bool registerGuestMtdStorage() {
    if (access("/system/framework/vendor.xiaomi.hardware.mtdservice-V1.1-java.jar", F_OK) != 0) return true;
    auto existing = IMTService::tryGetService();
    if (existing != nullptr) return true;
    android::sp<GuestMtdStorage> service = new GuestMtdStorage;
    if (service->registerAsService() != android::OK) return false;
    printf("guest_mtd_storage_ready=1\n"); fflush(stdout);
    return true;
}
