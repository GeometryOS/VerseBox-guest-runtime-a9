#include "Driver.h"
#include "Wire.h"
#include "ClientBuffers.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <map>
#include <mutex>
#include <set>
#include <string>

namespace a10binder {
struct Driver { std::string path; uint64_t process; std::set<int> sockets; bool commandRead; };
static std::mutex driverMutex;
static std::map<int, Driver> drivers;
static const pid_t driverOwner = getpid();
struct Session {
    int socket = -1, driver = -1;
    uint64_t process = 0;
    bool commandRead = false;
};
// IPCThreadState flushes Parcel frees in its pthread destructor before THREAD_EXIT.
// A C++ TLS destructor here would close the channel before that flush.
// pthread keys, not thread_local: A9's linker cannot relocate shared-library
// TLS (no TLSDESC, TPREL64 is a no-op), so a TLS variable would corrupt the
// thread pointer on first use.
static pthread_key_t sessionsKey;
static pthread_once_t sessionsOnce = PTHREAD_ONCE_INIT;
static void createSessionsKey() { pthread_key_create(&sessionsKey, nullptr); }
static std::map<int, Session>* getSessions() {
    pthread_once(&sessionsOnce, createSessionsKey);
    return static_cast<std::map<int, Session>*>(pthread_getspecific(sessionsKey));
}
static void setSessions(std::map<int, Session>* value) {
    pthread_once(&sessionsOnce, createSessionsKey);
    pthread_setspecific(sessionsKey, value);
}

static Session* findSession(int fd) {
    std::map<int, Session>* sessions = getSessions();
    if (!sessions) return nullptr;
    auto entry = sessions->find(fd);
    return entry == sessions->end() ? nullptr : &entry->second;
}

static void forgetSession(int fd) {
    std::map<int, Session>* sessions = getSessions();
    if (!sessions) return;
    sessions->erase(fd);
    if (sessions->empty()) { delete sessions; setSessions(nullptr); }
}

static int driverResult(int result) {
    if (result < 0 && (errno == ECONNRESET || errno == EPIPE || errno == ENOTCONN))
        errno = ECONNREFUSED;
    return result;
}

static bool exchange(int fd, Packet* request, Packet* reply) {
    if (!sendPacket(fd, *request) || !receivePacket(fd, reply)) {
        char line[160];
        int length = snprintf(line, sizeof(line), "a10binder exchange failed code=%x errno=%d\n",
                              request->header.command, errno);
        if (length > 0) write(2, line, static_cast<size_t>(length));
        return false;
    }
    if (reply->header.kind != Ack) { errno = EPROTO; return false; }
    if (reply->header.status) {
        char line[160];
        int length = snprintf(line, sizeof(line), "a10binder exchange status code=%x status=%d\n",
                              request->header.command, reply->header.status);
        if (length > 0) write(2, line, static_cast<size_t>(length));
        errno = reply->header.status;
        return false;
    }
    return true;
}
static int threadSocket(int fd) {
    Driver driver;
    {
        std::lock_guard<std::mutex> lock(driverMutex);
        auto it = drivers.find(fd);
        if (it == drivers.end()) { errno = EBADF; return -1; }
        Session* session = findSession(fd);
        if (session && session->process == it->second.process &&
            it->second.sockets.count(session->socket)) return session->socket;
        driver = it->second;
    }
    int socket = connectSocket(driver.path.c_str());
    if (socket < 0) return -1;
    Packet hello, response;
    hello.header.kind = Hello;
    hello.header.token = driver.process;
    hello.header.arg = gettid();
    if (!exchange(socket, &hello, &response)) { int error = errno; close(socket); errno = error; return -1; }
    std::lock_guard<std::mutex> lock(driverMutex);
    auto current = drivers.find(fd);
    if (current == drivers.end() || current->second.process != driver.process) {
        close(socket); errno = EBADF; return -1;
    }
    current->second.sockets.insert(socket);
    std::map<int, Session>* sessions = getSessions();
    if (!sessions) { sessions = new std::map<int, Session>; setSessions(sessions); }
    (*sessions)[fd] = Session{socket, fd, driver.process, driver.commandRead};
    return socket;
}
static bool writeCommand(int fd, uint32_t code, const uint8_t* data, size_t size, uint64_t readCapacity) {
    Packet packet, reply;
    packet.header.kind = readCapacity ? CommandRead : Command;
    packet.header.arg = readCapacity;
    packet.header.command = code;
    uintptr_t freeAddress = 0;
    if (code == BC_TRANSACTION_SG || code == BC_REPLY_SG) {
        binder_transaction_data_sg transaction;
        memcpy(&transaction, data, sizeof(transaction));
        packet.header.command = code == BC_TRANSACTION_SG ? BC_TRANSACTION : BC_REPLY;
        if (!serializeTransaction(packet.header.command, transaction.transaction_data, &packet,
                                  transaction.buffers_size))
            return false;
    } else if (code == BC_TRANSACTION || code == BC_REPLY) {
        binder_transaction_data transaction;
        memcpy(&transaction, data, sizeof(transaction));
        if (!serializeTransaction(code, transaction, &packet)) return false;
    } else if (code == BC_FREE_BUFFER) {
        memcpy(&freeAddress, data, sizeof(freeAddress));
        if (!clientBuffers().token(freeAddress, &packet.header.token)) return false;
    } else {
        switch (code) {
            case BC_INCREFS: case BC_ACQUIRE: case BC_RELEASE: case BC_DECREFS:
            case BC_INCREFS_DONE: case BC_ACQUIRE_DONE:
            case BC_ENTER_LOOPER: case BC_REGISTER_LOOPER: case BC_EXIT_LOOPER:
            case BC_REQUEST_DEATH_NOTIFICATION: case BC_CLEAR_DEATH_NOTIFICATION:
            case BC_DEAD_BINDER_DONE: break;
            default: errno = ENOTSUP; return false;
        }
        packet.append(data, size);
    }
    if (!exchange(fd, &packet, &reply)) return false;
    if (freeAddress) clientBuffers().release(freeAddress);
    return true;
}
static bool refreshWakeups(int fd) {
    std::lock_guard<std::mutex> lock(driverMutex);
    if (!drivers.count(fd)) { errno = EBADF; return false; }
    int saved = errno;
    Packet request, signal;
    request.header.kind = Rearm;
    if (!sendPacket(fd, request)) return false;
    for (;;) {
        if (!receivePacket(fd, &signal)) return false;
        if (!signal.data.empty() || !signal.fds.empty() || signal.header.command ||
            signal.header.status || signal.header.token || signal.header.arg ||
            (signal.header.kind != Wakeup && signal.header.kind != Ack)) {
            errno = EPROTO;
            return false;
        }
        if (signal.header.kind == Ack) break;
    }
    errno = saved;
    return true;
}

static int writeRead(int fd, binder_write_read* operation) {
    int socket = threadSocket(fd);
    if (socket < 0) return -1;
    operation->write_consumed = operation->read_consumed = 0;
    bool readRequested = false;
    while (operation->write_consumed < operation->write_size) {
        size_t remaining = operation->write_size - operation->write_consumed;
        if (remaining < sizeof(uint32_t)) { errno = EINVAL; return -1; }
        const auto* input = reinterpret_cast<const uint8_t*>(operation->write_buffer) + operation->write_consumed;
        uint32_t code;
        memcpy(&code, input, sizeof(code));
        size_t length = _IOC_SIZE(code);
        if (length > remaining - sizeof(code)) { errno = EINVAL; return -1; }
        uint64_t capacity = findSession(fd)->commandRead && remaining == sizeof(code) + length
                ? operation->read_size : 0;
        if (!writeCommand(socket, code, input + sizeof(code), length, capacity)) return -1;
        operation->write_consumed += sizeof(code) + length;
        readRequested = capacity != 0;
    }
    if (operation->read_size) {
        Packet read, event;
        read.header.kind = Read;
        read.header.arg = operation->read_size;
        if ((!readRequested && !sendPacket(socket, read)) || !receivePacket(socket, &event)) return -1;
        static int traceCount = 0;
        if (traceCount < 12) {
            char line[160];
            int length = snprintf(line, sizeof(line), "a10binder event kind=%u status=%d command=%08x nr=%u isize=%llu data=%zu\n",
                                  event.header.kind, event.header.status, event.header.command,
                                  _IOC_NR(event.header.command),
                                  static_cast<unsigned long long>(_IOC_SIZE(event.header.command)),
                                  event.data.size());
            if (length > 0) write(2, line, static_cast<size_t>(length));
            ++traceCount;
        }
        if (event.header.status) {
            char line[160];
            int length = snprintf(line, sizeof(line), "a10binder event status=%d kind=%u command=%x\n",
                                  event.header.status, event.header.kind, event.header.command);
            if (length > 0) write(2, line, static_cast<size_t>(length));
            errno = event.header.status;
            return -1;
        }
        if (event.header.kind != Event) { errno = EPROTO; return -1; }
        size_t length = _IOC_SIZE(event.header.command);
        if (sizeof(uint32_t) + length > operation->read_size) { errno = EMSGSIZE; return -1; }
        auto* output = reinterpret_cast<uint8_t*>(operation->read_buffer);
        binder_transaction_data_secctx transaction{};
        if (isIncomingTransaction(event.header.command) || event.header.command == BR_REPLY) {
            if (!clientBuffers().receive(&event, &transaction.transaction_data, &transaction.secctx)) return -1;
            memcpy(output + sizeof(uint32_t), &transaction, length);
        } else {
            if (event.data.size() != length) { errno = EPROTO; return -1; }
            memcpy(output + sizeof(uint32_t), event.data.data(), length);
        }
        memcpy(output, &event.header.command, sizeof(uint32_t));
        operation->read_consumed = sizeof(uint32_t) + length;
    }
    // Writes leave queued events pending; only a read can consume poll readiness.
    if (operation->read_consumed && !refreshWakeups(fd)) return -1;
    return 0;
}
}

extern "C" const char* a10binder_default_driver() {
    const char* path = getenv("A10VM_BINDER_PATH");
    return path ? path : "/private-binder-path-not-configured";
}
extern "C" const char* a10binder_default_hwbinder_driver() {
    const char* path = getenv("A10VM_HWBINDER_PATH");
    return path ? path : "/private-hwbinder-path-not-configured";
}
extern "C" int a10binder_open(const char* path) {
    using namespace a10binder;
    if (getpid() != driverOwner) { errno = ECHILD; return -1; }
    int fd = connectSocket(path);
    if (fd < 0) return -1;
    Packet hello, reply;
    hello.header.kind = Hello;
    hello.header.command = getuid() + 1;
    if (!exchange(fd, &hello, &reply)) { int error = errno; close(fd); errno = error; return -1; }
    std::lock_guard<std::mutex> lock(driverMutex);
    drivers.emplace(fd, Driver{path, reply.header.token, {}, (reply.header.arg & kCommandRead) != 0});
    return fd;
}
extern "C" int a10binder_close(int fd) {
    using namespace a10binder;
    // fork/exec children only close their inherited descriptor; shutdown would affect the parent.
    if (getpid() != driverOwner) return close(fd);
    std::lock_guard<std::mutex> lock(driverMutex);
    auto driver = drivers.find(fd);
    if (driver == drivers.end()) { errno = EBADF; return -1; }
    for (int socket : driver->second.sockets) { shutdown(socket, SHUT_RDWR); close(socket); }
    drivers.erase(driver);
    forgetSession(fd);
    return close(fd);
}
extern "C" int a10binder_is_driver(int fd) {
    if (getpid() != a10binder::driverOwner) return 0;
    std::lock_guard<std::mutex> lock(a10binder::driverMutex);
    return a10binder::drivers.count(fd) != 0;
}
extern "C" int a10binder_ioctl(int fd, unsigned long request, void* argument) {
    using namespace a10binder;
    if (getpid() != driverOwner) { errno = ECHILD; return -1; }
    if (request == BINDER_WRITE_READ) {
        binder_write_read* operation = static_cast<binder_write_read*>(argument);
        int result = driverResult(writeRead(fd, operation));
        if (result < 0) {
            char line[160];
            int length = snprintf(line, sizeof(line), "a10binder writeRead failed errno=%d write=%llu read=%llu\n",
                                  errno, static_cast<unsigned long long>(operation->write_size),
                                  static_cast<unsigned long long>(operation->read_size));
            if (length > 0) write(2, line, static_cast<size_t>(length));
        }
        return result;
    }
    if (request == BINDER_VERSION) {
        static_cast<binder_version*>(argument)->protocol_version = BINDER_CURRENT_PROTOCOL_VERSION;
        return 0;
    }
    if (request == BINDER_THREAD_EXIT) {
        Session* session = findSession(fd);
        if (!session || session->socket < 0) return 0;
        Packet packet, reply;
        packet.header.kind = Exit;
        bool ok = exchange(session->socket, &packet, &reply);
        {
            std::lock_guard<std::mutex> lock(driverMutex);
            auto driver = drivers.find(fd);
            if (driver != drivers.end() && driver->second.process == session->process &&
                driver->second.sockets.erase(session->socket)) close(session->socket);
            forgetSession(fd);
        }
        return driverResult(ok ? 0 : -1);
    }
    if (request != BINDER_SET_CONTEXT_MGR && request != BINDER_SET_MAX_THREADS) { errno = ENOTSUP; return -1; }
    int socket = threadSocket(fd);
    if (socket < 0) return -1;
    Packet packet, reply;
    packet.header.kind = request == BINDER_SET_CONTEXT_MGR ? Context : MaxThreads;
    if (request == BINDER_SET_MAX_THREADS) packet.header.arg = *static_cast<uint32_t*>(argument);
    return driverResult(exchange(socket, &packet, &reply) ? 0 : -1);
}
