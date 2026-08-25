#include "ygo/trace/sha256.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint64_t kDefaultTargetBytes = 33'554'432;

void require(const bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void run_known_answer_tests() {
    require(ygo::trace::sha256_string("") ==
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "SHA-256 empty-string known answer failed");
    require(ygo::trace::sha256_string("abc") ==
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA-256 abc known answer failed");
    const std::string million_a(1'000'000, 'a');
    const auto million_a_hash =
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
    require(ygo::trace::sha256_string(million_a) == million_a_hash,
            "SHA-256 one-million-a string known answer failed");
    const std::vector<std::uint8_t> million_a_bytes(million_a.begin(), million_a.end());
    require(ygo::trace::sha256_bytes(million_a_bytes) == million_a_hash,
            "SHA-256 one-million-a byte known answer failed");
}

std::uint64_t parse_target_bytes(const int argc, char** argv) {
    std::uint64_t target_bytes = kDefaultTargetBytes;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help") {
            std::cout << "usage: sha256_benchmark [--target-bytes N]\n";
            std::exit(0);
        }
        constexpr std::string_view prefix = "--target-bytes=";
        if (argument.rfind(prefix, 0) == 0) {
            const auto value = argument.substr(prefix.size());
            if (value.empty()) {
                throw std::runtime_error("--target-bytes requires a positive integer");
            }
            std::size_t consumed = 0;
            const auto parsed = std::stoull(std::string(value), &consumed, 10);
            if (consumed != value.size() || parsed == 0) {
                throw std::runtime_error("--target-bytes requires a positive integer");
            }
            target_bytes = parsed;
            continue;
        }
        if (argument == "--target-bytes" && index + 1 < argc) {
            const std::string value(argv[++index]);
            std::size_t consumed = 0;
            const auto parsed = std::stoull(value, &consumed, 10);
            if (consumed != value.size() || parsed == 0) {
                throw std::runtime_error("--target-bytes requires a positive integer");
            }
            target_bytes = parsed;
            continue;
        }
        throw std::runtime_error("unknown argument: " + std::string(argument));
    }
    return target_bytes;
}

std::string make_payload(const std::size_t size) {
    std::string payload(size, '\0');
    for (std::size_t index = 0; index < size; ++index) {
        payload[index] = static_cast<char>((index * 131u + 17u) % 251u);
    }
    return payload;
}

struct CaseResult {
    std::size_t size_bytes = 0;
    std::uint64_t calls = 0;
    std::uint64_t total_bytes = 0;
    std::uint64_t elapsed_us = 0;
    double megabytes_per_second = 0.0;
    std::string hash;
};

CaseResult measure(const std::size_t size, const std::uint64_t target_bytes) {
    const auto payload = make_payload(size);
    const auto calls = std::max<std::uint64_t>(
        1, (target_bytes + static_cast<std::uint64_t>(size) - 1) / static_cast<std::uint64_t>(size));
    std::string final_hash;
    std::uint64_t sink = 0;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t call = 0; call < calls; ++call) {
        final_hash = ygo::trace::sha256_string(payload);
        sink ^= static_cast<std::uint64_t>(static_cast<unsigned char>(final_hash[call % final_hash.size()]))
                << (call % 8);
    }
    const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - start)
                                .count();
    const auto elapsed_us = std::max<std::int64_t>(1, elapsed_ns / 1'000);
    const auto total_bytes = calls * static_cast<std::uint64_t>(size);
    const auto megabytes_per_second =
        static_cast<double>(total_bytes) * 1'000'000'000.0 /
        static_cast<double>(std::max<std::int64_t>(1, elapsed_ns)) / 1'000'000.0;
    std::cerr << "sha256_benchmark_sink=" << sink << '\n';
    return {size, calls, total_bytes, static_cast<std::uint64_t>(elapsed_us), megabytes_per_second,
            std::move(final_hash)};
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto target_bytes = parse_target_bytes(argc, argv);
        run_known_answer_tests();
        constexpr std::array<std::size_t, 4> sizes = {4'096, 32'768, 139'264, 1'048'576};
        const std::array<CaseResult, sizes.size()> results = {
            measure(sizes[0], target_bytes), measure(sizes[1], target_bytes),
            measure(sizes[2], target_bytes), measure(sizes[3], target_bytes)};

#ifdef YGO_M0_COMPILER_ID
        constexpr std::string_view compiler_identity = YGO_M0_COMPILER_ID;
#else
        constexpr std::string_view compiler_identity = "unknown";
#endif
#ifdef YGO_M0_BUILD_TYPE
        constexpr std::string_view build_type = YGO_M0_BUILD_TYPE;
#else
        constexpr std::string_view build_type = "unknown";
#endif

        std::cout << '{';
        std::cout << "\"schema\":\"ocgforge.m4.sha256_benchmark.v1\",\"type\":\"sha256_benchmark\"";
        std::cout << ",\"compiler_identity\":\"" << compiler_identity << "\"";
        std::cout << ",\"build_type\":\"" << build_type << "\"";
        std::cout << ",\"target_bytes_per_size\":" << target_bytes;
        std::cout << ",\"known_answer_tests\":\"passed\",\"cases\":[";
        for (std::size_t index = 0; index < results.size(); ++index) {
            if (index != 0) {
                std::cout << ',';
            }
            const auto& result = results[index];
            std::cout << "{\"size_bytes\":" << result.size_bytes << ",\"calls\":" << result.calls
                      << ",\"total_bytes\":" << result.total_bytes
                      << ",\"elapsed_us\":" << result.elapsed_us
                      << ",\"megabytes_per_second\":" << result.megabytes_per_second
                      << ",\"hash\":\"" << result.hash << "\"}";
        }
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sha256_benchmark_error=" << error.what() << '\n';
        return 2;
    }
}
