#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ygo/observation/player_observation.hpp"
#include "ygo/observation/visible_event.hpp"

namespace ygo::i6c6_test {

struct NativeScenarioManifestV1 final {
    std::string scenario_contract_id;
    std::string scenario_id;
    std::uint8_t perspective_player = 0;
    std::uint8_t starting_player = 0;
    std::uint64_t duel_flags = 0;
    std::array<std::uint64_t, 4> seed_words{};
    std::string seat0_deck_id;
    std::string seat0_deck_sha256;
    std::string seat1_deck_id;
    std::string seat1_deck_sha256;
    std::string ocgforge_semantic_commit;
    std::string rules_bundle_id;
    std::string ocgforge_core_commit;
    std::string ocgforge_core_patchset_id;
    std::string ocgforge_core_patchset_sha256;
    std::string ocgforge_cardscripts_commit;
    std::string native_setup_descriptor_id;
    std::string native_raw_transcript_sha256;
    std::string native_public_event_transcript_sha256;
    std::string supported_message_family_envelope_id;
    std::uint64_t comparison_boundary_public_event_prefix_count = 0;
};

struct RuntimeForensicProvenanceV1 final {
    std::string ocgforge_babelcdb_commit;
    std::string ignis_edopro_commit;
    std::string ignis_edopro_core_commit;
    std::string ignis_cardscripts_commit;
    std::string ignis_babelcdb_commit;
};

struct NativeScenarioEvidenceV1 final {
    NativeScenarioManifestV1 manifest;
    RuntimeForensicProvenanceV1 runtime_provenance;
    std::vector<std::uint8_t> canonical_public_event_transcript;
    ygo::observation::PlayerObservation public_observation;
};

std::vector<std::uint8_t> canonical_public_event_transcript_bytes(
    const std::vector<ygo::observation::VisibleGameEvent>& events);

std::vector<std::uint8_t> canonical_restricted_raw_transcript_bytes(
    const std::vector<std::vector<std::uint8_t>>& framed_messages);

NativeScenarioEvidenceV1 build_native_scenario_evidence(
    std::uint8_t perspective_player);

}  // namespace ygo::i6c6_test
