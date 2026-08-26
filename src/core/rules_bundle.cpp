#include "ygo/core/rules_bundle.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace ygo::core {

namespace {

class Sha256 final {
public:
    Sha256() {
        state_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                  0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    }

    void update(const std::uint8_t* data, std::size_t length) {
        total_bytes_ += length;
        while (length != 0) {
            const std::size_t take = std::min(length, block_.size() - block_size_);
            std::copy(data, data + take, block_.begin() + static_cast<std::ptrdiff_t>(block_size_));
            block_size_ += take;
            data += take;
            length -= take;
            if (block_size_ == block_.size()) {
                transform(block_.data());
                block_size_ = 0;
            }
        }
    }

    std::string finish() {
        const std::uint64_t bit_length = total_bytes_ * 8;
        block_[block_size_++] = 0x80;
        if (block_size_ > 56) {
            std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_), block_.end(), 0);
            transform(block_.data());
            block_size_ = 0;
        }
        std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_), block_.begin() + 56, 0);
        for (std::size_t i = 0; i < 8; ++i) {
            block_[63 - i] = static_cast<std::uint8_t>(bit_length >> (i * 8));
        }
        transform(block_.data());

        static constexpr char hex[] = "0123456789abcdef";
        std::string result;
        result.reserve(64);
        for (const auto word : state_) {
            for (int shift = 28; shift >= 0; shift -= 4) {
                result.push_back(hex[(word >> shift) & 0xfu]);
            }
        }
        return result;
    }

private:
    static std::uint32_t rotate_right(std::uint32_t value, std::uint32_t amount) {
        return (value >> amount) | (value << (32 - amount));
    }

    void transform(const std::uint8_t* block) {
        static constexpr std::array<std::uint32_t, 64> constants = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
            0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
            0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
            0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
            0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
            0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
        std::array<std::uint32_t, 64> schedule{};
        for (std::size_t i = 0; i < 16; ++i) {
            schedule[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                          (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                          (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                          static_cast<std::uint32_t>(block[i * 4 + 3]);
        }
        for (std::size_t i = 16; i < schedule.size(); ++i) {
            const auto s0 = rotate_right(schedule[i - 15], 7) ^ rotate_right(schedule[i - 15], 18) ^ (schedule[i - 15] >> 3);
            const auto s1 = rotate_right(schedule[i - 2], 17) ^ rotate_right(schedule[i - 2], 19) ^ (schedule[i - 2] >> 10);
            schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
        }
        std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (std::size_t i = 0; i < schedule.size(); ++i) {
            const auto s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
            const auto choose = (e & f) ^ ((~e) & g);
            const auto temp1 = h + s1 + choose + constants[i] + schedule[i];
            const auto s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temp2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> block_{};
    std::size_t block_size_ = 0;
    std::uint64_t total_bytes_ = 0;
};

std::string sha256_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open fixture deck: " + path.string());
    }
    Sha256 digest;
    std::array<std::uint8_t, 4096> buffer{};
    while (stream) {
        stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const auto count = stream.gcount();
        if (count > 0) {
            digest.update(buffer.data(), static_cast<std::size_t>(count));
        }
    }
    return digest.finish();
}

}  // namespace

FixtureDeck load_fixture_deck(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open fixture deck: " + path.string());
    }

    FixtureDeck deck;
    std::string line;
    const bool ydk_format = path.extension() == ".ydk";
    enum class Section { None, Main, Extra, Side };
    Section section = ydk_format ? Section::None : Section::Main;
    while (std::getline(stream, line)) {
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())) != 0) {
            line.pop_back();
        }
        std::size_t first = 0;
        while (first < line.size() && std::isspace(static_cast<unsigned char>(line[first])) != 0) {
            ++first;
        }
        line.erase(0, first);
        if (line.empty()) {
            continue;
        }
        if (ydk_format) {
            if (line == "#main") {
                section = Section::Main;
                continue;
            }
            if (line == "#extra") {
                section = Section::Extra;
                continue;
            }
            if (line == "!side") {
                section = Section::Side;
                continue;
            }
            if (line.front() == '#') {
                continue;
            }
            if (section == Section::None) {
                throw std::runtime_error("YDK card appears before a section: " + line);
            }
            if (section == Section::Side) {
                throw std::runtime_error("fixture side deck must be empty: " + path.string());
            }
        } else {
            const auto comment = line.find('#');
            if (comment != std::string::npos) {
                line.erase(comment);
            }
            if (line.empty()) {
                continue;
            }
        }
        std::uint32_t code = 0;
        std::stringstream parser(line);
        parser >> code;
        if (!parser || !parser.eof()) {
            throw std::runtime_error("invalid card passcode in fixture deck: " + line);
        }
        if (ydk_format && section == Section::Extra) {
            deck.extra_deck.push_back(code);
        } else {
            deck.main_deck.push_back(code);
        }
    }
    if (deck.main_deck.size() < 40) {
        throw std::runtime_error("fixture deck has fewer than 40 entries: " + path.string());
    }

    deck.sha256 = sha256_file(path);
    return deck;
}

std::vector<std::uint32_t> canonical_required_script_codes(const FixtureDeck& deck_a,
                                                            const FixtureDeck& deck_b) {
    std::vector<std::uint32_t> codes = deck_a.main_deck;
    codes.insert(codes.end(), deck_a.extra_deck.begin(), deck_a.extra_deck.end());
    codes.insert(codes.end(), deck_b.main_deck.begin(), deck_b.main_deck.end());
    codes.insert(codes.end(), deck_b.extra_deck.begin(), deck_b.extra_deck.end());
    std::sort(codes.begin(), codes.end());
    codes.erase(std::unique(codes.begin(), codes.end()), codes.end());
    return codes;
}

}  // namespace ygo::core
