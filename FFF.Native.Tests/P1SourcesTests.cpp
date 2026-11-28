#include "3FP/IO/ByteWindow.h"
#include "3FP/IO/FileMediaSource.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace FFFPlayer::Io;

static bool TestByteWindow() {
    ByteWindow window(8);
    window.Reset(100, 8);
    if (window.Length() != 0) return false;
    if (!window.Append(100, 4)) return false;
    if (!window.Contains(100, 4) || window.Contains(100, 5)) return false;
    if (window.Append(106, 2)) return false;          // gap
    if (!window.Append(104, 4)) return false;          // contiguous
    if (window.End() != 108 || window.Length() != 8) return false;
    if (!window.Append(108, 2)) return false;          // slides to [102,110)
    if (window.Start() != 102 || window.End() != 110) return false;
    if (window.Contains(101, 1) || !window.Contains(102, 8)) return false;
    if (window.EvictBefore(106) != 4 || window.Start() != 106) return false;
    window.SetCapacity(2);
    if (window.Start() != 108 || window.Length() != 2) return false;
    std::printf("PASS ByteWindow\n");
    return true;
}

static bool WriteFixture(const std::wstring& path, const std::vector<std::uint8_t>& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "CreateFileW fixture failed %lu path=%ls\n", GetLastError(), path.c_str());
        return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
        &written, nullptr) && written == bytes.size();
    if (!ok) std::fprintf(stderr, "WriteFile fixture failed %lu written=%lu\n", GetLastError(), written);
    CloseHandle(file);
    return ok;
}

static bool TestFileSource() {
    const std::wstring path = L"C:\\PLAN\\3FCompare\\3fp\\kernel\\FFF.Native.Tests\\p1_file_source.bin";
    std::vector<std::uint8_t> bytes(65536);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>((i * 131 + 17) & 0xff);
    if (!WriteFixture(path, bytes)) { std::fprintf(stderr, "fixture write failed\n"); return false; }

    IoOptions options;
    Result result;
    std::string error;
    auto source = FileMediaSource::Create(path, options, result, error);
    if (!source || result.status != Status::Ok) {
        std::fprintf(stderr, "file create failed: %s status=%u error=%u\n", error.c_str(),
            static_cast<unsigned>(result.status), static_cast<unsigned>(result.error));
        return false;
    }
    if (source->Size() != static_cast<std::int64_t>(bytes.size())) {
        std::fprintf(stderr, "size mismatch %lld\n", static_cast<long long>(source->Size()));
        return false;
    }
    if (!source->Capabilities().ByteExact() || !source->Capabilities().RandomAccess()) {
        std::fprintf(stderr, "capabilities mismatch\n");
        return false;
    }

    std::uint8_t buffer[4096]{};
    std::uint32_t read = 0;
    if (source->Read(buffer, sizeof(buffer), read, options).status != Status::Ok ||
        read != sizeof(buffer) || std::memcmp(buffer, bytes.data(), sizeof(buffer)) != 0) {
        std::fprintf(stderr, "first read failed read=%u\n", read);
        return false;
    }
    if (source->Seek(1000, options).status != Status::Ok) {
        std::fprintf(stderr, "seek 1000 failed\n");
        return false;
    }
    if (source->Read(buffer, sizeof(buffer), read, options).status != Status::Ok ||
        read != sizeof(buffer) ||
        std::memcmp(buffer, bytes.data() + 1000, sizeof(buffer)) != 0) {
        std::fprintf(stderr, "second read failed read=%u\n", read);
        return false;
    }
    if (source->Seek(static_cast<std::int64_t>(bytes.size()), options).status != Status::Ok) {
        std::fprintf(stderr, "seek end failed\n");
        return false;
    }
    if (source->Read(buffer, sizeof(buffer), read, options).status != Status::Eof || read != 0) {
        std::fprintf(stderr, "eof failed status=%u read=%u\n",
            static_cast<unsigned>(source->Read(buffer, sizeof(buffer), read, options).status), read);
        return false;
    }
    source->Close();

    auto missing = FileMediaSource::Create(L"C:\\__3fp_missing__\\nope.mp4", options,
        result, error);
    if (missing || result.status != Status::Fatal || result.error != Error::NotFound)
        return false;

    auto device = FileMediaSource::Create(L"\\\\.\\pipe\\3fp-test", options, result, error);
    if (device) return false;
    auto uncRoot = FileMediaSource::Create(L"\\\\server\\share", options, result, error);
    if (uncRoot) return false;

    DeleteFileW(path.c_str());
    std::printf("PASS FileMediaSource\n");
    return true;
}

int main() {
    if (!TestByteWindow()) return 1;
    if (!TestFileSource()) return 1;
    std::printf("PASS P1 sources\n");
    return 0;
}
