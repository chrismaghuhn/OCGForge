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

inline constexpr std::string_view kPublicActionIdentityV1SchemaId =
    "ocgforge.public_action_identity.v1";
inline constexpr std::string_view kPublicCandidateDomainV1SchemaId =
    "ocgforge.public_candidate_domain.v1";
inline constexpr std::string_view kPublicSemanticDecisionIdentityV1SchemaId =
    "ocgforge.public_semantic_decision_identity.v1";
inline constexpr std::string_view kPublicActionIdentityV2SchemaId =
    "ocgforge.public_action_identity.v2";
inline constexpr std::string_view kPublicCandidateDomainV2SchemaId =
    "ocgforge.public_candidate_domain.v2";
inline constexpr std::string_view kPublicSemanticDecisionIdentityV2SchemaId =
    "ocgforge.public_semantic_decision_identity.v2";
inline constexpr std::string_view kPublicActionIdentityV3SchemaId =
    "ocgforge.public_action_identity.v3";
inline constexpr std::string_view kPublicCandidateDomainV3SchemaId =
    "ocgforge.public_candidate_domain.v3";
inline constexpr std::string_view kPublicSemanticDecisionIdentityV3SchemaId =
    "ocgforge.public_semantic_decision_identity.v3";
inline constexpr std::string_view kEpisodicEnvironmentV2ContractId =
    "ocgforge.episodic_environment.v2";
inline constexpr std::string_view kEnvironmentIdentityV2SchemaId =
    "ocgforge.environment_identity.v2";
inline constexpr std::string_view kPublicActionKeyV1Prefix = "public_action.v1.";
inline constexpr std::string_view kPublicActionKeyV2Prefix = "public_action.v2.";
inline constexpr std::string_view kPublicActionKeyV3Prefix = "public_action.v3.";

// Historical V1 names remain source-compatible aliases. V2 callers must use
// the explicitly versioned successor APIs below.
inline constexpr std::string_view kPublicActionIdentitySchemaId =
    kPublicActionIdentityV1SchemaId;
inline constexpr std::string_view kPublicCandidateDomainSchemaId =
    kPublicCandidateDomainV1SchemaId;
inline constexpr std::string_view kPublicSemanticDecisionIdentitySchemaId =
    kPublicSemanticDecisionIdentityV1SchemaId;
inline constexpr std::string_view kPublicActionKeyPrefix = kPublicActionKeyV1Prefix;

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

enum class PublicCardSelectionOperation : std::uint8_t {
    None = 0,
    Select = 1,
    Unselect = 2,
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
    // Kept at the end to preserve existing positional aggregate initializers;
    // V2/V3 canonical bytes encode this field immediately after action kind.
    PublicCardSelectionOperation card_selection_operation =
        PublicCardSelectionOperation::None;
};

std::vector<std::uint8_t> canonical_public_action_key_bytes(
    const PublicActionKeyInput& input);
std::string public_action_key(const PublicActionKeyInput& input);
bool is_public_action_key(std::string_view key) noexcept;

std::vector<std::uint8_t> canonical_public_action_key_bytes_v2(
    const PublicActionKeyInput& input);
std::string public_action_key_v2(const PublicActionKeyInput& input);
bool is_public_action_key_v2(std::string_view key) noexcept;

std::vector<std::uint8_t> canonical_public_action_key_bytes_v3(
    const PublicActionKeyInput& input);
std::string public_action_key_v3(const PublicActionKeyInput& input);
bool is_public_action_key_v3(std::string_view key) noexcept;

std::vector<std::uint8_t> canonical_public_candidate_domain_bytes(
    std::string_view request_kind, const std::vector<std::string>& public_action_keys);
std::string public_candidate_domain_digest(std::string_view request_kind,
                                           const std::vector<std::string>& public_action_keys);

std::vector<std::uint8_t> canonical_public_candidate_domain_bytes_v2(
    std::string_view request_kind, const std::vector<std::string>& public_action_keys);
std::string public_candidate_domain_digest_v2(
    std::string_view request_kind, const std::vector<std::string>& public_action_keys);

std::vector<std::uint8_t> canonical_public_candidate_domain_bytes_v3(
    std::string_view request_kind, const std::vector<std::string>& public_action_keys);
std::string public_candidate_domain_digest_v3(
    std::string_view request_kind, const std::vector<std::string>& public_action_keys);

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

std::vector<std::uint8_t> canonical_public_semantic_decision_identity_bytes_v2(
    const PublicSemanticDecisionIdentityInput& input);
std::string public_semantic_decision_id_v2(const PublicSemanticDecisionIdentityInput& input);

struct PublicSemanticDecisionIdentityInputV3 final {
    std::string episode_semantic_id;
    std::uint64_t decision_index = 0;
    std::uint8_t acting_player = 0;
    std::string request_kind;
    std::string public_observation_digest;
    std::string public_candidate_domain_digest;
    std::vector<std::string> public_action_keys;
};

std::vector<std::uint8_t> canonical_public_semantic_decision_identity_bytes_v3(
    const PublicSemanticDecisionIdentityInputV3& input);
std::string public_semantic_decision_id_v3(
    const PublicSemanticDecisionIdentityInputV3& input);

}  // namespace ygo::environment
