#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace platform {
void setConsoleUtf8();
bool replaceFileAtomic(const std::string& from, const std::string& to);
double currentProcessPrivateMemoryMB();
uint64_t availablePhysicalMemoryBytes();
uint64_t currentProcessId();

class LineReader {
   private:
#ifdef _WIN32
    void* fileHandle;
#else
    int fileDescriptor;
#endif
    char* buffer;
    size_t bufferCapacity;
    size_t bufferSize;
    size_t bufferOffset;
    bool eof;
    long long currentFilePos;

    bool fillBuffer();
    void close();

   public:
    explicit LineReader(const std::string& path,
                        size_t bufferBytes = 8ULL * 1024ULL * 1024ULL);
    ~LineReader();

    LineReader(const LineReader&) = delete;
    LineReader& operator=(const LineReader&) = delete;

    bool isOpen() const;
    bool seek(long long offset);
    long long tell() const;
    bool readLine(std::string_view& line, std::string& scratch);
};

class BinaryMappedReader {
   private:
#ifdef _WIN32
    void* fileHandle;
    void* mappingHandle;
#else
    int fileDescriptor;
#endif
    const unsigned char* begin;
    const unsigned char* cursor;
    const unsigned char* end;
    size_t mappedBytes;

   public:
    explicit BinaryMappedReader(const std::string& filename);
    ~BinaryMappedReader();

    BinaryMappedReader(const BinaryMappedReader&) = delete;
    BinaryMappedReader& operator=(const BinaryMappedReader&) = delete;

    bool isOpen() const;
    bool readBytes(void* dst, size_t bytes);

    template <typename T>
    bool readPod(T& out) {
        return readBytes(&out, sizeof(T));
    }
};

class BinaryFileReader {
   private:
#ifdef _WIN32
    void* fileHandle;
#else
    int fileDescriptor;
#endif

   public:
    explicit BinaryFileReader(const std::string& filename);
    ~BinaryFileReader();

    BinaryFileReader(const BinaryFileReader&) = delete;
    BinaryFileReader& operator=(const BinaryFileReader&) = delete;

    bool isOpen() const;
    bool readBytes(void* dst, size_t bytes);

    template <typename T>
    bool readPod(T& out) {
        return readBytes(&out, sizeof(T));
    }
};

class BinaryFileWriter {
   private:
#ifdef _WIN32
    void* fileHandle;
#else
    int fileDescriptor;
#endif
    char* buffer;
    size_t capacity;
    size_t used;
    bool ok;

    bool writeRawAll(const void* data, size_t bytes);

   public:
    explicit BinaryFileWriter(const std::string& filename,
                              size_t bufferBytes = 4ULL * 1024ULL * 1024ULL);
    ~BinaryFileWriter();

    BinaryFileWriter(const BinaryFileWriter&) = delete;
    BinaryFileWriter& operator=(const BinaryFileWriter&) = delete;

    bool isOpen() const;
    bool flush();
    bool writeBytes(const void* data, size_t bytes);

    template <typename T>
    bool writePod(const T& value) {
        return writeBytes(&value, sizeof(value));
    }
};

class TcpClient {
   private:
    uintptr_t socketHandle;

   public:
    TcpClient();
    explicit TcpClient(uintptr_t nativeSocket);
    ~TcpClient();

    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;
    TcpClient(TcpClient&& other) noexcept;
    TcpClient& operator=(TcpClient&& other) noexcept;

    bool isValid() const;
    bool setReceiveTimeoutMs(int timeoutMs);
    int receive(char* buffer, size_t bufferSize);
    bool sendAll(const char* data, size_t bytes);
    void shutdownSend();
    void drainAndClose();
    void close();
};

class HttpServer {
   private:
    uintptr_t socketHandle;
    bool socketsStarted;

   public:
    HttpServer();
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    bool listenOn(int port, std::string& error);
    TcpClient accept(std::string& error);
    bool isOpen() const;
    void close();
};
}  // namespace platform
