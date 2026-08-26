#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ygo::core {

struct RulesBundlePaths {
    std::filesystem::path card_scripts_root;
    std::filesystem::path card_data_tsv;
    std::string bundle_id;
    std::string core_repository = "https://github.com/edo9300/ygopro-core.git";
    std::string core_commit = "9a0c558c2d686542f7914a6d529fd7aa57746aed";
    std::string cardscripts_repository = "https://github.com/ProjectIgnis/CardScripts.git";
    std::string cardscripts_commit = "f337c87018ca723c1aded5143e616bb649555273";
    std::string database_repository = "https://github.com/ProjectIgnis/BabelCDB.git";
    std::string database_commit = "89ad6837b0766a52984d8c715a7d5d4f8447946b";
    std::string core_api_version = "11.0";
    std::string core_patchset_id;
    std::string core_patchset_sha256;
};

struct FixtureDeck {
    std::vector<std::uint32_t> main_deck;
    std::vector<std::uint32_t> extra_deck;
    std::string sha256;
};

FixtureDeck load_fixture_deck(const std::filesystem::path& path);

std::vector<std::uint32_t> canonical_required_script_codes(const FixtureDeck& deck_a,
                                                            const FixtureDeck& deck_b);

}  // namespace ygo::core
