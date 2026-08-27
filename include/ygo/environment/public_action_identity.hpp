#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ygo::environment {

// These helpers define the perspective-safe identity codec. Callers must
// populate PublicActionKeyInput only from fields already audited against the
// acting player's PlayerObservation; the codec cannot infer visibility from
// an internal ActionCandidate.

inline constexpr std::string_view kPublicActionIdentitySchemaId =
    "ocgforge.public_action_identity.v1";
inline constexpr std::string_view kPublicCandidateDomainSchemaId =
    "ocgforge.public_candidate_domain.v1";
inline constexpr std::string_view kPublicSemanticDecisionIdentitySchemaId =
    "ocgforge.public_semantic_decision_identity.v1";
inline constexpr std::string_view kEpisodicEnvironmentV2ContractId =
    "ocgforge.episodic_environment.v2";
inline constexpr std::string_view kEnvironmentIdentityV2SchemaId =
    "ocgforge.environment_identity.v2";
inline constexpr std::string_view kPublicActionKeyPrefix = "public_action.v1.";

enum class PublicChoiceKind : std::uint8_t {
    YesNo = 1,
    EffectYesNo = 2,
    EffectChoice = 3,
    OptionValue = 4,
    AnnouncementNumber = 5,
};

struct PublicChoice final {
    PublicChoiceKind kind = PublicChoiceKind::YesNo;
    std::uint64_t value = 0;
    // For option/announcement values this is the exact engine response
    // selector. It is not the environment's candidate-vector index.
    std::optional<std::uint32_t> response_index;
};

enum class PublicCardReferenceKind : std::uint8_t {
    VisibleCard = 0,
    RedactedSlot = 1,
};

struct PublicCardReference final {
    PublicCardReferenceKind kind = PublicCardReferenceKind::RedactedSlot;
    std::string observation_locator;
};

struct PublicActionKeyInput final {
    std::string action_kind;
    std::optional<PublicChoice> choice;
    std::optional<PublicCardReference> source_reference;
    std::optional<PublicCardReference> target_reference;
    std::optional<std::uint32_t> phase;
    std::optional<std::uint8_t> position;
    std::optional<std::uint32_t> source_index;
    std::optional<std::int32_t> amount;
    std::string continuation_operation;
};

std::vector<std::uint8_t> canonical_public_action_key_bytes(
    const PublicActionKeyInput& input);
std::string public_action_key(const PublicActionKeyInput& input);
bool is_public_action_key(std::string_view key) noexcept;

std::vector<std::uint8_t> canonical_public_candidate_domain_bytes(
    std::string_view request_kind, const std::vector<std::string>& public_action_keys);
std::string public_candidate_domain_digest(std::string_view request_kind,
                                           const std::vector<std::string>& public_action_keys);

struct PublicSemanticDecisionIdentityInput final {
    std::string episode_semantic_id;
    std::uint64_t decision_index = 0;
    std::uint8_t acting_player = 0;
    std::string request_kind;
    std::string public_observation_digest;
    std::string public_candidate_domain_digest;
};

std::vector<std::uint8_t> canonical_public_semantic_decision_identity_bytes(
    const PublicSemanticDecisionIdentityInput& input);
std::string public_semantic_decision_id(const PublicSemanticDecisionIdentityInput& input);

namespace detail {

struct PublicActionBinding final {
    std::string public_action_key;
    std::string internal_semantic_key;
};

// Returns no value for an unknown or ambiguous public key. This is an
// internal projection helper; the internal semantic key never crosses the
// public policy boundary.
std::optional<std::string> resolve_public_action_key(
    const std::vector<PublicActionBinding>& bindings, std::string_view public_key);

}  // namespace detail

}  // namespace ygo::environment
