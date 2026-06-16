#include "Platform.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// #include <winsock2.h>
// #include <ws2tcpip.h>
// #include <windows.h>
// #include <psapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <psapi.h>
#else
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/sysinfo.h>
#endif
#endif

namespace {

#ifdef _WIN32
constexpr uintptr_t INVALID_NATIVE_SOCKET = static_cast<uintptr_t>(INVALID_SOCKET);

HANDLE asHandle(void* handle) {
    return static_cast<HANDLE>(handle);
}
#else
constexpr uintptr_t INVALID_NATIVE_SOCKET = static_cast<uintptr_t>(-1);
#endif

bool validSocket(uintptr_t socketHandle) {
    return socketHandle != INVALID_NATIVE_SOCKET;
}

std::string lastSocketErrorText(const char* action) {
#ifdef _WIN32
    return std::string(action) + " that bai voi code: " +
           std::to_string(WSAGetLastError());
#else
    return std::string(action) + " that bai: " + std::strerror(errno);
#endif
}

}  // namespace

namespace platform {

void setConsoleUtf8() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
}

bool replaceFileAtomic(const std::string& from, const std::string& to) {
#ifdef _WIN32
    return MoveFileExA(from.c_str(), to.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    if (!ec) {
        return true;
    }
    std::filesystem::remove(to, ec);
    ec.clear();
    std::filesystem::rename(from, to, ec);
    return !ec;
#endif
}

double currentProcessPrivateMemoryMB() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                             sizeof(pmc))) {
        return static_cast<double>(pmc.PrivateUsage) / (1024.0 * 1024.0);
    }
    return 0.0;
#else
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(__APPLE__)
        return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);
#else
        return static_cast<double>(usage.ru_maxrss) / 1024.0;
#endif
    }
    return 0.0;
#endif
}

uint64_t availablePhysicalMemoryBytes() {
#ifdef _WIN32
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) {
        return 0;
    }
    return static_cast<uint64_t>(status.ullAvailPhys);
#elif defined(__linux__)
    struct sysinfo info;
    if (sysinfo(&info) != 0) {
        return 0;
    }
    return static_cast<uint64_t>(info.freeram) *
           static_cast<uint64_t>(info.mem_unit);
#else
    long pages = sysconf(_SC_AVPHYS_PAGES);
    long pageSize = sysconf(_SC_PAGESIZE);
    if (pages <= 0 || pageSize <= 0) {
        return 0;
    }
    return static_cast<uint64_t>(pages) * static_cast<uint64_t>(pageSize);
#endif
}

uint64_t currentProcessId() {
#ifdef _WIN32
    return static_cast<uint64_t>(GetCurrentProcessId());
#else
    return static_cast<uint64_t>(getpid());
#endif
}

LineReader::LineReader(const std::string& path, size_t bufferBytes)
#ifdef _WIN32
    : fileHandle(INVALID_HANDLE_VALUE),
#else
    : fileDescriptor(-1),
#endif
    buffer(nullptr)
    , bufferCapacity(bufferBytes)
    , bufferSize(0)
    , bufferOffset(0)
    , eof(false)
    , currentFilePos(0) {
#ifdef _WIN32
    fileHandle = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                             NULL);
    eof = (asHandle(fileHandle) == INVALID_HANDLE_VALUE);
#else
    fileDescriptor = open(path.c_str(), O_RDONLY);
    eof = (fileDescriptor < 0);
#endif
    if (!eof) {
        try {
            buffer = new char[bufferCapacity];
        } catch (...) {
            close();
            eof = true;
        }
    }
}

LineReader::~LineReader() {
    close();
    delete[] buffer;
}

bool LineReader::isOpen() const {
#ifdef _WIN32
    return asHandle(fileHandle) != INVALID_HANDLE_VALUE;
#else
    return fileDescriptor >= 0;
#endif
}

void LineReader::close() {
#ifdef _WIN32
    if (asHandle(fileHandle) != INVALID_HANDLE_VALUE) {
        CloseHandle(asHandle(fileHandle));
        fileHandle = INVALID_HANDLE_VALUE;
    }
#else
    if (fileDescriptor >= 0) {
        ::close(fileDescriptor);
        fileDescriptor = -1;
    }
#endif
}

bool LineReader::seek(long long offset) {
    if (!isOpen()) {
        return false;
    }
#ifdef _WIN32
    LARGE_INTEGER li;
    li.QuadPart = offset;
    LARGE_INTEGER newPos;
    if (!SetFilePointerEx(asHandle(fileHandle), li, &newPos, FILE_BEGIN)) {
        return false;
    }
#else
    if (lseek(fileDescriptor, static_cast<off_t>(offset), SEEK_SET) < 0) {
        return false;
    }
#endif
    bufferSize = 0;
    bufferOffset = 0;
    eof = false;
    currentFilePos = offset;
    return true;
}

long long LineReader::tell() const {
    if (!isOpen()) {
        return 0;
    }
    return currentFilePos - static_cast<long long>(bufferSize - bufferOffset);
}

bool LineReader::fillBuffer() {
    if (!isOpen()) {
        return false;
    }
#ifdef _WIN32
    DWORD read = 0;
    DWORD toRead = bufferCapacity > std::numeric_limits<DWORD>::max()
                       ? std::numeric_limits<DWORD>::max()
                       : static_cast<DWORD>(bufferCapacity);
    if (!ReadFile(asHandle(fileHandle), buffer, toRead, &read, NULL) ||
        read == 0) {
        eof = true;
        bufferSize = 0;
        bufferOffset = 0;
        return false;
    }
    bufferSize = static_cast<size_t>(read);
#else
    ssize_t readBytes = 0;
    do {
        readBytes = read(fileDescriptor, buffer, bufferCapacity);
    } while (readBytes < 0 && errno == EINTR);
    if (readBytes <= 0) {
        eof = true;
        bufferSize = 0;
        bufferOffset = 0;
        return false;
    }
    bufferSize = static_cast<size_t>(readBytes);
#endif
    bufferOffset = 0;
    currentFilePos += static_cast<long long>(bufferSize);
    return true;
}

bool LineReader::readLine(std::string_view& line, std::string& scratch) {
    if (eof && bufferOffset >= bufferSize) {
        return false;
    }

    scratch.clear();
    while (true) {
        if (bufferOffset >= bufferSize && !fillBuffer()) {
            break;
        }

        const char* start = buffer + bufferOffset;
        size_t available = bufferSize - bufferOffset;
        const char* newline =
            static_cast<const char*>(std::memchr(start, '\n', available));
        if (newline != nullptr) {
            size_t segmentLength = static_cast<size_t>(newline - start);
            bufferOffset += segmentLength + 1;
            if (scratch.empty()) {
                if (segmentLength > 0 && start[segmentLength - 1] == '\r') {
                    segmentLength--;
                }
                line = std::string_view(start, segmentLength);
                return true;
            }

            scratch.append(start, segmentLength);
            if (!scratch.empty() && scratch.back() == '\r') {
                scratch.pop_back();
            }
            line = scratch;
            return true;
        }

        scratch.append(start, available);
        bufferOffset = bufferSize;
    }

    if (!scratch.empty() && scratch.back() == '\r') {
        scratch.pop_back();
    }
    line = scratch;
    return !line.empty();
}

BinaryMappedReader::BinaryMappedReader(const std::string& filename)
#ifdef _WIN32
    : fileHandle(INVALID_HANDLE_VALUE), mappingHandle(NULL),
#else
    : fileDescriptor(-1),
#endif
    begin(nullptr)
    , cursor(nullptr)
    , end(nullptr)
    , mappedBytes(0) {
#ifdef _WIN32
    fileHandle = CreateFileA(filename.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (asHandle(fileHandle) == INVALID_HANDLE_VALUE) {
        return;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(asHandle(fileHandle), &size) || size.QuadPart <= 0 ||
        static_cast<unsigned long long>(size.QuadPart) >
            static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) {
        CloseHandle(asHandle(fileHandle));
        fileHandle = INVALID_HANDLE_VALUE;
        return;
    }

    mappingHandle = CreateFileMappingA(asHandle(fileHandle), NULL, PAGE_READONLY,
                                       0, 0, NULL);
    if (mappingHandle == NULL) {
        CloseHandle(asHandle(fileHandle));
        fileHandle = INVALID_HANDLE_VALUE;
        return;
    }

    begin = static_cast<const unsigned char*>(
        MapViewOfFile(asHandle(mappingHandle), FILE_MAP_READ, 0, 0, 0));
    if (begin == nullptr) {
        CloseHandle(asHandle(mappingHandle));
        mappingHandle = NULL;
        CloseHandle(asHandle(fileHandle));
        fileHandle = INVALID_HANDLE_VALUE;
        return;
    }
    mappedBytes = static_cast<size_t>(size.QuadPart);
#else
    fileDescriptor = open(filename.c_str(), O_RDONLY);
    if (fileDescriptor < 0) {
        return;
    }

    struct stat st;
    if (fstat(fileDescriptor, &st) != 0 || st.st_size <= 0 ||
        static_cast<uint64_t>(st.st_size) >
            static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        ::close(fileDescriptor);
        fileDescriptor = -1;
        return;
    }

    void* mapped = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ,
                        MAP_PRIVATE, fileDescriptor, 0);
    if (mapped == MAP_FAILED) {
        ::close(fileDescriptor);
        fileDescriptor = -1;
        return;
    }
    begin = static_cast<const unsigned char*>(mapped);
    mappedBytes = static_cast<size_t>(st.st_size);
#endif
    cursor = begin;
    end = begin + mappedBytes;
}

BinaryMappedReader::~BinaryMappedReader() {
#ifdef _WIN32
    if (begin != nullptr) {
        UnmapViewOfFile(begin);
    }
    if (mappingHandle != NULL) {
        CloseHandle(asHandle(mappingHandle));
    }
    if (asHandle(fileHandle) != INVALID_HANDLE_VALUE) {
        CloseHandle(asHandle(fileHandle));
    }
#else
    if (begin != nullptr) {
        munmap(const_cast<unsigned char*>(begin), mappedBytes);
    }
    if (fileDescriptor >= 0) {
        ::close(fileDescriptor);
    }
#endif
}

bool BinaryMappedReader::isOpen() const {
    return begin != nullptr;
}

bool BinaryMappedReader::readBytes(void* dst, size_t bytes) {
    if (bytes == 0) {
        return true;
    }
    if (static_cast<size_t>(end - cursor) < bytes) {
        return false;
    }
    std::memcpy(dst, cursor, bytes);
    cursor += bytes;
    return true;
}

BinaryFileReader::BinaryFileReader(const std::string& filename)
#ifdef _WIN32
    : fileHandle(INVALID_HANDLE_VALUE) {
    fileHandle = CreateFileA(filename.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
#else
    : fileDescriptor(-1) {
    fileDescriptor = open(filename.c_str(), O_RDONLY);
#endif
}

BinaryFileReader::~BinaryFileReader() {
#ifdef _WIN32
    if (asHandle(fileHandle) != INVALID_HANDLE_VALUE) {
        CloseHandle(asHandle(fileHandle));
    }
#else
    if (fileDescriptor >= 0) {
        ::close(fileDescriptor);
    }
#endif
}

bool BinaryFileReader::isOpen() const {
#ifdef _WIN32
    return asHandle(fileHandle) != INVALID_HANDLE_VALUE;
#else
    return fileDescriptor >= 0;
#endif
}

bool BinaryFileReader::readBytes(void* dst, size_t bytes) {
    char* ptr = reinterpret_cast<char*>(dst);
    while (bytes > 0) {
        size_t chunk = bytes > 64ULL * 1024ULL * 1024ULL
                           ? 64ULL * 1024ULL * 1024ULL
                           : bytes;
#ifdef _WIN32
        DWORD readBytes = 0;
        if (!ReadFile(asHandle(fileHandle), ptr, static_cast<DWORD>(chunk),
                      &readBytes, NULL) ||
            readBytes != chunk) {
            return false;
        }
        ptr += readBytes;
        bytes -= readBytes;
#else
        ssize_t got = 0;
        do {
            got = read(fileDescriptor, ptr, chunk);
        } while (got < 0 && errno == EINTR);
        if (got <= 0) {
            return false;
        }
        ptr += got;
        bytes -= static_cast<size_t>(got);
#endif
    }
    return true;
}

BinaryFileWriter::BinaryFileWriter(const std::string& filename, size_t bufferBytes)
#ifdef _WIN32
    : fileHandle(INVALID_HANDLE_VALUE),
#else
    : fileDescriptor(-1),
#endif
    buffer(nullptr)
    , capacity(bufferBytes)
    , used(0)
    , ok(true) {
#ifdef _WIN32
    fileHandle = CreateFileA(filename.c_str(), GENERIC_WRITE, 0, NULL,
                             CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                             NULL);
    ok = asHandle(fileHandle) != INVALID_HANDLE_VALUE;
#else
    fileDescriptor = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    ok = fileDescriptor >= 0;
#endif
    if (ok && capacity > 0) {
        try {
            buffer = new char[capacity];
        } catch (...) {
            ok = false;
        }
    }
}

BinaryFileWriter::~BinaryFileWriter() {
    flush();
    delete[] buffer;
#ifdef _WIN32
    if (asHandle(fileHandle) != INVALID_HANDLE_VALUE) {
        CloseHandle(asHandle(fileHandle));
    }
#else
    if (fileDescriptor >= 0) {
        ::close(fileDescriptor);
    }
#endif
}

bool BinaryFileWriter::isOpen() const {
#ifdef _WIN32
    return ok && asHandle(fileHandle) != INVALID_HANDLE_VALUE;
#else
    return ok && fileDescriptor >= 0;
#endif
}

bool BinaryFileWriter::writeRawAll(const void* data, size_t bytes) {
    const char* ptr = reinterpret_cast<const char*>(data);
    while (bytes > 0) {
        size_t chunk = bytes > 64ULL * 1024ULL * 1024ULL
                           ? 64ULL * 1024ULL * 1024ULL
                           : bytes;
#ifdef _WIN32
        DWORD written = 0;
        if (!WriteFile(asHandle(fileHandle), ptr, static_cast<DWORD>(chunk),
                       &written, NULL) ||
            written != chunk) {
            return false;
        }
        ptr += written;
        bytes -= written;
#else
        ssize_t written = 0;
        do {
            written = write(fileDescriptor, ptr, chunk);
        } while (written < 0 && errno == EINTR);
        if (written <= 0) {
            return false;
        }
        ptr += written;
        bytes -= static_cast<size_t>(written);
#endif
    }
    return true;
}

bool BinaryFileWriter::flush() {
    if (!ok) {
        return false;
    }
    if (used == 0) {
        return true;
    }
    ok = writeRawAll(buffer, used);
    used = 0;
    return ok;
}

bool BinaryFileWriter::writeBytes(const void* data, size_t bytes) {
    if (!ok) {
        return false;
    }
    if (bytes == 0) {
        return true;
    }
    if (bytes > capacity) {
        if (!flush()) {
            return false;
        }
        ok = writeRawAll(data, bytes);
        return ok;
    }
    if (used + bytes > capacity && !flush()) {
        return false;
    }
    std::memcpy(buffer + used, data, bytes);
    used += bytes;
    return true;
}

TcpClient::TcpClient() : socketHandle(INVALID_NATIVE_SOCKET) {}

TcpClient::TcpClient(uintptr_t nativeSocket) : socketHandle(nativeSocket) {}

TcpClient::~TcpClient() {
    close();
}

TcpClient::TcpClient(TcpClient&& other) noexcept
    : socketHandle(other.socketHandle) {
    other.socketHandle = INVALID_NATIVE_SOCKET;
}

TcpClient& TcpClient::operator=(TcpClient&& other) noexcept {
    if (this != &other) {
        close();
        socketHandle = other.socketHandle;
        other.socketHandle = INVALID_NATIVE_SOCKET;
    }
    return *this;
}

bool TcpClient::isValid() const {
    return validSocket(socketHandle);
}

bool TcpClient::setReceiveTimeoutMs(int timeoutMs) {
    if (!isValid()) {
        return false;
    }
#ifdef _WIN32
    DWORD timeoutValue = static_cast<DWORD>(timeoutMs);
    return setsockopt(static_cast<SOCKET>(socketHandle), SOL_SOCKET, SO_RCVTIMEO,
                      reinterpret_cast<const char*>(&timeoutValue),
                      sizeof(timeoutValue)) == 0;
#else
    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    return setsockopt(static_cast<int>(socketHandle), SOL_SOCKET, SO_RCVTIMEO,
                      &tv, sizeof(tv)) == 0;
#endif
}

int TcpClient::receive(char* buffer, size_t bufferSize) {
    if (!isValid() || bufferSize == 0) {
        return -1;
    }
#ifdef _WIN32
    int capped = bufferSize > static_cast<size_t>(std::numeric_limits<int>::max())
                     ? std::numeric_limits<int>::max()
                     : static_cast<int>(bufferSize);
    return recv(static_cast<SOCKET>(socketHandle), buffer, capped, 0);
#else
    ssize_t got = recv(static_cast<int>(socketHandle), buffer, bufferSize, 0);
    return got > static_cast<ssize_t>(std::numeric_limits<int>::max())
               ? std::numeric_limits<int>::max()
               : static_cast<int>(got);
#endif
}

bool TcpClient::sendAll(const char* data, size_t bytes) {
    if (!isValid()) {
        return false;
    }
    const char* ptr = data;
    while (bytes > 0) {
#ifdef _WIN32
        int chunk = bytes > static_cast<size_t>(std::numeric_limits<int>::max())
                        ? std::numeric_limits<int>::max()
                        : static_cast<int>(bytes);
        int sent = send(static_cast<SOCKET>(socketHandle), ptr, chunk, 0);
#else
        ssize_t sent = send(static_cast<int>(socketHandle), ptr, bytes, 0);
#endif
        if (sent <= 0) {
            return false;
        }
        ptr += sent;
        bytes -= static_cast<size_t>(sent);
    }
    return true;
}

void TcpClient::shutdownSend() {
    if (!isValid()) {
        return;
    }
#ifdef _WIN32
    shutdown(static_cast<SOCKET>(socketHandle), SD_SEND);
#else
    shutdown(static_cast<int>(socketHandle), SHUT_WR);
#endif
}

void TcpClient::drainAndClose() {
    if (!isValid()) {
        return;
    }
    shutdownSend();
    char discardBuffer[1024];
    while (receive(discardBuffer, sizeof(discardBuffer)) > 0) {}
    close();
}

void TcpClient::close() {
    if (!isValid()) {
        return;
    }
#ifdef _WIN32
    closesocket(static_cast<SOCKET>(socketHandle));
#else
    ::close(static_cast<int>(socketHandle));
#endif
    socketHandle = INVALID_NATIVE_SOCKET;
}

HttpServer::HttpServer()
    : socketHandle(INVALID_NATIVE_SOCKET), socketsStarted(false) {}

HttpServer::~HttpServer() {
    close();
#ifdef _WIN32
    if (socketsStarted) {
        WSACleanup();
    }
#endif
}

bool HttpServer::listenOn(int port, std::string& error) {
#ifdef _WIN32
    if (!socketsStarted) {
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            error = "WSAStartup that bai: " + std::to_string(result);
            return false;
        }
        socketsStarted = true;
    }
#endif

    socketHandle = static_cast<uintptr_t>(socket(AF_INET, SOCK_STREAM, 0));
    if (!isOpen()) {
        error = lastSocketErrorText("Tao socket");
        return false;
    }

    int optval = 1;
#ifdef _WIN32
    setsockopt(static_cast<SOCKET>(socketHandle), SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&optval), sizeof(optval));
#else
    setsockopt(static_cast<int>(socketHandle), SOL_SOCKET, SO_REUSEADDR,
               &optval, sizeof(optval));
#endif

    sockaddr_in service;
    std::memset(&service, 0, sizeof(service));
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = htonl(INADDR_ANY);
    service.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(
#ifdef _WIN32
            static_cast<SOCKET>(socketHandle),
#else
            static_cast<int>(socketHandle),
#endif
            reinterpret_cast<sockaddr*>(&service), sizeof(service)) != 0) {
        error = lastSocketErrorText("Bind");
        close();
        return false;
    }

    if (listen(
#ifdef _WIN32
            static_cast<SOCKET>(socketHandle),
#else
            static_cast<int>(socketHandle),
#endif
            SOMAXCONN) != 0) {
        error = lastSocketErrorText("Listen");
        close();
        return false;
    }

    return true;
}

TcpClient HttpServer::accept(std::string& error) {
    error.clear();
    if (!isOpen()) {
        error = "Server socket chua mo.";
        return TcpClient();
    }
#ifdef _WIN32
    SOCKET clientSocket = ::accept(static_cast<SOCKET>(socketHandle), NULL, NULL);
    if (clientSocket == INVALID_SOCKET) {
        error = lastSocketErrorText("Accept");
        return TcpClient();
    }
    return TcpClient(static_cast<uintptr_t>(clientSocket));
#else
    int clientSocket = ::accept(static_cast<int>(socketHandle), nullptr, nullptr);
    if (clientSocket < 0) {
        error = lastSocketErrorText("Accept");
        return TcpClient();
    }
    return TcpClient(static_cast<uintptr_t>(clientSocket));
#endif
}

bool HttpServer::isOpen() const {
    return validSocket(socketHandle);
}

void HttpServer::close() {
    if (!isOpen()) {
        return;
    }
#ifdef _WIN32
    closesocket(static_cast<SOCKET>(socketHandle));
#else
    ::close(static_cast<int>(socketHandle));
#endif
    socketHandle = INVALID_NATIVE_SOCKET;
}

}  // namespace platform
