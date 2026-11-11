#include "SocketStream.h"
#include "ChecksumCalculatorThreadInfo.h"
#include "WindowProtocol.h"

#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <thread>
#include <sys/socket.h>
#include <unistd.h>

namespace {
template<class Function> void expectError(Function function, const char* message) {
    bool caught = false;
    try { function(); } catch (const std::exception& error) {
        caught = std::strstr(error.what(), message) != nullptr;
    }
    assert(caught);
}

std::array<int, 2> pair() {
    std::array<int, 2> sockets{};
    assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets.data()) == 0);
    return sockets;
}

void bufferedRoundTrip() {
    const auto sockets = pair();
    SocketStream client(sockets[0], 4);
    std::thread peer([fd = sockets[1]] {
        SocketStream server(fd, 4);
        char data[9]{};
        server.readFully(data, sizeof(data));
        assert(std::memcmp(data, "abcdefXYZ", 9) == 0);
        server.writeFully("OK", 2);
    });
    std::memcpy(client.alloc(3), "abc", 3);
    std::memcpy(client.alloc(3), "def", 3);
    client.flush();
    client.writeFully("X", 1);
    std::memcpy(client.alloc(2), "YZ", 2);
    char result[2]{};
    client.readback(result, 2);
    assert(std::memcmp(result, "OK", 2) == 0);
    peer.join();
}

void fragmentedRead() {
    const auto sockets = pair();
    SocketStream reader(sockets[0]);
    std::thread peer([fd = sockets[1]] {
        SocketStream writer(fd);
        for (unsigned char value = 0; value < 127; ++value) writer.writeFully(&value, 1);
    });
    unsigned char result[127]{};
    reader.readFully(result, sizeof(result));
    for (int i = 0; i < 127; ++i) assert(result[i] == i);
    peer.join();
    assert(!reader.readOrEof(result, 1));
}

void failures() {
    auto sockets = pair();
    SocketStream reader(sockets[0]);
    assert(write(sockets[1], "x", 1) == 1);
    close(sockets[1]);
    char result[2]{};
    expectError([&] { reader.readFully(result, 2); }, "Truncated");
    expectError([&] { reader.readFully(result, 1); }, "EOF");
    std::memcpy(reader.alloc(1), "x", 1);
    expectError([&] { reader.flush(); }, "GL send");
    expectError([&] { reader.alloc(IOStream::maxTransfer + 1); }, "limit");
    expectError([&] { reader.getDmaForReading(0); }, "DMA");

    sockets = pair();
    SocketStream waiting(sockets[0], 16, 25);
    SocketStream idle(sockets[1]);
    expectError([&] { waiting.readFully(result, 1); }, "GL receive");
}

void checksumOrderAndLength() {
    ChecksumCalculator writer;
    ChecksumCalculator reader;
    assert(writer.setVersion(1) && reader.setVersion(1));
    assert(!writer.setVersion(2));
    unsigned char checksum[8]{};
    const char data[] = "packet";
    for (int packet = 0; packet < 3; ++packet) {
        writer.addBuffer(data, sizeof(data));
        assert(writer.writeChecksum(checksum, sizeof(checksum)));
        reader.addBuffer(data, sizeof(data));
        if (packet == 1) checksum[4] ^= 1;
        assert(reader.validate(checksum, sizeof(checksum)) == (packet != 1));
    }
    writer.addBuffer(data, sizeof(data));
    assert(writer.writeChecksum(checksum, sizeof(checksum)));
    reader.addBuffer(data, sizeof(data) - 1);
    assert(!reader.validate(checksum, sizeof(checksum)));
}

void checksumThreadContext() {
    ChecksumCalculatorThreadInfo main;
    assert(ChecksumCalculatorThreadInfo::setVersion(1));
    std::thread other([] {
        ChecksumCalculatorThreadInfo child;
        assert(child.get().getVersion() == 0);
        assert(ChecksumCalculatorThreadInfo::setVersion(0));
    });
    other.join();
    assert(main.get().getVersion() == 1);
}

void windowControl() {
    using namespace windowprobe;
    Input sent;
    validate(sent);
    Frame frame{magic, 1, 0, sent.x, sent.y};
    validate(frame, 0, sent);
    expectError([&] { validate(frame, 1, sent); }, "sequence mismatch");
    frame.inputSequence = 1;
    expectError([&] { validate(frame, 0, sent); }, "sequence mismatch");
    frame.inputSequence = 0; ++frame.x;
    expectError([&] { validate(frame, 0, sent); }, "sequence mismatch");
    sent.x = width;
    expectError([&] { validate(sent); }, "Invalid window input");
    sent.x = 0; sent.running = 2;
    expectError([&] { validate(sent); }, "Invalid window input");
    sent.running = 0;
    validate(sent);
    sent.tag ^= 1;
    expectError([&] { validate(sent); }, "Invalid window input");
}
}

int main() {
    bufferedRoundTrip();
    fragmentedRead();
    failures();
    checksumOrderAndLength();
    checksumThreadContext();
    windowControl();
    std::puts("PASS buffered I/O, fragmented reads, EOF, timeout, disconnect, DMA boundary, checksum order/length, TLS, window control");
}
