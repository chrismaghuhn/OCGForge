#include "ygo/phase6/task5c_gameplay.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>

#include "ygo/policy/production_provenance.hpp"
#include "ygo/policy/teacher.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/trace/sha256.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/identity_resolver.hpp"
#include "ygo/trajectory/recorder.hpp"
#include "ygo/trajectory/receipt.hpp"
#include "ygo/trajectory/restricted_evidence.hpp"
#include "ygo/trajectory/shard.hpp"

namespace ygo::phase6::task5c {
namespace {

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

[[noreturn]] void fail(const std::string& message) {
    throw std::invalid_argument(message);
}

bool lower_hex(const std::string_view value, const std::size_t expected_size) noexcept {
    if (value.size() != expected_size) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](const char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    });
}

bool nonzero_lower_hex(const std::string_view value, const std::size_t expected_size) noexcept {
    return lower_hex(value, expected_size) &&
           std::any_of(value.begin(), value.end(), [](const char character) {
               return character != '0';
           });
}

bool valid_utf8(const std::string_view value) noexcept {
    std::size_t index = 0;
    while (index < value.size()) {
        const auto byte = static_cast<unsigned char>(value[index]);
        if (byte <= 0x7f) {
            ++index;
            continue;
        }
        std::size_t continuation_count = 0;
        std::uint32_t code_point = 0;
        if (byte >= 0xc2 && byte <= 0xdf) {
            continuation_count = 1;
            code_point = byte & 0x1f;
        } else if (byte >= 0xe0 && byte <= 0xef) {
            continuation_count = 2;
            code_point = byte & 0x0f;
        } else if (byte >= 0xf0 && byte <= 0xf4) {
            continuation_count = 3;
            code_point = byte & 0x07;
        } else {
            return false;
        }
        if (index + continuation_count >= value.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset <= continuation_count; ++offset) {
            const auto continuation = static_cast<unsigned char>(value[index + offset]);
            if ((continuation & 0xc0) != 0x80) {
                return false;
            }
            code_point = (code_point << 6) | (continuation & 0x3f);
        }
        if ((continuation_count == 2 && code_point < 0x800) ||
            (continuation_count == 3 && code_point < 0x10000) ||
            (code_point >= 0xd800 && code_point <= 0xdfff) || code_point > 0x10ffff) {
            return false;
        }
        index += continuation_count + 1;
    }
    return true;
}

bool valid_nonempty_string(const std::string_view value) noexcept {
    return !value.empty() && valid_utf8(value);
}

bool valid_prefixed_digest(const std::string_view value,
                           const std::string_view prefix,
                           const bool reject_zero = true) noexcept {
    if (value.size() != prefix.size() + 64 || value.substr(0, prefix.size()) != prefix) {
        return false;
    }
    const auto digest = value.substr(prefix.size());
    return reject_zero ? nonzero_lower_hex(digest, 64) : lower_hex(digest, 64);
}

bool valid_commit(const std::string_view value) noexcept {
    return lower_hex(value, 40) &&
           std::any_of(value.begin(), value.end(), [](const char character) {
               return character != '0';
           });
}

bool is_smoke_checkpoint(const std::string_view value) noexcept {
    return value == kSmokeCheckpointIdentity;
}

bool byte_less(const std::string_view left, const std::string_view right) noexcept {
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(),
        [](const char left_byte, const char right_byte) {
            return static_cast<unsigned char>(left_byte) <
                   static_cast<unsigned char>(right_byte);
        });
}

class ByteWriter final {
public:
    void u8(const std::uint8_t value) { bytes_.push_back(value); }

    void u32(const std::uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xff));
        }
    }

    void u64(const std::uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xff));
        }
    }

    void boolean(const bool value) { u8(value ? 1 : 0); }

    void string(const std::string_view value) {
        if (value.size() > std::numeric_limits<std::uint32_t>::max() ||
            !valid_utf8(value)) {
            fail("canonical byte string is invalid");
        }
        u32(static_cast<std::uint32_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    void optional_string(const std::optional<std::string>& value) {
        u8(value.has_value() ? 1 : 0);
        if (value.has_value()) {
            string(*value);
        }
    }

    void optional_u8(const std::optional<std::uint8_t>& value) {
        u8(value.has_value() ? 1 : 0);
        if (value.has_value()) u8(*value);
    }

    void raw(const std::vector<std::uint8_t>& value) {
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    std::vector<std::uint8_t> take() { return std::move(bytes_); }

private:
    std::vector<std::uint8_t> bytes_;
};

std::string digest_identity(const std::string_view prefix,
                            const std::vector<std::uint8_t>& bytes) {
    return std::string(prefix) + trace::sha256_bytes(bytes);
}

struct FixedTeacherValues final {
    std::string_view artifact;
    std::string_view binding;
    std::string_view deck;
    std::string_view deck_sha256;
};

constexpr FixedTeacherValues kSwordsoulTeacher{
    "policy_artifact.v1.52f56b550a2a674430439d3db104a0b2281b69df79891573e4d71967e3d4310d",
    "ocgforge.teacher_policy_binding.v1.4f78a100a75f98b8c5a7845198984a8ea34db8b6a75b6fde396c19d2b3ca6d0c",
    kSwordsoulDeckIdentity,
    kSwordsoulDeckSha256};
constexpr FixedTeacherValues kSalamangreatTeacher{
    "policy_artifact.v1.a68642ee28f0dd53ebe4908994664f178b3d5cea6fb7c06421990729cd9c4527",
    "ocgforge.teacher_policy_binding.v1.ecbf2ae56dab29e93f319399a08930a3700466cd3d9ab553ef964fc109846c56",
    kSalamangreatDeckIdentity,
    kSalamangreatDeckSha256};

const FixedTeacherValues& teacher_values(const std::string_view deck_role) {
    if (deck_role == kSwordsoulDeckIdentity) return kSwordsoulTeacher;
    if (deck_role == kSalamangreatDeckIdentity) return kSalamangreatTeacher;
    fail("unknown fixed deck role");
}

void validate_job_strings(const EvaluationJobV1& job) {
    const auto fields = std::array<std::string_view, 33>{
        job.identity_domain,
        job.identity_schema,
        job.evaluation_schema_id,
        job.evaluation_schema_version,
        job.evaluation_contract_identity,
        job.corpus_profile_identity,
        job.job_kind,
        job.matchup_id,
        job.rules_bundle_id,
        job.format_id,
        job.duel_mode_id,
        job.seat_0_deck_role_id,
        job.seat_0_deck_content_sha256,
        job.seat_1_deck_role_id,
        job.seat_1_deck_content_sha256,
        job.evaluated_policy_checkpoint_identity,
        job.evaluated_policy_deck_role_id,
        job.opponent_policy_deck_role_id,
        job.phase5_logical_model_input_contract_id,
        job.phase5_encoded_model_input_contract_id,
        job.phase5_batch_layout_contract_id,
        job.card_vocabulary_contract_id,
        job.teacher_policy_producer_id,
        job.teacher_policy_sampling_id,
        job.teacher_policy_rng_id,
        job.teacher_policy_artifact_role_0_id,
        job.teacher_policy_binding_role_0_id,
        job.teacher_policy_artifact_role_1_id,
        job.teacher_policy_binding_role_1_id,
        job.opponent_policy_artifact_id,
        job.opponent_policy_binding_id,
        job.opponent_policy_role_id,
        job.evaluator_semantic_version,
    };
    for (const auto value : fields) {
        if (!valid_nonempty_string(value)) fail("evaluation job contains an invalid string");
    }
    if (!valid_commit(job.evaluator_semantic_source_commit)) {
        fail("evaluation job source commit is not immutable");
    }
    if (job.source_dataset_identity.has_value() || job.dataset_split_identity.has_value()) {
        fail("gameplay evaluation job cannot carry offline membership");
    }
}

void validate_job_impl(const EvaluationJobV1& job) {
    validate_job_strings(job);
    if (job.identity_domain != "ocgforge.phase6.evaluation_job_identity.v1" ||
        job.identity_schema != "ocgforge.phase6.evaluation_job_identity.v1" ||
        job.evaluation_schema_id != "ocgforge.phase6.task5.evaluation_job.v1" ||
        job.evaluation_schema_version != "v1" ||
        job.evaluation_contract_identity != kAcceptedEvaluationContractIdentity ||
        (job.corpus_profile_identity != kImplementationAcceptanceProfile &&
         job.corpus_profile_identity != kMeaningfulFixedMatchupProfile) ||
        job.job_kind != kGameplayJobKind || job.matchup_id != kMatchupIdentity ||
        job.rules_bundle_id != kRulesBundleIdentity || job.format_id != kFormatIdentity ||
        job.duel_mode_id != kDuelModeIdentity || job.duel_flags != kDuelFlags) {
        fail("evaluation job fixed binding is not accepted");
    }
    if (job.seat_0_deck_role_id == job.seat_1_deck_role_id ||
        (job.seat_0_deck_role_id != kSwordsoulDeckIdentity &&
         job.seat_0_deck_role_id != kSalamangreatDeckIdentity) ||
        (job.seat_1_deck_role_id != kSwordsoulDeckIdentity &&
         job.seat_1_deck_role_id != kSalamangreatDeckIdentity)) {
        fail("evaluation job does not contain the two locked decks");
    }
    const auto& role0 = teacher_values(job.seat_0_deck_role_id);
    const auto& role1 = teacher_values(job.seat_1_deck_role_id);
    if (job.seat_0_deck_content_sha256 != role0.deck_sha256 ||
        job.seat_1_deck_content_sha256 != role1.deck_sha256 ||
        job.evaluated_policy_seat > 1 || job.opponent_policy_seat > 1 ||
        job.evaluated_policy_seat == job.opponent_policy_seat ||
        job.starting_player > 1) {
        fail("evaluation job seat or deck binding is invalid");
    }
    const auto& evaluated_role = job.evaluated_policy_seat == 0 ? role0.deck : role1.deck;
    const auto& opponent_role = job.opponent_policy_seat == 0 ? role0.deck : role1.deck;
    if (job.evaluated_policy_deck_role_id != evaluated_role ||
        job.opponent_policy_deck_role_id != opponent_role ||
        job.opponent_policy_role_id != opponent_role) {
        fail("evaluation job policy/deck role binding is invalid");
    }
    if (!valid_prefixed_digest(job.evaluated_policy_checkpoint_identity,
                               "phase6_checkpoint.v1.")) {
        fail("evaluation job checkpoint identity is invalid");
    }
    if ((job.corpus_profile_identity == kImplementationAcceptanceProfile &&
         job.evaluated_policy_checkpoint_identity != kSmokeCheckpointIdentity) ||
        (job.corpus_profile_identity == kMeaningfulFixedMatchupProfile &&
         job.evaluated_policy_checkpoint_identity == kSmokeCheckpointIdentity)) {
        fail("evaluation job checkpoint/profile binding is not accepted");
    }
    if (job.phase5_logical_model_input_contract_id != "ocgforge.model_logical_input.v1" ||
        job.phase5_encoded_model_input_contract_id != "ocgforge.model_encoded_input.v1" ||
        job.phase5_batch_layout_contract_id != "ocgforge.model_batch_layout.v1" ||
        job.card_vocabulary_contract_id != "ocgforge.model_card_vocabulary.v1" ||
        job.teacher_policy_producer_id != "ocgforge.policy.teacher_core.v1" ||
        job.teacher_policy_sampling_id !=
            "ocgforge.policy.deterministic_lexicographic_argmax.v1" ||
        job.teacher_policy_rng_id != "ocgforge.no_policy_rng.v1") {
        fail("evaluation job Phase-5 or Teacher binding is invalid");
    }
    if (job.teacher_policy_artifact_role_0_id != role0.artifact ||
        job.teacher_policy_binding_role_0_id != role0.binding ||
        job.teacher_policy_artifact_role_1_id != role1.artifact ||
        job.teacher_policy_binding_role_1_id != role1.binding) {
        fail("evaluation job Teacher role mapping is invalid");
    }
    const auto& opponent_teacher = teacher_values(opponent_role);
    if (job.opponent_policy_artifact_id != opponent_teacher.artifact ||
        job.opponent_policy_binding_id != opponent_teacher.binding ||
        (job.deterministic_seed != 1 && job.deterministic_seed != 2)) {
        fail("evaluation job opponent or seed binding is invalid");
    }
}

EvaluationJobV1 make_job_for_placement(
    const std::uint64_t seed,
    const std::string_view seat_zero_role,
    const std::uint8_t evaluated_policy_seat,
    const std::uint8_t starting_player,
    const std::string& source_commit,
    const std::string& checkpoint_identity,
    const std::string_view profile) {
    const auto seat_one_role = seat_zero_role == kSwordsoulDeckIdentity
                                   ? kSalamangreatDeckIdentity
                                   : kSwordsoulDeckIdentity;
    if (evaluated_policy_seat > 1) fail("evaluation policy seat is invalid");
    const auto seat_role = [&](const std::uint8_t seat) -> std::string_view {
        return seat == 0 ? seat_zero_role : seat_one_role;
    };
    const auto opponent_policy_seat = static_cast<std::uint8_t>(1 - evaluated_policy_seat);
    const auto evaluated_role = seat_role(evaluated_policy_seat);
    const auto opponent_role = seat_role(opponent_policy_seat);
    EvaluationJobV1 job;
    job.seat_0_deck_role_id = std::string(seat_zero_role);
    job.seat_0_deck_content_sha256 = std::string(teacher_values(seat_zero_role).deck_sha256);
    job.seat_1_deck_role_id = std::string(seat_one_role);
    job.seat_1_deck_content_sha256 = std::string(teacher_values(seat_one_role).deck_sha256);
    job.corpus_profile_identity = std::string(profile);
    job.evaluated_policy_checkpoint_identity = checkpoint_identity;
    job.evaluated_policy_seat = evaluated_policy_seat;
    job.evaluated_policy_deck_role_id = std::string(evaluated_role);
    job.opponent_policy_seat = opponent_policy_seat;
    job.opponent_policy_deck_role_id = std::string(opponent_role);
    job.teacher_policy_artifact_role_0_id =
        std::string(teacher_values(seat_zero_role).artifact);
    job.teacher_policy_binding_role_0_id =
        std::string(teacher_values(seat_zero_role).binding);
    job.teacher_policy_artifact_role_1_id =
        std::string(teacher_values(seat_one_role).artifact);
    job.teacher_policy_binding_role_1_id =
        std::string(teacher_values(seat_one_role).binding);
    job.opponent_policy_artifact_id = std::string(teacher_values(opponent_role).artifact);
    job.opponent_policy_binding_id = std::string(teacher_values(opponent_role).binding);
    job.opponent_policy_role_id = std::string(opponent_role);
    job.deterministic_seed = seed;
    job.starting_player = starting_player;
    job.evaluator_semantic_source_commit = source_commit;
    validate_job_impl(job);
    return job;
}

EvaluationJobV1 make_job(const std::uint64_t seed,
                         const std::string_view seat_zero_role,
                         const std::uint8_t starting_player,
                         const std::string& source_commit,
                         const std::string& checkpoint_identity) {
    return make_job_for_placement(
        seed, seat_zero_role, 0, starting_player, source_commit, checkpoint_identity,
        kImplementationAcceptanceProfile);
}

void write_job_fields(ByteWriter& writer, const EvaluationJobV1& job) {
    writer.string(job.identity_domain);
    writer.string(job.identity_schema);
    writer.string(job.evaluation_schema_id);
    writer.string(job.evaluation_schema_version);
    writer.string(job.evaluation_contract_identity);
    writer.string(job.corpus_profile_identity);
    writer.string(job.job_kind);
    writer.string(job.matchup_id);
    writer.string(job.rules_bundle_id);
    writer.string(job.format_id);
    writer.string(job.duel_mode_id);
    writer.u64(job.duel_flags);
    writer.string(job.seat_0_deck_role_id);
    writer.string(job.seat_0_deck_content_sha256);
    writer.string(job.seat_1_deck_role_id);
    writer.string(job.seat_1_deck_content_sha256);
    writer.string(job.evaluated_policy_checkpoint_identity);
    writer.u8(job.evaluated_policy_seat);
    writer.string(job.evaluated_policy_deck_role_id);
    writer.u8(job.opponent_policy_seat);
    writer.string(job.opponent_policy_deck_role_id);
    writer.string(job.phase5_logical_model_input_contract_id);
    writer.string(job.phase5_encoded_model_input_contract_id);
    writer.string(job.phase5_batch_layout_contract_id);
    writer.string(job.card_vocabulary_contract_id);
    writer.string(job.teacher_policy_producer_id);
    writer.string(job.teacher_policy_sampling_id);
    writer.string(job.teacher_policy_rng_id);
    writer.string(job.teacher_policy_artifact_role_0_id);
    writer.string(job.teacher_policy_binding_role_0_id);
    writer.string(job.teacher_policy_artifact_role_1_id);
    writer.string(job.teacher_policy_binding_role_1_id);
    writer.string(job.opponent_policy_artifact_id);
    writer.string(job.opponent_policy_binding_id);
    writer.string(job.opponent_policy_role_id);
    writer.optional_string(job.source_dataset_identity);
    writer.optional_string(job.dataset_split_identity);
    writer.u64(job.deterministic_seed);
    writer.u8(job.starting_player);
    writer.string(job.evaluator_semantic_version);
    writer.string(job.evaluator_semantic_source_commit);
}

std::vector<std::uint8_t> canonical_context_corpus_bytes(
    const EvaluationContextV1& context,
    const std::vector<std::string>& job_ids) {
    ByteWriter writer;
    writer.string("ocgforge.phase6.evaluation_corpus_identity.v1");
    writer.string("ocgforge.phase6.evaluation_corpus_identity.v1");
    writer.string(context.evaluation_contract_identity);
    writer.string(context.corpus_profile_identity);
    writer.string(context.corpus_kind);
    writer.string(kMatchupIdentity);
    writer.string(kRulesBundleIdentity);
    writer.string(kFormatIdentity);
    writer.string(kDuelModeIdentity);
    writer.u64(kDuelFlags);
    writer.string(kSwordsoulDeckIdentity);
    writer.string(kSwordsoulDeckSha256);
    writer.string(kSalamangreatDeckIdentity);
    writer.string(kSalamangreatDeckSha256);
    writer.string(context.checkpoint_identity);
    writer.optional_string(std::nullopt);
    writer.optional_string(std::nullopt);
    writer.u32(static_cast<std::uint32_t>(job_ids.size()));
    for (const auto& job_id : job_ids) writer.string(job_id);
    return writer.take();
}

std::vector<std::uint8_t> canonical_context_root_bytes(const EvaluationContextV1& context) {
    ByteWriter writer;
    writer.string("ocgforge.phase6.evaluation_identity.v1");
    writer.string("ocgforge.phase6.evaluation_identity.v1");
    writer.string(context.evaluation_contract_identity);
    writer.string(context.evaluation_corpus_identity);
    writer.string(context.checkpoint_identity);
    writer.string(context.evaluator_semantic_version);
    writer.string(context.evaluator_semantic_source_commit);
    return writer.take();
}

std::vector<std::uint8_t> canonical_context_job_manifest_bytes(
    const EvaluationContextV1& context,
    const std::vector<std::string>& job_ids) {
    ByteWriter writer;
    writer.string("ocgforge.phase6.task5.evaluation_job_manifest.v1");
    writer.string("ocgforge.phase6.task5.evaluation_job_manifest.v1");
    writer.string(context.evaluation_identity);
    writer.string(context.evaluation_contract_identity);
    writer.string(context.evaluation_corpus_identity);
    writer.u32(static_cast<std::uint32_t>(job_ids.size()));
    for (const auto& job_id : job_ids) writer.string(job_id);
    return writer.take();
}

std::vector<std::string> context_job_ids(const EvaluationContextV1& context) {
    std::vector<std::string> result;
    result.reserve(context.jobs.size());
    for (const auto& job : context.jobs) result.push_back(evaluation_job_identity(job));
    return result;
}

policy::PolicySelection policy_failure(const policy::PolicyErrorCode code,
                                       std::string message) noexcept {
    policy::PolicySelection result;
    result.error = policy::PolicyError{code, std::move(message)};
    return result;
}

std::uint32_t score_bits_value(const std::string_view bits) {
    if (!lower_hex(bits, 8)) fail("score_f32_bits is not eight lowercase hex characters");
    std::uint32_t raw = 0;
    for (const char character : bits) {
        raw <<= 4;
        raw |= static_cast<std::uint32_t>(character <= '9' ? character - '0'
                                                             : character - 'a' + 10);
    }
    float value = 0.0f;
    std::memcpy(&value, &raw, sizeof(value));
    if (!std::isfinite(value)) fail("score_f32_bits is non-finite");
    return raw;
}

std::uint32_t selected_score_ordinal(const std::vector<std::string>& bits,
                                     const std::vector<std::string>& keys) {
    if (bits.empty() || bits.size() != keys.size()) fail("score vector cardinality is invalid");
    std::vector<std::uint32_t> raw;
    raw.reserve(bits.size());
    for (const auto& bit : bits) raw.push_back(score_bits_value(bit));
    const auto to_float = [](const std::uint32_t raw_bits) {
        float value = 0.0f;
        std::memcpy(&value, &raw_bits, sizeof(value));
        return value;
    };
    float best_value = to_float(raw.front());
    std::size_t best = 0;
    for (std::size_t index = 1; index < raw.size(); ++index) {
        const float candidate_value = to_float(raw[index]);
        if (candidate_value > best_value ||
            (candidate_value == best_value && byte_less(keys[index], keys[best]))) {
            best = index;
            best_value = candidate_value;
        }
    }
    if (best > std::numeric_limits<std::uint32_t>::max()) fail("selected ordinal overflows u32");
    return static_cast<std::uint32_t>(best);
}

bool valid_public_digest(const std::string_view value) noexcept {
    return lower_hex(value, 64);
}

bool validate_meaningful_checkpoint_binding(
    const MeaningfulCheckpointBindingV1& binding,
    const model::CardVocabularyV1& vocabulary,
    std::string* error = nullptr) noexcept {
    try {
        if (!binding.manifest_validated()) {
            fail("meaningful checkpoint binding was not issued by a validated loader");
        }
        if (!valid_prefixed_digest(binding.checkpoint_identity(), "phase6_checkpoint.v1.") ||
            is_smoke_checkpoint(binding.checkpoint_identity())) {
            fail("meaningful checkpoint binding is not a non-smoke checkpoint");
        }
        if (!valid_prefixed_digest(binding.model_architecture_config_identity(),
                                   "phase6_architecture_config.v1.") ||
            binding.phase5_logical_model_input_contract_identity() !=
                "ocgforge.model_logical_input.v1" ||
            binding.phase5_encoded_model_input_contract_identity() !=
                "ocgforge.model_encoded_input.v1" ||
            binding.phase5_batch_layout_contract_identity() !=
                "ocgforge.model_batch_layout.v1") {
            fail("meaningful checkpoint binding Phase-5 or architecture contract is invalid");
        }
        if (!valid_prefixed_digest(binding.card_vocabulary_identity(),
                                   "model_card_vocabulary.v1.") ||
            binding.card_vocabulary_identity() != vocabulary.identity()) {
            fail("meaningful checkpoint binding vocabulary is invalid");
        }
        (void)vocabulary.canonical_bytes();
        if (!nonzero_lower_hex(binding.dataset_identity(), 64) ||
            !valid_prefixed_digest(binding.dataset_split_identity(),
                                   "phase6_dataset_split.v1.") ||
            binding.training_contract_identity() != "ocgforge.phase6.bc_contract.v1" ||
            binding.canonical_weight_export_codec_identity() !=
                "ocgforge.phase6.canonical_weight_export.v1" ||
            !valid_prefixed_digest(binding.canonical_weight_content_identity(),
                                   "phase6_weight_content.v1.") ||
            binding.task7_materialization_schema_id() != kTask7MaterializationSchemaId ||
            binding.task7_materialization_config_identity() !=
                kTask7MaterializationConfigIdentity) {
            fail("meaningful checkpoint binding content is invalid");
        }
        return true;
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return false;
    } catch (...) {
        set_error(error, "meaningful checkpoint binding validation threw");
        return false;
    }
}

}  // namespace

std::string_view gameplay_job_status_name(const GameplayJobStatus status) noexcept {
    switch (status) {
    case GameplayJobStatus::TrustedWin: return "TRUSTED_WIN";
    case GameplayJobStatus::TrustedLoss: return "TRUSTED_LOSS";
    case GameplayJobStatus::TrustedDraw: return "TRUSTED_DRAW";
    case GameplayJobStatus::Interrupted: return "INTERRUPTED";
    case GameplayJobStatus::Failed: return "FAILED";
    case GameplayJobStatus::Quarantined: return "QUARANTINED";
    }
    return "UNKNOWN";
}

std::string_view replay_admission_status_name(const ReplayAdmissionStatus status) noexcept {
    switch (status) {
    case ReplayAdmissionStatus::NotRun: return "NOT_RUN";
    case ReplayAdmissionStatus::Passed: return "PASS";
    case ReplayAdmissionStatus::Failed: return "FAIL";
    case ReplayAdmissionStatus::Quarantined: return "QUARANTINED";
    }
    return "UNKNOWN";
}

std::string_view gameplay_failure_stage_name(const GameplayFailureStage stage) noexcept {
    switch (stage) {
    case GameplayFailureStage::BeforePublicDecision: return "before_public_decision";
    case GameplayFailureStage::PublicFrameValidation: return "public_frame_validation";
    case GameplayFailureStage::ModelInputValidation: return "model_input_validation";
    case GameplayFailureStage::Inference: return "inference";
    case GameplayFailureStage::Selection: return "selection";
    case GameplayFailureStage::Environment: return "environment";
    case GameplayFailureStage::Replay: return "replay";
    case GameplayFailureStage::Admission: return "admission";
    }
    return "unknown";
}

detail::PolicyFailureClassificationV1 detail::classify_policy_selection_failure(
    const bool evaluated_turn,
    const std::optional<CheckpointPolicyFailureV1>& evaluated_failure) noexcept {
    if (!evaluated_turn) {
        return detail::PolicyFailureClassificationV1{
            GameplayFailureStage::Environment, "OPPONENT_POLICY_FAILURE"};
    }
    if (evaluated_failure.has_value()) {
        return detail::PolicyFailureClassificationV1{
            evaluated_failure->stage, evaluated_failure->code};
    }
    return detail::PolicyFailureClassificationV1{
        GameplayFailureStage::Inference, "INFERENCE_FAILURE"};
}

std::vector<std::uint8_t> canonical_evaluation_job_bytes(const EvaluationJobV1& job) {
    validate_job_impl(job);
    ByteWriter writer;
    write_job_fields(writer, job);
    return writer.take();
}

std::string evaluation_job_identity(const EvaluationJobV1& job) {
    return digest_identity(kEvaluationJobIdentityPrefix, canonical_evaluation_job_bytes(job));
}

bool validate_evaluation_job(const EvaluationJobV1& job, std::string* error) noexcept {
    try {
        (void)canonical_evaluation_job_bytes(job);
        return true;
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return false;
    } catch (...) {
        set_error(error, "evaluation job validation threw");
        return false;
    }
}

std::string evaluation_corpus_identity(const EvaluationContextV1& context) {
    for (const auto& job : context.jobs) validate_job_impl(job);
    const auto ids = context_job_ids(context);
    return digest_identity(kEvaluationCorpusIdentityPrefix,
                           canonical_context_corpus_bytes(context, ids));
}

std::string evaluation_identity(const EvaluationContextV1& context) {
    return digest_identity(kEvaluationIdentityPrefix, canonical_context_root_bytes(context));
}

std::string evaluation_job_manifest_identity(const EvaluationContextV1& context) {
    const auto ids = context_job_ids(context);
    return digest_identity(kEvaluationJobManifestIdentityPrefix,
                           canonical_context_job_manifest_bytes(context, ids));
}

bool validate_evaluation_context(const EvaluationContextV1& context,
                                std::string* error) noexcept {
    try {
        const bool implementation_context =
            context.corpus_profile_identity == kImplementationAcceptanceProfile;
        const bool meaningful_context =
            context.corpus_profile_identity == kMeaningfulFixedMatchupProfile;
        if (context.evaluation_contract_identity != kAcceptedEvaluationContractIdentity ||
            (!implementation_context && !meaningful_context) ||
            context.corpus_kind !=
                (meaningful_context ? kMeaningfulFixedMatchupKind
                                    : kImplementationAcceptanceKind) ||
            !valid_prefixed_digest(context.checkpoint_identity, "phase6_checkpoint.v1.") ||
            (implementation_context && !is_smoke_checkpoint(context.checkpoint_identity)) ||
            (meaningful_context && is_smoke_checkpoint(context.checkpoint_identity)) ||
            context.evaluator_semantic_version != kEvaluatorSemanticVersion ||
            !valid_commit(context.evaluator_semantic_source_commit) ||
            context.jobs.size() != (meaningful_context ? 16u : 8u)) {
            fail("evaluation context fixed binding is not accepted");
        }
        for (const auto& job : context.jobs) {
            validate_job_impl(job);
            if (job.corpus_profile_identity != context.corpus_profile_identity ||
                job.evaluated_policy_checkpoint_identity != context.checkpoint_identity ||
                job.evaluator_semantic_source_commit != context.evaluator_semantic_source_commit ||
                job.evaluator_semantic_version != context.evaluator_semantic_version) {
                fail("evaluation context job binding differs from the root context");
            }
        }
        if (implementation_context) {
            const std::array<std::tuple<std::uint64_t, std::string_view, std::uint8_t>, 8>
                coords = {
                    std::make_tuple(std::uint64_t{1}, kSwordsoulDeckIdentity, std::uint8_t{0}),
                    std::make_tuple(std::uint64_t{1}, kSwordsoulDeckIdentity, std::uint8_t{1}),
                    std::make_tuple(std::uint64_t{1}, kSalamangreatDeckIdentity, std::uint8_t{0}),
                    std::make_tuple(std::uint64_t{1}, kSalamangreatDeckIdentity, std::uint8_t{1}),
                    std::make_tuple(std::uint64_t{2}, kSwordsoulDeckIdentity, std::uint8_t{0}),
                    std::make_tuple(std::uint64_t{2}, kSwordsoulDeckIdentity, std::uint8_t{1}),
                    std::make_tuple(std::uint64_t{2}, kSalamangreatDeckIdentity, std::uint8_t{0}),
                    std::make_tuple(std::uint64_t{2}, kSalamangreatDeckIdentity, std::uint8_t{1})};
            for (std::size_t index = 0; index < coords.size(); ++index) {
                const auto& [seed, role, starting_player] = coords[index];
                const auto expected = make_job(seed, role, starting_player,
                                               context.evaluator_semantic_source_commit,
                                               context.checkpoint_identity);
                if (canonical_evaluation_job_bytes(context.jobs[index]) !=
                    canonical_evaluation_job_bytes(expected)) {
                    fail("evaluation context is not the exact frozen eight-job schedule");
                }
            }
        } else {
            const std::array<std::pair<std::string_view, std::uint8_t>, 4> placements = {
                std::make_pair(kSwordsoulDeckIdentity, std::uint8_t{0}),
                std::make_pair(kSwordsoulDeckIdentity, std::uint8_t{1}),
                std::make_pair(kSalamangreatDeckIdentity, std::uint8_t{0}),
                std::make_pair(kSalamangreatDeckIdentity, std::uint8_t{1})};
            std::size_t index = 0;
            for (const auto seed : {std::uint64_t{1}, std::uint64_t{2}}) {
                for (const auto& [role, evaluated_policy_seat] : placements) {
                    for (const auto starting_player : {std::uint8_t{0}, std::uint8_t{1}}) {
                        const auto expected = make_job_for_placement(
                            seed, role, evaluated_policy_seat, starting_player,
                            context.evaluator_semantic_source_commit,
                            context.checkpoint_identity, kMeaningfulFixedMatchupProfile);
                        if (canonical_evaluation_job_bytes(context.jobs[index]) !=
                            canonical_evaluation_job_bytes(expected)) {
                            fail("evaluation context is not the exact frozen sixteen-job schedule");
                        }
                        ++index;
                    }
                }
            }
        }
        const auto ids = context_job_ids(context);
        if (context.evaluation_corpus_identity !=
                digest_identity(kEvaluationCorpusIdentityPrefix,
                                canonical_context_corpus_bytes(context, ids)) ||
            context.evaluation_identity !=
                digest_identity(kEvaluationIdentityPrefix, canonical_context_root_bytes(context))) {
            fail("evaluation context root or corpus identity does not recompute");
        }
        if (context.evaluation_job_manifest_identity !=
            digest_identity(kEvaluationJobManifestIdentityPrefix,
                            canonical_context_job_manifest_bytes(context, ids))) {
            fail("evaluation context job-manifest identity does not recompute");
        }
        return true;
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return false;
    } catch (...) {
        set_error(error, "evaluation context validation threw");
        return false;
    }
}

EvaluationContextV1 make_implementation_acceptance_context(
    std::string evaluator_semantic_source_commit,
    std::string checkpoint_identity) {
    if (!is_smoke_checkpoint(checkpoint_identity)) {
        fail("implementation acceptance is bound to the accepted smoke checkpoint");
    }
    EvaluationContextV1 context;
    context.evaluator_semantic_source_commit = std::move(evaluator_semantic_source_commit);
    context.checkpoint_identity = std::move(checkpoint_identity);
    const std::array<std::tuple<std::uint64_t, std::string_view, std::uint8_t>, 8> coordinates = {
        std::make_tuple(std::uint64_t{1}, kSwordsoulDeckIdentity, std::uint8_t{0}),
        std::make_tuple(std::uint64_t{1}, kSwordsoulDeckIdentity, std::uint8_t{1}),
        std::make_tuple(std::uint64_t{1}, kSalamangreatDeckIdentity, std::uint8_t{0}),
        std::make_tuple(std::uint64_t{1}, kSalamangreatDeckIdentity, std::uint8_t{1}),
        std::make_tuple(std::uint64_t{2}, kSwordsoulDeckIdentity, std::uint8_t{0}),
        std::make_tuple(std::uint64_t{2}, kSwordsoulDeckIdentity, std::uint8_t{1}),
        std::make_tuple(std::uint64_t{2}, kSalamangreatDeckIdentity, std::uint8_t{0}),
        std::make_tuple(std::uint64_t{2}, kSalamangreatDeckIdentity, std::uint8_t{1})};
    for (const auto& coordinate : coordinates) {
        context.jobs.push_back(make_job(
            std::get<0>(coordinate), std::get<1>(coordinate), std::get<2>(coordinate),
            context.evaluator_semantic_source_commit, context.checkpoint_identity));
    }
    const auto ids = context_job_ids(context);
    context.evaluation_corpus_identity =
        digest_identity(kEvaluationCorpusIdentityPrefix,
                        canonical_context_corpus_bytes(context, ids));
    context.evaluation_identity =
        digest_identity(kEvaluationIdentityPrefix, canonical_context_root_bytes(context));
    context.evaluation_job_manifest_identity =
        digest_identity(kEvaluationJobManifestIdentityPrefix,
                        canonical_context_job_manifest_bytes(context, ids));
    std::string error;
    if (!validate_evaluation_context(context, &error)) fail(error);
    return context;
}

MeaningfulFixedMatchupContextV1 make_meaningful_fixed_matchup_context(
    MeaningfulCheckpointBindingV1 checkpoint_binding,
    const model::CardVocabularyV1& concrete_vocabulary,
    std::string evaluator_semantic_source_commit) {
    std::string binding_error;
    if (!validate_meaningful_checkpoint_binding(
            checkpoint_binding, concrete_vocabulary, &binding_error)) {
        fail(binding_error);
    }
    if (!valid_commit(evaluator_semantic_source_commit)) {
        fail("meaningful evaluator source commit is not immutable");
    }

    EvaluationContextV1 context;
    context.corpus_profile_identity = std::string(kMeaningfulFixedMatchupProfile);
    context.corpus_kind = std::string(kMeaningfulFixedMatchupKind);
    context.checkpoint_identity = checkpoint_binding.checkpoint_identity();
    context.evaluator_semantic_source_commit = std::move(evaluator_semantic_source_commit);

    const std::array<std::pair<std::string_view, std::uint8_t>, 4> placements = {
        std::make_pair(kSwordsoulDeckIdentity, std::uint8_t{0}),
        std::make_pair(kSwordsoulDeckIdentity, std::uint8_t{1}),
        std::make_pair(kSalamangreatDeckIdentity, std::uint8_t{0}),
        std::make_pair(kSalamangreatDeckIdentity, std::uint8_t{1})};
    for (const auto seed : {std::uint64_t{1}, std::uint64_t{2}}) {
        for (const auto& [role, evaluated_policy_seat] : placements) {
            for (const auto starting_player : {std::uint8_t{0}, std::uint8_t{1}}) {
                context.jobs.push_back(make_job_for_placement(
                    seed, role, evaluated_policy_seat, starting_player,
                    context.evaluator_semantic_source_commit,
                    context.checkpoint_identity, kMeaningfulFixedMatchupProfile));
            }
        }
    }
    const auto ids = context_job_ids(context);
    context.evaluation_corpus_identity =
        digest_identity(kEvaluationCorpusIdentityPrefix,
                        canonical_context_corpus_bytes(context, ids));
    context.evaluation_identity =
        digest_identity(kEvaluationIdentityPrefix, canonical_context_root_bytes(context));
    context.evaluation_job_manifest_identity =
        digest_identity(kEvaluationJobManifestIdentityPrefix,
                        canonical_context_job_manifest_bytes(context, ids));
    std::string error;
    if (!validate_evaluation_context(context, &error)) fail(error);
    return MeaningfulFixedMatchupContextV1{
        std::move(context), std::move(checkpoint_binding)};
}

std::vector<std::uint8_t> canonical_inference_request_bytes(
    const InferenceRequestV1& request) {
    if (request.schema_id != kInferenceRequestSchemaId ||
        !valid_prefixed_digest(request.checkpoint_identity, "phase6_checkpoint.v1.") ||
        !valid_prefixed_digest(request.model_input_identity, "model_input.v1.", false) ||
        (!valid_public_digest(request.ordered_candidate_domain_identity) &&
         !valid_prefixed_digest(request.ordered_candidate_domain_identity,
                                "phase6_ordered_candidate_domain.v1.", false)) ||
        request.public_semantic_decision_id.has_value() &&
            !valid_public_digest(*request.public_semantic_decision_id) ||
        request.perspective_player > 1) {
        fail("inference request is not canonical");
    }
    ByteWriter writer;
    writer.string(request.schema_id);
    writer.string(request.checkpoint_identity);
    writer.string(request.model_input_identity);
    writer.string(request.ordered_candidate_domain_identity);
    writer.optional_string(request.public_semantic_decision_id);
    writer.u8(request.perspective_player);
    writer.u64(request.decision_index);
    return writer.take();
}

std::string inference_request_identity(const InferenceRequestV1& request) {
    return digest_identity(kInferenceRequestIdentityPrefix,
                           canonical_inference_request_bytes(request));
}

std::vector<std::uint8_t> canonical_inference_response_identity_bytes(
    const InferenceResponseV1& response) {
    if (response.schema_id != kInferenceResponseSchemaId ||
        !valid_prefixed_digest(response.request_identity, "phase6_inference_request.v1.") ||
        !valid_prefixed_digest(response.checkpoint_identity, "phase6_checkpoint.v1.") ||
        !valid_prefixed_digest(response.model_input_identity, "model_input.v1.", false) ||
        (!valid_public_digest(response.ordered_candidate_domain_identity) &&
         !valid_prefixed_digest(response.ordered_candidate_domain_identity,
                                "phase6_ordered_candidate_domain.v1.", false)) ||
        !environment::is_public_action_key(response.selected_public_action_key)) {
        fail("inference response identity fields are not canonical");
    }
    ByteWriter writer;
    writer.string(kInferenceResponseIdentityDomain);
    writer.string(kInferenceResponseIdentityDomain);
    writer.string(response.request_identity);
    writer.string(response.checkpoint_identity);
    writer.string(response.model_input_identity);
    writer.string(response.ordered_candidate_domain_identity);
    writer.u32(response.selected_candidate_ordinal);
    writer.string(response.selected_public_action_key);
    return writer.take();
}

std::string inference_response_identity(const InferenceResponseV1& response) {
    return digest_identity(kInferenceResponseIdentityPrefix,
                           canonical_inference_response_identity_bytes(response));
}

InferenceResponseCreateResult make_inference_response(
    const InferenceRequestV1& request,
    std::vector<std::string> score_f32_bits,
    const std::vector<std::string>& ordered_candidate_keys) noexcept {
    try {
        if (request.request_identity != inference_request_identity(request)) {
            fail("inference request identity does not recompute");
        }
        if (score_f32_bits.empty() ||
            score_f32_bits.size() > std::numeric_limits<std::uint32_t>::max() ||
            score_f32_bits.size() != ordered_candidate_keys.size()) {
            fail("inference response score vector is not the exact candidate width");
        }
        if (ordered_candidate_keys.empty()) fail("inference response candidate domain is empty");
        std::set<std::string> unique_keys;
        for (const auto& key : ordered_candidate_keys) {
            if (!environment::is_public_action_key(key) || !unique_keys.insert(key).second) {
                fail("inference response candidate domain is not canonical");
            }
        }
        for (const auto& bits : score_f32_bits) (void)score_bits_value(bits);
        InferenceResponseV1 response;
        response.request_identity = request.request_identity;
        response.checkpoint_identity = request.checkpoint_identity;
        response.model_input_identity = request.model_input_identity;
        response.ordered_candidate_domain_identity = request.ordered_candidate_domain_identity;
        response.score_count = static_cast<std::uint32_t>(score_f32_bits.size());
        response.score_f32_bits = std::move(score_f32_bits);
        response.selected_candidate_ordinal =
            selected_score_ordinal(response.score_f32_bits, ordered_candidate_keys);
        response.selected_public_action_key =
            ordered_candidate_keys[response.selected_candidate_ordinal];
        response.response_identity = inference_response_identity(response);
        return {std::optional<InferenceResponseV1>(std::move(response)), std::nullopt};
    } catch (const std::exception& exception) {
        return {std::nullopt, exception.what()};
    } catch (...) {
        return {std::nullopt, "inference response construction threw"};
    }
}

namespace {

bool validate_inference_response_binding(const InferenceRequestV1& request,
                                         const InferenceResponseV1& response,
                                         std::string* error,
                                         const std::optional<std::size_t>&
                                             expected_candidate_count = std::nullopt) noexcept {
    try {
        if (request.request_identity != inference_request_identity(request) ||
            response.schema_id != kInferenceResponseSchemaId ||
            response.request_identity != request.request_identity ||
            response.checkpoint_identity != request.checkpoint_identity ||
            response.model_input_identity != request.model_input_identity ||
            response.ordered_candidate_domain_identity !=
                request.ordered_candidate_domain_identity ||
            response.score_count != response.score_f32_bits.size() ||
            response.score_count == 0 || response.score_f32_bits.empty() ||
            (expected_candidate_count.has_value() &&
             response.score_f32_bits.size() != *expected_candidate_count)) {
            fail("inference response does not bind the current request");
        }
        for (const auto& bits : response.score_f32_bits) (void)score_bits_value(bits);

        // A canonical selection identity can be checked before selection
        // attribution.  An invalid public key is deliberately deferred to
        // the selection stage so a current response with a bad selection is
        // not confused with a stale or wrongly bound response.
        if (environment::is_public_action_key(response.selected_public_action_key) &&
            response.response_identity != inference_response_identity(response)) {
            fail("inference response identity does not recompute");
        }
        return true;
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return false;
    } catch (...) {
        set_error(error, "inference response binding validation threw");
        return false;
    }
}

}  // namespace

bool validate_inference_response(const InferenceRequestV1& request,
                                 const InferenceResponseV1& response,
                                 std::string* error) noexcept {
    try {
        if (!validate_inference_response_binding(request, response, error)) return false;
        if (response.selected_candidate_ordinal >= response.score_count ||
            !environment::is_public_action_key(response.selected_public_action_key)) {
            fail("inference response selection is invalid");
        }
        return true;
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return false;
    } catch (...) {
        set_error(error, "inference response validation threw");
        return false;
    }
}

CheckpointBoundPolicyCreateResult create_checkpoint_bound_policy(
    std::string checkpoint_identity,
    const std::uint8_t participant,
    std::string participant_policy_assignment_id,
    std::string policy_artifact_id,
    model::CardVocabularyV1 vocabulary,
    CheckpointInferenceProviderV1 provider) noexcept {
    try {
        if (!valid_prefixed_digest(checkpoint_identity, "phase6_checkpoint.v1.") ||
            participant > 1 ||
            !valid_prefixed_digest(participant_policy_assignment_id,
                                   "participant_policy_assignment.v1.") ||
            !valid_prefixed_digest(policy_artifact_id, "policy_artifact.v1.") ||
            (is_smoke_checkpoint(checkpoint_identity) &&
             vocabulary.identity() != kSmokeCardVocabularyIdentity) ||
            !provider) {
            fail("checkpoint-bound policy configuration is invalid");
        }
        CheckpointBoundPolicyV1 policy(
            std::move(checkpoint_identity), participant,
            std::move(participant_policy_assignment_id), std::move(policy_artifact_id),
            std::move(vocabulary), std::move(provider));
        return CheckpointBoundPolicyCreateResult{
            std::optional<CheckpointBoundPolicyV1>(std::move(policy)), std::nullopt};
    } catch (const std::exception& exception) {
        return {std::nullopt,
                policy::PolicyError{policy::PolicyErrorCode::InvalidConfiguration,
                                    exception.what()}};
    } catch (...) {
        return {std::nullopt,
                policy::PolicyError{policy::PolicyErrorCode::InvalidConfiguration,
                                    "checkpoint-bound policy construction threw"}};
    }
}

policy::PolicySelection CheckpointBoundPolicyV1::fail_with_stage(
    const GameplayFailureStage stage, std::string code,
    const policy::PolicyErrorCode policy_code, std::string message) noexcept {
    last_failure_ = CheckpointPolicyFailureV1{stage, std::move(code)};
    return policy_failure(policy_code, std::move(message));
}

policy::PolicySelection CheckpointBoundPolicyV1::select(
    const environment::DecisionFrame& frame) noexcept {
    try {
        last_failure_.reset();
        if (pending_.has_value()) {
            return fail_with_stage(
                GameplayFailureStage::Inference, "INFERENCE_FAILURE",
                policy::PolicyErrorCode::LifecycleFailure,
                "checkpoint policy has an unresolved inference response");
        }
        if (frame.contract_id != environment::kEpisodicEnvironmentV2ContractId ||
            frame.acting_player != participant_ ||
            frame.public_observation.perspective_player != participant_ ||
            frame.public_observation.decision_index != frame.decision_index ||
            frame.request.player != participant_ || !frame.submission_token.valid() ||
            !valid_public_digest(frame.public_observation_digest) ||
            frame.request.candidates.empty()) {
            return fail_with_stage(
                GameplayFailureStage::PublicFrameValidation, "PUBLIC_FRAME_INVALID",
                policy::PolicyErrorCode::InvalidCandidateDomain,
                "checkpoint policy received an invalid public frame");
        }
        std::string expected_observation_digest;
        try {
            expected_observation_digest =
                environment::public_observation_digest(frame.public_observation);
        } catch (...) {
            return fail_with_stage(
                GameplayFailureStage::PublicFrameValidation, "PUBLIC_FRAME_INVALID",
                policy::PolicyErrorCode::InvalidCandidateDomain,
                "checkpoint policy could not validate the public observation");
        }
        if (frame.public_observation_digest != expected_observation_digest) {
            return fail_with_stage(
                GameplayFailureStage::PublicFrameValidation, "PUBLIC_FRAME_INVALID",
                policy::PolicyErrorCode::InvalidCandidateDomain,
                "checkpoint policy received a mismatched public observation digest");
        }
        std::vector<std::string> keys;
        keys.reserve(frame.request.candidates.size());
        std::set<std::string> unique_keys;
        for (const auto& candidate : frame.request.candidates) {
            if (!environment::is_public_action_key(candidate.public_action_key) ||
                !unique_keys.insert(candidate.public_action_key).second) {
                return fail_with_stage(
                    GameplayFailureStage::PublicFrameValidation, "PUBLIC_FRAME_INVALID",
                    policy::PolicyErrorCode::InvalidCandidateDomain,
                    "checkpoint policy received an invalid candidate domain");
            }
            keys.push_back(candidate.public_action_key);
        }
        const auto request_kind =
            std::string(environment::environment_decision_kind_name(frame.request.kind));
        if (request_kind.empty()) {
            return fail_with_stage(
                GameplayFailureStage::PublicFrameValidation, "PUBLIC_FRAME_INVALID",
                policy::PolicyErrorCode::InvalidCandidateDomain,
                "checkpoint policy received an unsupported decision kind");
        }
        std::string expected_candidate_digest;
        try {
            expected_candidate_digest =
                environment::public_candidate_domain_digest(request_kind, keys);
        } catch (...) {
            return fail_with_stage(
                GameplayFailureStage::PublicFrameValidation, "PUBLIC_FRAME_INVALID",
                policy::PolicyErrorCode::InvalidCandidateDomain,
                "checkpoint policy could not validate the public candidate domain");
        }
        if (frame.public_candidate_domain_digest != expected_candidate_digest) {
            return fail_with_stage(
                GameplayFailureStage::PublicFrameValidation, "PUBLIC_FRAME_INVALID",
                policy::PolicyErrorCode::InvalidCandidateDomain,
                "checkpoint policy received a mismatched candidate digest");
        }
        environment::PublicSemanticDecisionIdentityInput decision_identity;
        decision_identity.episode_semantic_id = frame.episode_semantic_id;
        decision_identity.decision_index = frame.decision_index;
        decision_identity.acting_player = frame.acting_player;
        decision_identity.request_kind = request_kind;
        decision_identity.public_observation_digest = frame.public_observation_digest;
        decision_identity.public_candidate_domain_digest = frame.public_candidate_domain_digest;
        std::string expected_decision_id;
        try {
            expected_decision_id = environment::public_semantic_decision_id(decision_identity);
        } catch (...) {
            return fail_with_stage(
                GameplayFailureStage::PublicFrameValidation, "PUBLIC_FRAME_INVALID",
                policy::PolicyErrorCode::InvalidCandidateDomain,
                "checkpoint policy could not validate the public decision identity");
        }
        if (expected_decision_id != frame.public_semantic_decision_id) {
            return fail_with_stage(
                GameplayFailureStage::PublicFrameValidation, "PUBLIC_FRAME_INVALID",
                policy::PolicyErrorCode::InvalidCandidateDomain,
                "checkpoint policy received a mismatched decision identity");
        }

        model::LogicalModelProjectionResult logical;
        try {
            logical = model::project_logical_model_input_v1(
                frame.public_observation, frame.request.candidates);
        } catch (...) {
            return fail_with_stage(
                GameplayFailureStage::ModelInputValidation, "MODEL_INPUT_INVALID",
                policy::PolicyErrorCode::InvalidConfiguration,
                "public model-input projection threw");
        }
        if (!logical || !logical.value.has_value()) {
            return fail_with_stage(
                GameplayFailureStage::ModelInputValidation, "MODEL_INPUT_INVALID",
                policy::PolicyErrorCode::InvalidConfiguration,
                "public model-input projection failed");
        }
        model::EncodedModelInputResult encoded;
        try {
            encoded = model::encode_model_input_v1(*logical.value, vocabulary_);
        } catch (...) {
            return fail_with_stage(
                GameplayFailureStage::ModelInputValidation, "MODEL_INPUT_INVALID",
                policy::PolicyErrorCode::InvalidConfiguration,
                "encoded model-input projection threw");
        }
        if (!encoded || !encoded.value.has_value()) {
            return fail_with_stage(
                GameplayFailureStage::ModelInputValidation, "MODEL_INPUT_INVALID",
                policy::PolicyErrorCode::InvalidConfiguration,
                "encoded model-input projection failed");
        }
        if (encoded.value->routing_keys != keys ||
            encoded.value->public_candidate_domain_digest !=
                std::optional<std::string>{frame.public_candidate_domain_digest}) {
            return fail_with_stage(
                GameplayFailureStage::ModelInputValidation, "MODEL_INPUT_INVALID",
                policy::PolicyErrorCode::InvalidCandidateDomain,
                "encoded model input changed the current candidate domain");
        }
        InferenceRequestV1 request;
        request.checkpoint_identity = checkpoint_identity_;
        try {
            request.model_input_identity =
                model::model_input_identity(*logical.value, *encoded.value);
        } catch (...) {
            return fail_with_stage(
                GameplayFailureStage::ModelInputValidation, "MODEL_INPUT_INVALID",
                policy::PolicyErrorCode::InvalidConfiguration,
                "model-input identity could not be computed");
        }
        request.ordered_candidate_domain_identity = frame.public_candidate_domain_digest;
        request.public_semantic_decision_id = frame.public_semantic_decision_id;
        request.perspective_player = frame.acting_player;
        request.decision_index = frame.decision_index;
        try {
            request.request_identity = inference_request_identity(request);
        } catch (...) {
            return fail_with_stage(
                GameplayFailureStage::Inference, "INFERENCE_FAILURE",
                policy::PolicyErrorCode::LifecycleFailure,
                "inference request identity could not be computed");
        }

        InferenceResponseCreateResult provider_result;
        try {
            provider_result = provider_(request, *logical.value, *encoded.value);
        } catch (...) {
            return fail_with_stage(
                GameplayFailureStage::Inference, "INFERENCE_FAILURE",
                policy::PolicyErrorCode::LifecycleFailure,
                "checkpoint inference failed");
        }
        if (!provider_result || !provider_result.value.has_value()) {
            return fail_with_stage(
                GameplayFailureStage::Inference, "INFERENCE_FAILURE",
                policy::PolicyErrorCode::LifecycleFailure,
                "checkpoint inference failed");
        }
        auto response = *provider_result.value;
        std::string response_error;
        if (!validate_inference_response_binding(request, response, &response_error,
                                                 keys.size())) {
            return fail_with_stage(
                GameplayFailureStage::Inference, "INFERENCE_RESPONSE_INVALID",
                policy::PolicyErrorCode::LifecycleFailure,
                "checkpoint inference response rejected: " + response_error);
        }
        if (response.selected_candidate_ordinal >= keys.size() ||
            keys[response.selected_candidate_ordinal] != response.selected_public_action_key ||
            selected_score_ordinal(response.score_f32_bits, keys) !=
                response.selected_candidate_ordinal) {
            return fail_with_stage(
                GameplayFailureStage::Selection, "SELECTION_INVALID",
                policy::PolicyErrorCode::InvalidCandidateDomain,
                "checkpoint inference selected an invalid or non-deterministic key");
        }
        pending_ = Pending{frame.episode_semantic_id, frame.public_semantic_decision_id,
                           response.selected_public_action_key};
        return policy::PolicySelection{
            std::optional<policy::PolicySelectionResult>(
                policy::PolicySelectionResult{response.selected_public_action_key, std::nullopt}),
            std::nullopt};
    } catch (const std::exception& exception) {
        return fail_with_stage(
            GameplayFailureStage::Inference, "INFERENCE_FAILURE",
            policy::PolicyErrorCode::LifecycleFailure, exception.what());
    } catch (...) {
        return fail_with_stage(
            GameplayFailureStage::Inference, "INFERENCE_FAILURE",
            policy::PolicyErrorCode::LifecycleFailure,
            "checkpoint policy selection threw");
    }
}

bool CheckpointBoundPolicyV1::commit(
    const environment::AcceptedActionTransition& transition) noexcept {
    if (!pending_.has_value()) return false;
    const bool matches = pending_->episode_semantic_id == transition.episode_semantic_id &&
                         pending_->public_semantic_decision_id ==
                             transition.public_semantic_decision_id &&
                         pending_->selected_public_action_key ==
                             transition.selected_public_action_key;
    pending_.reset();
    return matches;
}

void CheckpointBoundPolicyV1::reject_pending_proposal() noexcept {
    pending_.reset();
}

policy::PolicyExecutionBinding CheckpointBoundPolicyV1::execution_binding() const {
    return policy::PolicyExecutionBinding{
        policy_artifact_id_,
        assignment_id_,
        std::string(trajectory::kNoPolicyRngContractId),
        std::string(trajectory::kNoPolicyRngContractId),
        std::string(trajectory::kNoPolicyRngContractId),
        std::string(trajectory::kNoPolicyRngContractId)};
}

namespace {

bool valid_status(const GameplayJobStatus status) noexcept {
    return static_cast<std::uint8_t>(status) <=
           static_cast<std::uint8_t>(GameplayJobStatus::Quarantined);
}

bool valid_replay_status(const ReplayAdmissionStatus status) noexcept {
    return static_cast<std::uint8_t>(status) <=
           static_cast<std::uint8_t>(ReplayAdmissionStatus::Quarantined);
}

bool valid_failure_stage(const GameplayFailureStage stage) noexcept {
    return static_cast<std::uint8_t>(stage) <=
           static_cast<std::uint8_t>(GameplayFailureStage::Admission);
}

void write_terminal_outcome(ByteWriter& writer, const TerminalOutcomeV1& value) {
    if (!value.terminal) fail("terminal outcome must be terminal");
    writer.boolean(value.terminal);
    writer.optional_u8(value.winner);
    writer.optional_u8(value.win_reason);
}

void validate_optional_public_ids(const std::optional<std::string>& trajectory_id,
                                  const std::optional<std::string>& gameplay_id) {
    if (trajectory_id.has_value() &&
        !valid_prefixed_digest(*trajectory_id, "trajectory_record.v1.", false)) {
        fail("trajectory record identity is not public-canonical");
    }
    if (gameplay_id.has_value() &&
        !valid_prefixed_digest(*gameplay_id, "public_gameplay_trajectory.v1.", false)) {
        fail("public gameplay identity is not canonical");
    }
}

void validate_failure_fields(const std::optional<GameplayFailureStage>& stage,
                             const std::optional<std::string>& code) {
    if (stage.has_value() && !valid_failure_stage(*stage)) fail("unknown gameplay failure stage");
    if (code.has_value()) {
        constexpr std::array<std::string_view, 28> public_failure_codes = {
            "INFERENCE_FAILURE",
            "INFERENCE_RESPONSE_INVALID",
            "STEP_REJECTED",
            "STEP_REJECTION_RECORDING_FAILURE",
            "STEP_REJECTION_INTERRUPT_FAILURE",
            "POLICY_FAILURE_INTERRUPT_FAILURE",
            "INVALID_DECISION_FRAME",
            "PUBLIC_FRAME_INVALID",
            "MODEL_INPUT_INVALID",
            "SELECTION_INVALID",
            "POLICY_COMMIT_FAILURE",
            "ENVIRONMENT_FACTORY_REJECTED",
            "RESET_REJECTED",
            "RESET_RECORDING_FAILURE",
            "TERMINAL_VIEW_FAILURE",
            "UNKNOWN_STEP_RESULT",
            "STEP_RECORDING_FAILURE",
            "TRAJECTORY_SEAL_FAILURE",
            "REPLAY_FAILURE",
            "ADMISSION_FAILURE",
            "ADMISSION_RECEIPT_FAILURE",
            "FAILED_TRAJECTORY_CLOSURE",
            "QUARANTINED_TRAJECTORY_PASSED_ADMISSION",
            "GAMEPLAY_FINALIZATION_FAILURE",
            "GAMEPLAY_JOB_EXCEPTION",
            "EVALUATOR_ALREADY_RAN",
            "OPPONENT_POLICY_FAILURE",
            "EVALUATOR_INTERNAL_FAILURE"};
        if (!valid_nonempty_string(*code) ||
            std::find(public_failure_codes.begin(), public_failure_codes.end(), *code) ==
                public_failure_codes.end()) {
            fail("failure code is not an accepted public token");
        }
    }
    if (stage.has_value() != code.has_value()) {
        fail("gameplay failure stage/code must be paired");
    }
}

}  // namespace

std::vector<std::uint8_t> canonical_replay_admission_summary_bytes(
    const ReplayAdmissionSummaryV1& summary) {
    if (!validate_replay_admission_summary(summary, nullptr)) {
        fail("replay/admission summary is invalid");
    }
    ByteWriter writer;
    writer.string(kReplayAdmissionSummaryIdentityDomain);
    writer.string(kReplayAdmissionSummaryIdentityDomain);
    writer.string(summary.schema_id);
    writer.string(summary.evaluation_identity);
    writer.string(summary.evaluation_job_identity);
    writer.optional_string(summary.trajectory_record_id);
    writer.optional_string(summary.public_gameplay_trajectory_id);
    writer.u8(static_cast<std::uint8_t>(summary.replay_status));
    writer.u8(static_cast<std::uint8_t>(summary.admission_status));
    writer.u8(summary.failure_stage.has_value() ? 1 : 0);
    if (summary.failure_stage.has_value()) writer.u8(static_cast<std::uint8_t>(*summary.failure_stage));
    writer.optional_string(summary.failure_code);
    writer.boolean(summary.fallback_assisted);
    return writer.take();
}

std::string replay_admission_summary_identity(
    const ReplayAdmissionSummaryV1& summary) {
    return digest_identity(kReplayAdmissionSummaryIdentityPrefix,
                           canonical_replay_admission_summary_bytes(summary));
}

bool validate_replay_admission_summary(
    const ReplayAdmissionSummaryV1& summary,
    std::string* error) noexcept {
    try {
        if (summary.schema_id != kReplayAdmissionSummarySchemaId ||
            !valid_prefixed_digest(summary.evaluation_identity,
                                   "phase6_evaluation.v1.") ||
            !valid_prefixed_digest(summary.evaluation_job_identity,
                                   "phase6_evaluation_job.v1.") ||
            !valid_replay_status(summary.replay_status) ||
            !valid_replay_status(summary.admission_status)) {
            fail("replay/admission summary identity or status is invalid");
        }
        validate_optional_public_ids(summary.trajectory_record_id,
                                     summary.public_gameplay_trajectory_id);
        validate_failure_fields(summary.failure_stage, summary.failure_code);
        if ((summary.replay_status == ReplayAdmissionStatus::Failed ||
             summary.admission_status == ReplayAdmissionStatus::Failed ||
             summary.admission_status == ReplayAdmissionStatus::Quarantined) &&
            (!summary.failure_stage.has_value() || !summary.failure_code.has_value())) {
            fail("replay/admission failure status lacks a typed failure reason");
        }
        if (summary.admission_status == ReplayAdmissionStatus::Passed &&
            summary.replay_status != ReplayAdmissionStatus::Passed) {
            fail("admission cannot pass when replay did not pass");
        }
        if (summary.replay_status == ReplayAdmissionStatus::NotRun &&
            summary.admission_status != ReplayAdmissionStatus::NotRun &&
            summary.admission_status != ReplayAdmissionStatus::Quarantined) {
            fail("admission status cannot run without replay");
        }
        if (summary.fallback_assisted &&
            summary.admission_status == ReplayAdmissionStatus::Passed) {
            fail("fallback-assisted job cannot pass admission");
        }
        return true;
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return false;
    } catch (...) {
        set_error(error, "replay/admission summary validation threw");
        return false;
    }
}

std::vector<std::uint8_t> canonical_gameplay_job_result_bytes(
    const GameplayJobResultV1& result) {
    if (!validate_gameplay_job_result(result, nullptr)) {
        fail("gameplay job result is invalid");
    }
    ByteWriter writer;
    writer.string(kGameplayJobResultIdentityDomain);
    writer.string(kGameplayJobResultIdentityDomain);
    writer.string(result.schema_id);
    writer.string(result.evaluation_identity);
    writer.string(result.evaluation_job_identity);
    writer.string(result.checkpoint_identity);
    writer.u8(static_cast<std::uint8_t>(result.status));
    writer.boolean(result.started);
    writer.boolean(result.terminal_observed);
    writer.boolean(result.fallback_assisted);
    writer.u8(result.terminal_outcome.has_value() ? 1 : 0);
    if (result.terminal_outcome.has_value()) write_terminal_outcome(writer, *result.terminal_outcome);
    writer.optional_string(result.trajectory_record_id);
    writer.optional_string(result.public_gameplay_trajectory_id);
    writer.string(result.replay_admission_summary_identity);
    writer.u8(result.failure_stage.has_value() ? 1 : 0);
    if (result.failure_stage.has_value()) writer.u8(static_cast<std::uint8_t>(*result.failure_stage));
    writer.optional_string(result.failure_code);
    return writer.take();
}

std::string gameplay_job_result_identity(const GameplayJobResultV1& result) {
    return digest_identity(kGameplayJobResultIdentityPrefix,
                           canonical_gameplay_job_result_bytes(result));
}

bool validate_gameplay_job_result(const GameplayJobResultV1& result,
                                  std::string* error) noexcept {
    try {
        if (result.schema_id != kGameplayJobResultSchemaId ||
            !valid_prefixed_digest(result.evaluation_identity, "phase6_evaluation.v1.") ||
            !valid_prefixed_digest(result.evaluation_job_identity,
                                   "phase6_evaluation_job.v1.") ||
            !valid_prefixed_digest(result.checkpoint_identity,
                                   "phase6_checkpoint.v1.") ||
            !valid_status(result.status) ||
            !valid_prefixed_digest(result.replay_admission_summary_identity,
                                   "phase6_replay_admission_summary.v1.")) {
            fail("gameplay job result identity or schema is invalid");
        }
        validate_optional_public_ids(result.trajectory_record_id,
                                     result.public_gameplay_trajectory_id);
        validate_failure_fields(result.failure_stage, result.failure_code);
        if ((result.status == GameplayJobStatus::Failed ||
             result.status == GameplayJobStatus::Quarantined) &&
            (!result.failure_stage.has_value() || !result.failure_code.has_value())) {
            fail("failed gameplay result lacks a typed failure reason");
        }
        if (result.terminal_outcome.has_value() && !result.terminal_outcome->terminal) {
            fail("gameplay result terminal payload is not terminal");
        }
        const bool trusted = result.status == GameplayJobStatus::TrustedWin ||
                             result.status == GameplayJobStatus::TrustedLoss ||
                             result.status == GameplayJobStatus::TrustedDraw;
        if (trusted && (!result.started || !result.terminal_observed ||
                        !result.terminal_outcome.has_value() ||
                        !result.trajectory_record_id.has_value() ||
                        !result.public_gameplay_trajectory_id.has_value() ||
                        result.fallback_assisted || result.failure_stage.has_value() ||
                        result.failure_code.has_value())) {
            fail("trusted gameplay result lacks a clean terminal/admission boundary");
        }
        if (result.status == GameplayJobStatus::Interrupted &&
            (result.terminal_observed || result.terminal_outcome.has_value())) {
            fail("interrupted gameplay result carries terminal outcome evidence");
        }
        if (result.fallback_assisted && trusted) {
            fail("fallback-assisted result cannot be trusted");
        }
        return true;
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return false;
    } catch (...) {
        set_error(error, "gameplay job result validation threw");
        return false;
    }
}

std::vector<std::uint8_t> canonical_gameplay_summary_bytes(
    const GameplaySummaryV1& summary) {
    if (!validate_gameplay_summary(summary, nullptr)) {
        fail("gameplay summary is invalid");
    }
    ByteWriter writer;
    writer.string(kGameplaySummaryIdentityDomain);
    writer.string(kGameplaySummaryIdentityDomain);
    writer.string(summary.schema_id);
    writer.string(summary.evaluation_identity);
    writer.string(summary.evaluation_corpus_identity);
    writer.string(summary.evaluation_job_manifest_identity);
    writer.string(summary.checkpoint_identity);
    writer.u32(static_cast<std::uint32_t>(summary.gameplay_job_result_identities.size()));
    for (const auto& id : summary.gameplay_job_result_identities) writer.string(id);
    writer.u32(summary.scheduled_job_count);
    writer.u32(summary.started_job_count);
    writer.u32(summary.completed_terminal_job_count);
    writer.u32(summary.trusted_win_count);
    writer.u32(summary.trusted_loss_count);
    writer.u32(summary.trusted_draw_count);
    writer.u32(summary.interrupted_job_count);
    writer.u32(summary.failed_job_count);
    writer.u32(summary.quarantined_job_count);
    writer.u32(summary.fallback_assisted_job_count);
    writer.u32(summary.replay_failure_count);
    writer.u32(summary.admission_failure_count);
    writer.u32(summary.inference_failure_count);
    writer.string(summary.wilson_metric_identity);
    writer.u32(summary.wilson_numerator);
    writer.u32(summary.wilson_denominator);
    writer.string(summary.wilson_interval_status);
    return writer.take();
}

std::string gameplay_summary_identity(const GameplaySummaryV1& summary) {
    return digest_identity(kGameplaySummaryIdentityPrefix,
                           canonical_gameplay_summary_bytes(summary));
}

bool validate_gameplay_summary(const GameplaySummaryV1& summary,
                               std::string* error) noexcept {
    try {
        if (summary.schema_id != kGameplaySummarySchemaId ||
            !valid_prefixed_digest(summary.evaluation_identity, "phase6_evaluation.v1.") ||
            !valid_prefixed_digest(summary.evaluation_corpus_identity,
                                   "phase6_evaluation_corpus.v1.") ||
            !valid_prefixed_digest(summary.evaluation_job_manifest_identity,
                                   "phase6_evaluation_job_manifest.v1.") ||
            !valid_prefixed_digest(summary.checkpoint_identity,
                                   "phase6_checkpoint.v1.") ||
            summary.wilson_metric_identity !=
                "ocgforge.phase6.gameplay_metrics.wilson_95.v1" ||
            (summary.wilson_interval_status != "AVAILABLE" &&
             summary.wilson_interval_status != "NOT_APPLICABLE")) {
            fail("gameplay summary fixed binding is invalid");
        }
        if (summary.gameplay_job_result_identities.size() !=
            summary.scheduled_job_count) {
            fail("gameplay summary result vector does not match schedule count");
        }
        std::set<std::string> ids;
        for (const auto& id : summary.gameplay_job_result_identities) {
            if (!valid_prefixed_digest(id, "phase6_gameplay_job_result.v1.") ||
                !ids.insert(id).second) {
                fail("gameplay summary result vector is not unique and ordered");
            }
        }
        const auto classified = static_cast<std::uint64_t>(summary.trusted_win_count) +
                                summary.trusted_loss_count + summary.trusted_draw_count +
                                summary.interrupted_job_count + summary.failed_job_count +
                                summary.quarantined_job_count;
        const auto completed_terminal =
            static_cast<std::uint64_t>(summary.trusted_win_count) +
            summary.trusted_loss_count + summary.trusted_draw_count;
        const auto wilson_denominator =
            static_cast<std::uint64_t>(summary.trusted_win_count) +
            summary.trusted_loss_count;
        if (classified != summary.scheduled_job_count ||
            summary.started_job_count > summary.scheduled_job_count ||
            summary.completed_terminal_job_count != completed_terminal ||
            summary.wilson_numerator != summary.trusted_win_count ||
            summary.wilson_denominator != wilson_denominator ||
            (summary.wilson_denominator == 0 &&
             summary.wilson_interval_status != "NOT_APPLICABLE") ||
            (summary.wilson_denominator != 0 &&
             summary.wilson_interval_status != "AVAILABLE")) {
            fail("gameplay summary counts do not conserve the scheduled population");
        }
        if (summary.fallback_assisted_job_count > summary.scheduled_job_count ||
            summary.replay_failure_count > summary.scheduled_job_count ||
            summary.admission_failure_count > summary.scheduled_job_count ||
            summary.inference_failure_count > summary.scheduled_job_count) {
            fail("gameplay summary failure count exceeds its population");
        }
        return true;
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return false;
    } catch (...) {
        set_error(error, "gameplay summary validation threw");
        return false;
    }
}

namespace detail {

bool counts_as_inference_failure(const GameplayJobResultV1& result) noexcept {
    return result.failure_stage.has_value() &&
           *result.failure_stage == GameplayFailureStage::Inference;
}

}  // namespace detail

namespace {

struct JsonValue final {
    enum class Kind : std::uint8_t { Null, Boolean, Number, String, Array, Object };

    Kind kind = Kind::Null;
    bool boolean = false;
    std::uint64_t number = 0;
    std::string string;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue> object;

    static JsonValue null() { return {}; }
    static JsonValue boolean_value(const bool value) {
        JsonValue result;
        result.kind = Kind::Boolean;
        result.boolean = value;
        return result;
    }
    static JsonValue number_value(const std::uint64_t value) {
        JsonValue result;
        result.kind = Kind::Number;
        result.number = value;
        return result;
    }
    static JsonValue string_value(std::string value) {
        JsonValue result;
        result.kind = Kind::String;
        result.string = std::move(value);
        return result;
    }
    static JsonValue array_value(std::vector<JsonValue> value) {
        JsonValue result;
        result.kind = Kind::Array;
        result.array = std::move(value);
        return result;
    }
    static JsonValue object_value(std::map<std::string, JsonValue> value) {
        JsonValue result;
        result.kind = Kind::Object;
        result.object = std::move(value);
        return result;
    }
};

void append_utf8(std::string& output, const std::uint32_t code_point) {
    if (code_point <= 0x7f) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (code_point >> 6)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    } else if (code_point <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (code_point >> 12)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    } else if (code_point <= 0x10ffff) {
        output.push_back(static_cast<char>(0xf0 | (code_point >> 18)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    } else {
        fail("JSON string contains an invalid Unicode scalar");
    }
}

std::uint32_t hex_quad(const std::string_view input, const std::size_t offset) {
    if (offset + 4 > input.size()) fail("JSON Unicode escape is truncated");
    std::uint32_t value = 0;
    for (std::size_t index = offset; index < offset + 4; ++index) {
        const char character = input[index];
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f') ||
              (character >= 'A' && character <= 'F'))) {
            fail("JSON Unicode escape is not hexadecimal");
        }
        value <<= 4;
        value |= static_cast<std::uint32_t>(character <= '9'
                                                ? character - '0'
                                                : character >= 'a'
                                                      ? character - 'a' + 10
                                                      : character - 'A' + 10);
    }
    return value;
}

class JsonParser final {
public:
    explicit JsonParser(const std::string_view input) : input_(input) {}

    JsonValue parse_document() {
        if (input_.empty()) fail("JSON document is empty");
        const auto value = parse_value();
        skip_whitespace();
        if (position_ != input_.size()) fail("JSON document has trailing data");
        return value;
    }

private:
    void skip_whitespace() {
        while (position_ < input_.size()) {
            const char character = input_[position_];
            if (character != ' ' && character != '\t' && character != '\n' &&
                character != '\r') {
                return;
            }
            ++position_;
        }
    }

    char take() {
        if (position_ >= input_.size()) fail("JSON document ended unexpectedly");
        return input_[position_++];
    }

    void expect(const char expected) {
        if (take() != expected) fail("JSON punctuation is invalid");
    }

    JsonValue parse_value() {
        skip_whitespace();
        if (position_ >= input_.size()) fail("JSON value is missing");
        switch (input_[position_]) {
        case 'n':
            expect_literal("null");
            return JsonValue::null();
        case 't':
            expect_literal("true");
            return JsonValue::boolean_value(true);
        case 'f':
            expect_literal("false");
            return JsonValue::boolean_value(false);
        case '"':
            return JsonValue::string_value(parse_string());
        case '[':
            return parse_array();
        case '{':
            return parse_object();
        default:
            if (input_[position_] >= '0' && input_[position_] <= '9') {
                return JsonValue::number_value(parse_number());
            }
            fail("JSON value has an unsupported token");
        }
    }

    void expect_literal(const std::string_view literal) {
        if (input_.substr(position_, literal.size()) != literal) {
            fail("JSON literal is invalid");
        }
        position_ += literal.size();
    }

    std::uint64_t parse_number() {
        const std::size_t start = position_;
        if (input_[position_] == '0') {
            ++position_;
            if (position_ < input_.size() && input_[position_] >= '0' &&
                input_[position_] <= '9') {
                fail("JSON number has a leading zero");
            }
        } else {
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') {
                ++position_;
            }
        }
        if (position_ < input_.size() &&
            (input_[position_] == '.' || input_[position_] == 'e' ||
             input_[position_] == 'E' || input_[position_] == '-')) {
            fail("JSON number is not an unsigned integer");
        }
        std::uint64_t value = 0;
        for (std::size_t index = start; index < position_; ++index) {
            const auto digit = static_cast<std::uint64_t>(input_[index] - '0');
            if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
                fail("JSON number overflows u64");
            }
            value = value * 10 + digit;
        }
        return value;
    }

    std::string parse_string() {
        expect('"');
        std::string result;
        while (position_ < input_.size()) {
            const unsigned char byte = static_cast<unsigned char>(input_[position_++]);
            if (byte == '"') return result;
            if (byte < 0x20) fail("JSON string contains a control character");
            if (byte != '\\') {
                result.push_back(static_cast<char>(byte));
                continue;
            }
            const char escaped = take();
            switch (escaped) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': {
                const auto first = hex_quad(input_, position_);
                position_ += 4;
                if (first >= 0xd800 && first <= 0xdbff) {
                    if (input_.substr(position_, 2) != "\\u") {
                        fail("JSON high surrogate lacks a low surrogate");
                    }
                    position_ += 2;
                    const auto second = hex_quad(input_, position_);
                    position_ += 4;
                    if (second < 0xdc00 || second > 0xdfff) {
                        fail("JSON surrogate pair is invalid");
                    }
                    append_utf8(result, 0x10000 + ((first - 0xd800) << 10) +
                                           (second - 0xdc00));
                } else if (first >= 0xdc00 && first <= 0xdfff) {
                    fail("JSON string contains an unpaired low surrogate");
                } else {
                    append_utf8(result, first);
                }
                break;
            }
            default: fail("JSON escape is invalid");
            }
        }
        fail("JSON string is unterminated");
    }

    JsonValue parse_array() {
        expect('[');
        std::vector<JsonValue> result;
        skip_whitespace();
        if (position_ < input_.size() && input_[position_] == ']') {
            ++position_;
            return JsonValue::array_value(std::move(result));
        }
        for (;;) {
            result.push_back(parse_value());
            skip_whitespace();
            const char separator = take();
            if (separator == ']') break;
            if (separator != ',') fail("JSON array separator is invalid");
        }
        return JsonValue::array_value(std::move(result));
    }

    JsonValue parse_object() {
        expect('{');
        std::map<std::string, JsonValue> result;
        skip_whitespace();
        if (position_ < input_.size() && input_[position_] == '}') {
            ++position_;
            return JsonValue::object_value(std::move(result));
        }
        for (;;) {
            skip_whitespace();
            if (position_ >= input_.size() || input_[position_] != '"') {
                fail("JSON object key is missing");
            }
            const auto key = parse_string();
            skip_whitespace();
            expect(':');
            auto inserted = result.emplace(key, parse_value());
            if (!inserted.second) fail("JSON object contains duplicate keys");
            skip_whitespace();
            const char separator = take();
            if (separator == '}') break;
            if (separator != ',') fail("JSON object separator is invalid");
        }
        return JsonValue::object_value(std::move(result));
    }

    std::string_view input_;
    std::size_t position_ = 0;
};

std::string json_escape(const std::string_view value) {
    if (!valid_utf8(value)) fail("JSON string is not strict UTF-8");
    std::ostringstream output;
    output << '"';
    for (std::size_t index = 0; index < value.size();) {
        const unsigned char byte = static_cast<unsigned char>(value[index]);
        if (byte < 0x80) {
            switch (byte) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (byte < 0x20) {
                    output << "\\u" << std::hex << std::nouppercase << std::setfill('0')
                           << std::setw(4) << static_cast<unsigned int>(byte) << std::dec;
                } else {
                    output << static_cast<char>(byte);
                }
            }
            ++index;
            continue;
        }
        std::size_t count = byte >= 0xf0 ? 4 : byte >= 0xe0 ? 3 : 2;
        std::uint32_t code_point = byte >= 0xf0 ? byte & 0x07
                                      : byte >= 0xe0 ? byte & 0x0f
                                                     : byte & 0x1f;
        for (std::size_t offset = 1; offset < count; ++offset) {
            code_point = (code_point << 6) |
                         (static_cast<unsigned char>(value[index + offset]) & 0x3f);
        }
        if (code_point <= 0xffff) {
            output << "\\u" << std::hex << std::nouppercase << std::setfill('0')
                   << std::setw(4) << code_point << std::dec;
        } else {
            const auto adjusted = code_point - 0x10000;
            const auto high = 0xd800 + (adjusted >> 10);
            const auto low = 0xdc00 + (adjusted & 0x3ff);
            output << "\\u" << std::hex << std::nouppercase << std::setfill('0')
                   << std::setw(4) << high << "\\u" << std::setw(4) << low << std::dec;
        }
        index += count;
    }
    output << '"';
    return output.str();
}

std::string canonical_json(const JsonValue& value) {
    switch (value.kind) {
    case JsonValue::Kind::Null: return "null";
    case JsonValue::Kind::Boolean: return value.boolean ? "true" : "false";
    case JsonValue::Kind::Number: return std::to_string(value.number);
    case JsonValue::Kind::String: return json_escape(value.string);
    case JsonValue::Kind::Array: {
        std::string output = "[";
        for (std::size_t index = 0; index < value.array.size(); ++index) {
            if (index != 0) output += ',';
            output += canonical_json(value.array[index]);
        }
        output += ']';
        return output;
    }
    case JsonValue::Kind::Object: {
        std::string output = "{";
        std::size_t index = 0;
        for (const auto& [key, child] : value.object) {
            if (index++ != 0) output += ',';
            output += json_escape(key);
            output += ':';
            output += canonical_json(child);
        }
        output += '}';
        return output;
    }
    }
    fail("JSON value kind is unknown");
}

JsonValue parse_canonical_json(const std::string_view data) {
    if (data.size() < 2 || data.front() == '\xef' || data[data.size() - 1] != '\n' ||
        data[data.size() - 2] == '\n' || data[data.size() - 2] == '\r') {
        fail("JSON must be UTF-8 without BOM with exactly one final LF");
    }
    const auto body = data.substr(0, data.size() - 1);
    const auto value = JsonParser(body).parse_document();
    if (canonical_json(value) + "\n" != data) {
        fail("JSON is not canonical");
    }
    return value;
}

const std::map<std::string, JsonValue>& object_of(const JsonValue& value,
                                                  const std::string_view label) {
    if (value.kind != JsonValue::Kind::Object) fail(std::string(label) + " is not an object");
    return value.object;
}

void require_fields(const std::map<std::string, JsonValue>& object,
                    const std::initializer_list<std::string_view> fields,
                    const std::string_view label) {
    std::set<std::string> expected;
    for (const auto field : fields) expected.emplace(field);
    if (object.size() != expected.size()) fail(std::string(label) + " has unexpected fields");
    for (const auto& field : object) {
        if (!expected.count(field.first)) fail(std::string(label) + " has an unknown field");
    }
}

const JsonValue& field_of(const std::map<std::string, JsonValue>& object,
                          const std::string_view field) {
    const auto found = object.find(std::string(field));
    if (found == object.end()) fail("JSON object is missing a required field");
    return found->second;
}

std::string json_string_field(const std::map<std::string, JsonValue>& object,
                              const std::string_view field) {
    const auto& value = field_of(object, field);
    if (value.kind != JsonValue::Kind::String) fail("JSON field is not a string");
    return value.string;
}

std::optional<std::string> json_optional_string_field(
    const std::map<std::string, JsonValue>& object, const std::string_view field) {
    const auto& value = field_of(object, field);
    if (value.kind == JsonValue::Kind::Null) return std::nullopt;
    if (value.kind != JsonValue::Kind::String) fail("JSON optional string is not string/null");
    return value.string;
}

std::uint64_t json_number_field(const std::map<std::string, JsonValue>& object,
                               const std::string_view field) {
    const auto& value = field_of(object, field);
    if (value.kind != JsonValue::Kind::Number) fail("JSON field is not an unsigned integer");
    return value.number;
}

bool json_bool_field(const std::map<std::string, JsonValue>& object,
                    const std::string_view field) {
    const auto& value = field_of(object, field);
    if (value.kind != JsonValue::Kind::Boolean) fail("JSON field is not a boolean");
    return value.boolean;
}

const std::vector<JsonValue>& json_array_field(
    const std::map<std::string, JsonValue>& object, const std::string_view field) {
    const auto& value = field_of(object, field);
    if (value.kind != JsonValue::Kind::Array) fail("JSON field is not an array");
    return value.array;
}

JsonValue optional_string_json(const std::optional<std::string>& value) {
    return value.has_value() ? JsonValue::string_value(*value) : JsonValue::null();
}

JsonValue optional_stage_json(const std::optional<GameplayFailureStage>& value) {
    return value.has_value()
               ? JsonValue::string_value(std::string(gameplay_failure_stage_name(*value)))
               : JsonValue::null();
}

std::optional<GameplayFailureStage> parse_optional_stage(
    const std::map<std::string, JsonValue>& object, const std::string_view field) {
    const auto& value = field_of(object, field);
    if (value.kind == JsonValue::Kind::Null) return std::nullopt;
    if (value.kind != JsonValue::Kind::String) fail("failure stage is not string/null");
    for (std::uint8_t raw = 0; raw <= static_cast<std::uint8_t>(GameplayFailureStage::Admission);
         ++raw) {
        const auto stage = static_cast<GameplayFailureStage>(raw);
        if (value.string == gameplay_failure_stage_name(stage)) return stage;
    }
    fail("failure stage is unknown");
}

std::optional<std::uint8_t> json_optional_u8_field(
    const std::map<std::string, JsonValue>& object, const std::string_view field) {
    const auto& value = field_of(object, field);
    if (value.kind == JsonValue::Kind::Null) return std::nullopt;
    if (value.kind != JsonValue::Kind::Number || value.number > 0xff) {
        fail("JSON optional u8 is invalid");
    }
    return static_cast<std::uint8_t>(value.number);
}

std::optional<TerminalOutcomeV1> parse_optional_terminal_strict(
    const std::map<std::string, JsonValue>& object, const std::string_view field) {
    const auto& value = field_of(object, field);
    if (value.kind == JsonValue::Kind::Null) return std::nullopt;
    const auto& terminal = object_of(value, "terminal_outcome");
    require_fields(terminal, {"terminal", "winner", "win_reason"}, "terminal_outcome");
    TerminalOutcomeV1 result;
    result.terminal = json_bool_field(terminal, "terminal");
    result.winner = json_optional_u8_field(terminal, "winner");
    result.win_reason = json_optional_u8_field(terminal, "win_reason");
    return result;
}

GameplayJobStatus parse_job_status(const std::string& value) {
    for (std::uint8_t raw = 0; raw <= static_cast<std::uint8_t>(GameplayJobStatus::Quarantined);
         ++raw) {
        const auto status = static_cast<GameplayJobStatus>(raw);
        if (value == gameplay_job_status_name(status)) return status;
    }
    fail("gameplay job status is unknown");
}

ReplayAdmissionStatus parse_replay_status(const std::string& value) {
    for (std::uint8_t raw = 0;
         raw <= static_cast<std::uint8_t>(ReplayAdmissionStatus::Quarantined); ++raw) {
        const auto status = static_cast<ReplayAdmissionStatus>(raw);
        if (value == replay_admission_status_name(status)) return status;
    }
    fail("replay/admission status is unknown");
}

JsonValue terminal_json(const TerminalOutcomeV1& value) {
    std::map<std::string, JsonValue> terminal;
    terminal.emplace("terminal", JsonValue::boolean_value(value.terminal));
    terminal.emplace("winner",
                     value.winner.has_value() ? JsonValue::number_value(*value.winner)
                                               : JsonValue::null());
    terminal.emplace("win_reason", value.win_reason.has_value()
                                      ? JsonValue::number_value(*value.win_reason)
                                      : JsonValue::null());
    return JsonValue::object_value(std::move(terminal));
}

JsonValue replay_summary_json_value(const ReplayAdmissionSummaryV1& summary) {
    std::map<std::string, JsonValue> object;
    object.emplace("admission_status",
                   JsonValue::string_value(std::string(replay_admission_status_name(
                       summary.admission_status))));
    object.emplace("evaluation_identity", JsonValue::string_value(summary.evaluation_identity));
    object.emplace("evaluation_job_identity",
                   JsonValue::string_value(summary.evaluation_job_identity));
    object.emplace("failure_code", optional_string_json(summary.failure_code));
    object.emplace("failure_stage", optional_stage_json(summary.failure_stage));
    object.emplace("fallback_assisted", JsonValue::boolean_value(summary.fallback_assisted));
    object.emplace("public_gameplay_trajectory_id",
                   optional_string_json(summary.public_gameplay_trajectory_id));
    object.emplace("replay_admission_summary_identity",
                   JsonValue::string_value(replay_admission_summary_identity(summary)));
    object.emplace("replay_status",
                   JsonValue::string_value(std::string(replay_admission_status_name(
                       summary.replay_status))));
    object.emplace("schema_id", JsonValue::string_value(summary.schema_id));
    object.emplace("trajectory_record_id", optional_string_json(summary.trajectory_record_id));
    return JsonValue::object_value(std::move(object));
}

JsonValue job_result_json_value(const GameplayJobResultV1& result) {
    std::map<std::string, JsonValue> object;
    object.emplace("checkpoint_identity", JsonValue::string_value(result.checkpoint_identity));
    object.emplace("evaluation_identity", JsonValue::string_value(result.evaluation_identity));
    object.emplace("evaluation_job_identity",
                   JsonValue::string_value(result.evaluation_job_identity));
    object.emplace("failure_code", optional_string_json(result.failure_code));
    object.emplace("failure_stage", optional_stage_json(result.failure_stage));
    object.emplace("fallback_assisted", JsonValue::boolean_value(result.fallback_assisted));
    object.emplace("gameplay_job_result_identity",
                   JsonValue::string_value(gameplay_job_result_identity(result)));
    object.emplace("public_gameplay_trajectory_id",
                   optional_string_json(result.public_gameplay_trajectory_id));
    object.emplace("replay_admission_summary_identity",
                   JsonValue::string_value(result.replay_admission_summary_identity));
    object.emplace("schema_id", JsonValue::string_value(result.schema_id));
    object.emplace("started", JsonValue::boolean_value(result.started));
    object.emplace("status", JsonValue::string_value(std::string(gameplay_job_status_name(
                       result.status))));
    object.emplace("terminal_observed", JsonValue::boolean_value(result.terminal_observed));
    object.emplace("terminal_outcome", result.terminal_outcome.has_value()
                                           ? terminal_json(*result.terminal_outcome)
                                           : JsonValue::null());
    object.emplace("trajectory_record_id", optional_string_json(result.trajectory_record_id));
    return JsonValue::object_value(std::move(object));
}

JsonValue summary_json_value(const GameplaySummaryV1& summary) {
    std::vector<JsonValue> ids;
    ids.reserve(summary.gameplay_job_result_identities.size());
    for (const auto& id : summary.gameplay_job_result_identities) {
        ids.push_back(JsonValue::string_value(id));
    }
    std::map<std::string, JsonValue> object;
    object.emplace("admission_failure_count", JsonValue::number_value(summary.admission_failure_count));
    object.emplace("checkpoint_identity", JsonValue::string_value(summary.checkpoint_identity));
    object.emplace("completed_terminal_job_count",
                   JsonValue::number_value(summary.completed_terminal_job_count));
    object.emplace("evaluation_corpus_identity",
                   JsonValue::string_value(summary.evaluation_corpus_identity));
    object.emplace("evaluation_identity", JsonValue::string_value(summary.evaluation_identity));
    object.emplace("evaluation_job_manifest_identity",
                   JsonValue::string_value(summary.evaluation_job_manifest_identity));
    object.emplace("failed_job_count", JsonValue::number_value(summary.failed_job_count));
    object.emplace("fallback_assisted_job_count",
                   JsonValue::number_value(summary.fallback_assisted_job_count));
    object.emplace("gameplay_job_result_identities", JsonValue::array_value(std::move(ids)));
    object.emplace("gameplay_summary_identity",
                   JsonValue::string_value(gameplay_summary_identity(summary)));
    object.emplace("inference_failure_count",
                   JsonValue::number_value(summary.inference_failure_count));
    object.emplace("interrupted_job_count", JsonValue::number_value(summary.interrupted_job_count));
    object.emplace("quarantined_job_count", JsonValue::number_value(summary.quarantined_job_count));
    object.emplace("replay_failure_count", JsonValue::number_value(summary.replay_failure_count));
    object.emplace("scheduled_job_count", JsonValue::number_value(summary.scheduled_job_count));
    object.emplace("started_job_count", JsonValue::number_value(summary.started_job_count));
    object.emplace("schema_id", JsonValue::string_value(summary.schema_id));
    object.emplace("trusted_draw_count", JsonValue::number_value(summary.trusted_draw_count));
    object.emplace("trusted_loss_count", JsonValue::number_value(summary.trusted_loss_count));
    object.emplace("trusted_win_count", JsonValue::number_value(summary.trusted_win_count));
    object.emplace("wilson_denominator", JsonValue::number_value(summary.wilson_denominator));
    object.emplace("wilson_interval_status",
                   JsonValue::string_value(summary.wilson_interval_status));
    object.emplace("wilson_metric_identity",
                   JsonValue::string_value(summary.wilson_metric_identity));
    object.emplace("wilson_numerator", JsonValue::number_value(summary.wilson_numerator));
    return JsonValue::object_value(std::move(object));
}

std::vector<std::string> json_string_array(const std::vector<JsonValue>& values) {
    std::vector<std::string> result;
    result.reserve(values.size());
    for (const auto& value : values) {
        if (value.kind != JsonValue::Kind::String) fail("JSON string array contains a non-string");
        result.push_back(value.string);
    }
    return result;
}

}  // namespace

std::string encode_replay_admission_summary_json(
    const ReplayAdmissionSummaryV1& summary) {
    return canonical_json(replay_summary_json_value(summary)) + "\n";
}

ReplayAdmissionSummaryV1 decode_replay_admission_summary_json(
    const std::string_view data) {
    const auto object = object_of(parse_canonical_json(data), "replay/admission summary");
    require_fields(object,
                   {"admission_status", "evaluation_identity", "evaluation_job_identity",
                    "failure_code", "failure_stage", "fallback_assisted",
                    "public_gameplay_trajectory_id", "replay_admission_summary_identity",
                    "replay_status", "schema_id", "trajectory_record_id"},
                   "replay/admission summary");
    ReplayAdmissionSummaryV1 result;
    result.schema_id = json_string_field(object, "schema_id");
    result.evaluation_identity = json_string_field(object, "evaluation_identity");
    result.evaluation_job_identity = json_string_field(object, "evaluation_job_identity");
    result.trajectory_record_id = json_optional_string_field(object, "trajectory_record_id");
    result.public_gameplay_trajectory_id =
        json_optional_string_field(object, "public_gameplay_trajectory_id");
    result.replay_status = parse_replay_status(json_string_field(object, "replay_status"));
    result.admission_status = parse_replay_status(json_string_field(object, "admission_status"));
    result.failure_stage = parse_optional_stage(object, "failure_stage");
    result.failure_code = json_optional_string_field(object, "failure_code");
    result.fallback_assisted = json_bool_field(object, "fallback_assisted");
    const auto declared = json_string_field(object, "replay_admission_summary_identity");
    if (!validate_replay_admission_summary(result, nullptr) ||
        declared != replay_admission_summary_identity(result)) {
        fail("replay/admission summary identity does not match its payload");
    }
    return result;
}

std::string encode_gameplay_job_result_json(const GameplayJobResultV1& result) {
    return canonical_json(job_result_json_value(result)) + "\n";
}

GameplayJobResultV1 decode_gameplay_job_result_json(const std::string_view data) {
    const auto object = object_of(parse_canonical_json(data), "gameplay job result");
    require_fields(object,
                   {"checkpoint_identity", "evaluation_identity", "evaluation_job_identity",
                    "failure_code", "failure_stage", "fallback_assisted",
                    "gameplay_job_result_identity", "public_gameplay_trajectory_id",
                    "replay_admission_summary_identity", "schema_id", "started", "status",
                    "terminal_observed", "terminal_outcome", "trajectory_record_id"},
                   "gameplay job result");
    GameplayJobResultV1 result;
    result.schema_id = json_string_field(object, "schema_id");
    result.evaluation_identity = json_string_field(object, "evaluation_identity");
    result.evaluation_job_identity = json_string_field(object, "evaluation_job_identity");
    result.checkpoint_identity = json_string_field(object, "checkpoint_identity");
    result.status = parse_job_status(json_string_field(object, "status"));
    result.started = json_bool_field(object, "started");
    result.terminal_observed = json_bool_field(object, "terminal_observed");
    result.fallback_assisted = json_bool_field(object, "fallback_assisted");
    result.terminal_outcome = parse_optional_terminal_strict(object, "terminal_outcome");
    result.trajectory_record_id = json_optional_string_field(object, "trajectory_record_id");
    result.public_gameplay_trajectory_id =
        json_optional_string_field(object, "public_gameplay_trajectory_id");
    result.replay_admission_summary_identity =
        json_string_field(object, "replay_admission_summary_identity");
    result.failure_stage = parse_optional_stage(object, "failure_stage");
    result.failure_code = json_optional_string_field(object, "failure_code");
    const auto declared = json_string_field(object, "gameplay_job_result_identity");
    if (!validate_gameplay_job_result(result, nullptr) ||
        declared != gameplay_job_result_identity(result)) {
        fail("gameplay job result identity does not match its payload");
    }
    return result;
}

std::string encode_gameplay_summary_json(const GameplaySummaryV1& summary) {
    return canonical_json(summary_json_value(summary)) + "\n";
}

GameplaySummaryV1 decode_gameplay_summary_json(const std::string_view data) {
    const auto object = object_of(parse_canonical_json(data), "gameplay summary");
    require_fields(object,
                   {"admission_failure_count", "checkpoint_identity",
                    "completed_terminal_job_count", "evaluation_corpus_identity",
                    "evaluation_identity", "evaluation_job_manifest_identity",
                    "failed_job_count", "fallback_assisted_job_count",
                    "gameplay_job_result_identities", "gameplay_summary_identity",
                    "inference_failure_count", "interrupted_job_count",
                    "quarantined_job_count", "replay_failure_count", "scheduled_job_count",
                    "started_job_count", "schema_id", "trusted_draw_count",
                    "trusted_loss_count", "trusted_win_count", "wilson_denominator",
                    "wilson_interval_status", "wilson_metric_identity", "wilson_numerator"},
                   "gameplay summary");
    GameplaySummaryV1 result;
    result.schema_id = json_string_field(object, "schema_id");
    result.evaluation_identity = json_string_field(object, "evaluation_identity");
    result.evaluation_corpus_identity = json_string_field(object, "evaluation_corpus_identity");
    result.evaluation_job_manifest_identity =
        json_string_field(object, "evaluation_job_manifest_identity");
    result.checkpoint_identity = json_string_field(object, "checkpoint_identity");
    result.gameplay_job_result_identities =
        json_string_array(json_array_field(object, "gameplay_job_result_identities"));
    const auto u32 = [&object](const std::string_view field) {
        const auto value = json_number_field(object, field);
        if (value > std::numeric_limits<std::uint32_t>::max()) fail("JSON u32 overflows");
        return static_cast<std::uint32_t>(value);
    };
    result.scheduled_job_count = u32("scheduled_job_count");
    result.started_job_count = u32("started_job_count");
    result.completed_terminal_job_count = u32("completed_terminal_job_count");
    result.trusted_win_count = u32("trusted_win_count");
    result.trusted_loss_count = u32("trusted_loss_count");
    result.trusted_draw_count = u32("trusted_draw_count");
    result.interrupted_job_count = u32("interrupted_job_count");
    result.failed_job_count = u32("failed_job_count");
    result.quarantined_job_count = u32("quarantined_job_count");
    result.fallback_assisted_job_count = u32("fallback_assisted_job_count");
    result.replay_failure_count = u32("replay_failure_count");
    result.admission_failure_count = u32("admission_failure_count");
    result.inference_failure_count = u32("inference_failure_count");
    result.wilson_metric_identity = json_string_field(object, "wilson_metric_identity");
    result.wilson_numerator = u32("wilson_numerator");
    result.wilson_denominator = u32("wilson_denominator");
    result.wilson_interval_status = json_string_field(object, "wilson_interval_status");
    const auto declared = json_string_field(object, "gameplay_summary_identity");
    if (!validate_gameplay_summary(result, nullptr) ||
        declared != gameplay_summary_identity(result)) {
        fail("gameplay summary identity does not match its payload");
    }
    return result;
}

std::string encode_gameplay_job_results_jsonl(
    const std::vector<GameplayJobResultV1>& results,
    const std::vector<std::string>& ordered_job_identities) {
    if (results.size() != ordered_job_identities.size()) {
        fail("gameplay result stream does not cover the frozen job schedule");
    }
    std::string output;
    for (std::size_t index = 0; index < results.size(); ++index) {
        if (results[index].evaluation_job_identity != ordered_job_identities[index]) {
            fail("gameplay result stream is not in evaluation-job-manifest order");
        }
        output += encode_gameplay_job_result_json(results[index]);
    }
    return output;
}

std::vector<GameplayJobResultV1> decode_gameplay_job_results_jsonl(
    const std::string_view data,
    const std::vector<std::string>& ordered_job_identities) {
    if (data.empty() || data.back() != '\n' ||
        (data.size() >= 2 && data[data.size() - 2] == '\n')) {
        fail("gameplay JSONL must contain one final LF and no blank lines");
    }
    std::vector<GameplayJobResultV1> results;
    std::size_t line_start = 0;
    while (line_start < data.size()) {
        const auto line_end = data.find('\n', line_start);
        if (line_end == std::string_view::npos || line_end == line_start) {
            fail("gameplay JSONL contains a blank or unterminated line");
        }
        const auto line = data.substr(line_start, line_end - line_start);
        if (line.find('\r') != std::string_view::npos) fail("gameplay JSONL contains CRLF");
        results.push_back(decode_gameplay_job_result_json(std::string(line) + "\n"));
        line_start = line_end + 1;
    }
    if (results.size() != ordered_job_identities.size()) {
        fail("gameplay JSONL does not cover the frozen schedule exactly");
    }
    for (std::size_t index = 0; index < results.size(); ++index) {
        if (results[index].evaluation_job_identity != ordered_job_identities[index]) {
            fail("gameplay JSONL records are not in frozen job order");
        }
    }
    return results;
}

namespace {

trajectory::PolicyRngDecisionProvenance no_rng_attribution(
    const environment::DecisionFrame& frame,
    const policy::PolicyExecutionBinding& binding) {
    trajectory::PolicyRngDecisionProvenance result;
    result.decision_index = frame.decision_index;
    result.acting_policy_assignment_id = binding.participant_policy_assignment_id;
    result.policy_rng_identity = trajectory::kNoPolicyRngContractId;
    result.policy_rng_contract_identity = trajectory::kNoPolicyRngContractId;
    result.policy_rng_stream_id = trajectory::kNoPolicyRngContractId;
    result.policy_rng_initialization_identity = trajectory::kNoPolicyRngContractId;
    result.mode = trajectory::PolicyRngMode::None;
    return result;
}

trajectory::ParticipantPolicyAssignment assignment_for(
    const EvaluationJobV1& job,
    const environment::CertifiedEnvironmentConfig& environment_config,
    const environment::EpisodeSpec& spec,
    const std::uint8_t player,
    const std::string& artifact_id,
    const trajectory::PolicyRole role) {
    if (player > 1) fail("policy assignment player is invalid");
    const auto deck_index = spec.seat_assignment == environment::SeatAssignment::Mirror
                                ? 1u - static_cast<unsigned int>(player)
                                : static_cast<unsigned int>(player);
    if (deck_index >= environment_config.locked_decks.size()) {
        fail("policy assignment deck index is invalid");
    }
    trajectory::ParticipantPolicyAssignment assignment;
    assignment.player = player;
    assignment.seat_role = player == spec.starting_player
                               ? trajectory::SeatRole::StartingPlayer
                               : trajectory::SeatRole::NonStartingPlayer;
    assignment.deck_role = deck_index == 0 ? trajectory::DeckRole::FirstLockedDeck
                                           : trajectory::DeckRole::SecondLockedDeck;
    assignment.resolved_locked_deck_id = environment_config.locked_decks[deck_index].id;
    assignment.resolved_locked_deck_sha256 = environment_config.locked_decks[deck_index].sha256;
    assignment.policy_role = role;
    assignment.policy_artifact_id = artifact_id;
    assignment.assignment_epoch = 0;
    assignment.effective_from_decision_index = 0;
    assignment.league_context.reset();
    assignment.participant_policy_assignment_id =
        trajectory::compute_participant_policy_assignment_id(assignment);
    (void)job;
    return assignment;
}

struct RuntimePolicies final {
    CheckpointBoundPolicyV1 evaluated;
    policy::TeacherPolicySession opponent;
    trajectory::PolicyProvenanceEnvelope provenance;
};

RuntimePolicies make_runtime_policies(
    const FrozenGameplayEvaluatorConfigV1& config,
    const EvaluationJobV1& job,
    const environment::EpisodeSpec& spec) {
    const auto opponent_profile = job.opponent_policy_deck_role_id == kSwordsoulDeckIdentity
                                      ? teacher::make_swordsoul_tenyi_profile()
                                      : teacher::make_salamangreat_profile();
    const auto opponent_artifact = policy::make_teacher_policy_artifact(opponent_profile);
    if (opponent_artifact.policy_artifact_id != job.opponent_policy_artifact_id ||
        opponent_artifact.artifact_metadata_identity !=
            std::optional<std::string>{job.opponent_policy_binding_id}) {
        fail("opponent Teacher artifact does not match the frozen job");
    }

    const auto evaluated_assignment = assignment_for(
        job, config.environment_config, spec, job.evaluated_policy_seat,
        config.evaluated_policy_artifact.policy_artifact_id, trajectory::PolicyRole::Evaluation);
    const auto opponent_assignment = assignment_for(
        job, config.environment_config, spec, job.opponent_policy_seat,
        opponent_artifact.policy_artifact_id, trajectory::PolicyRole::Opponent);

    auto evaluated_policy = create_checkpoint_bound_policy(
        job.evaluated_policy_checkpoint_identity, job.evaluated_policy_seat,
        evaluated_assignment.participant_policy_assignment_id,
        config.evaluated_policy_artifact.policy_artifact_id,
        config.card_vocabulary, config.inference_provider);
    if (!evaluated_policy || !evaluated_policy.value.has_value()) {
        fail(evaluated_policy.error.has_value()
                 ? evaluated_policy.error->message
                 : "checkpoint policy construction failed");
    }
    auto opponent_session = policy::create_teacher_policy_session(
        opponent_profile,
        policy::make_teacher_policy_binding(opponent_profile),
        opponent_artifact, opponent_assignment);
    if (!opponent_session || !opponent_session.value.has_value()) {
        fail(opponent_session.error.has_value()
                 ? opponent_session.error->message
                 : "opponent Teacher session construction failed");
    }
    RuntimePolicies result{std::move(*evaluated_policy.value),
                           std::move(*opponent_session.value), {}};
    result.provenance.policy_artifacts = {config.evaluated_policy_artifact, opponent_artifact};
    std::sort(result.provenance.policy_artifacts.begin(), result.provenance.policy_artifacts.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_artifact_id < right.policy_artifact_id;
              });
    result.provenance.participant_assignments = {evaluated_assignment, opponent_assignment};
    std::sort(result.provenance.participant_assignments.begin(),
              result.provenance.participant_assignments.end(),
              [](const auto& left, const auto& right) {
                  return left.participant_policy_assignment_id <
                         right.participant_policy_assignment_id;
              });
    std::string provenance_error;
    if (!config.provenance_resolver.validate(result.provenance, config.environment_config, spec,
                                             &provenance_error)) {
        fail("gameplay policy provenance was rejected: " + provenance_error);
    }
    return result;
}

struct SingleRun final {
    GameplayJobResultV1 result;
    ReplayAdmissionSummaryV1 replay_admission;
};

SingleRun no_envelope_failure(const EvaluationContextV1& context,
                              const EvaluationJobV1& job,
                              const std::optional<GameplayFailureStage>& stage,
                              const std::string& code) {
    ReplayAdmissionSummaryV1 replay;
    replay.evaluation_identity = context.evaluation_identity;
    replay.evaluation_job_identity = evaluation_job_identity(job);
    replay.failure_stage = stage;
    replay.failure_code = code;

    GameplayJobResultV1 result;
    result.evaluation_identity = context.evaluation_identity;
    result.evaluation_job_identity = evaluation_job_identity(job);
    result.checkpoint_identity = context.checkpoint_identity;
    result.status = GameplayJobStatus::Failed;
    result.started = true;
    result.failure_stage = stage;
    result.failure_code = code;
    result.replay_admission_summary_identity = replay_admission_summary_identity(replay);
    return {std::move(result), std::move(replay)};
}

void attach_public_trajectory_ids(const trajectory::EpisodeEnvelope& envelope,
                                  GameplayJobResultV1& result,
                                  ReplayAdmissionSummaryV1& replay) {
    if (std::holds_alternative<trajectory::FailedClosure>(envelope.closure)) return;
    const auto gameplay_id = trajectory::public_gameplay_trajectory_id(envelope);
    const auto record_id = trajectory::trajectory_record_id(envelope);
    result.trajectory_record_id = record_id;
    result.public_gameplay_trajectory_id = gameplay_id;
    replay.trajectory_record_id = record_id;
    replay.public_gameplay_trajectory_id = gameplay_id;
}

SingleRun finalize_run(
    const FrozenGameplayEvaluatorConfigV1& config,
    const EvaluationJobV1& job,
    const environment::EpisodeSpec& spec,
    trajectory::TrajectoryRecorder& recorder,
    environment::EpisodicEnvironment& environment,
    const std::optional<environment::EpisodeInterrupted>& interruption,
    const bool quarantined,
    const std::optional<GameplayFailureStage>& prior_failure_stage = std::nullopt,
    const std::optional<std::string>& prior_failure_code = std::nullopt) {
    try {
        (void)spec;
        (void)environment;
        const auto sealed = recorder.seal();
        if (!sealed.has_value()) {
            return no_envelope_failure(config.evaluation_context, job,
                                       GameplayFailureStage::Environment,
                                       "TRAJECTORY_SEAL_FAILURE");
        }
        const auto& envelope = *sealed;
        ReplayAdmissionSummaryV1 replay;
        replay.evaluation_identity = config.evaluation_context.evaluation_identity;
        replay.evaluation_job_identity = evaluation_job_identity(job);
        replay.failure_stage = prior_failure_stage;
        replay.failure_code = prior_failure_code;
        GameplayJobResultV1 result;
        result.evaluation_identity = config.evaluation_context.evaluation_identity;
        result.evaluation_job_identity = evaluation_job_identity(job);
        result.checkpoint_identity = config.evaluation_context.checkpoint_identity;
        result.started = true;
        result.failure_stage = prior_failure_stage;
        result.failure_code = prior_failure_code;
        attach_public_trajectory_ids(envelope, result, replay);

        // A policy/inference failure is not an engine StepRejected.  The existing recorder has
        // no public mutation to mark in that case, so the sealed interrupted envelope remains
        // an unadmitted diagnostic.  Never run replay/admission on it as if a policy action had
        // been accepted; the job is a typed failure and remains in the schedule denominator.
        if (prior_failure_stage.has_value() &&
            *prior_failure_stage != GameplayFailureStage::Environment) {
            result.status = GameplayJobStatus::Failed;
            result.terminal_observed = false;
            result.terminal_outcome.reset();
            replay.replay_status = ReplayAdmissionStatus::NotRun;
            replay.admission_status = ReplayAdmissionStatus::NotRun;
            result.replay_admission_summary_identity = replay_admission_summary_identity(replay);
            return {std::move(result), std::move(replay)};
        }

        trajectory::CandidateTrajectoryShard shard;
        const auto envelope_bytes = trajectory::canonical_episode_envelope_bytes(envelope);
        shard.entries.push_back({trace::sha256_bytes(envelope_bytes), envelope_bytes});
        trajectory::RestrictedCollectionEvidenceBundle evidence;
        evidence.candidate_shard_artifact_sha256 =
            trajectory::candidate_shard_artifact_sha256(shard);
        if (interruption.has_value()) {
            evidence.interrupted_episodes.push_back(
                {shard.entries.front().episode_envelope_sha256,
                 trajectory::RestrictedReplayEvidence{
                     interruption->contract_id,
                     interruption->episode_semantic_id,
                     interruption->reason,
                     interruption->run_control_evidence.engine_process_budget,
                     interruption->run_control_evidence.semantic_action_budget,
                     interruption->run_control_evidence.engine_process_count,
                     interruption->run_control_evidence.semantic_action_count,
                     interruption->final_engine_step_index}});
        }
        const auto evidence_artifact =
            trajectory::restricted_collection_evidence_artifact_sha256(evidence);
        trajectory::admission::ReplayOptions options;
        options.cancellation_source = config.run_control.cancellation.source;
        if (std::holds_alternative<trajectory::TerminalClosure>(envelope.closure)) {
            options.terminal_run_control = config.run_control;
        }
        std::optional<trajectory::RestrictedReplayEvidence> replay_evidence;
        if (!evidence.interrupted_episodes.empty()) {
            replay_evidence = evidence.interrupted_episodes.front().evidence;
        }

        if (quarantined) {
            if (recorder.manifest().collection_disposition.kind !=
                trajectory::CollectionDispositionKind::QuarantinedAfterPolicyRejection) {
                result.status = GameplayJobStatus::Failed;
                result.terminal_observed = false;
                result.terminal_outcome.reset();
                replay.replay_status = ReplayAdmissionStatus::NotRun;
                replay.admission_status = ReplayAdmissionStatus::NotRun;
                result.replay_admission_summary_identity = replay_admission_summary_identity(replay);
                return {std::move(result), std::move(replay)};
            }
            std::string admission_error;
            const auto verification = trajectory::admission::verify_candidate_shard_for_admission(
                shard, evidence, evidence.candidate_shard_artifact_sha256, evidence_artifact,
                options, config.provenance_resolver, &admission_error);
            if (verification.has_value()) {
                return no_envelope_failure(config.evaluation_context, job,
                                           GameplayFailureStage::Admission,
                                           "QUARANTINED_TRAJECTORY_PASSED_ADMISSION");
            }
            replay.replay_status = ReplayAdmissionStatus::NotRun;
            replay.admission_status = ReplayAdmissionStatus::Quarantined;
            result.status = GameplayJobStatus::Quarantined;
            result.terminal_observed = false;
            result.terminal_outcome.reset();
            result.replay_admission_summary_identity = replay_admission_summary_identity(replay);
            return {std::move(result), std::move(replay)};
        }

        const auto replay_result = trajectory::admission::replay_episode(
            envelope, replay_evidence, options);
        if (!replay_result.accepted) {
            replay.replay_status = ReplayAdmissionStatus::Failed;
            replay.admission_status = ReplayAdmissionStatus::NotRun;
            replay.failure_stage = GameplayFailureStage::Replay;
            replay.failure_code = "REPLAY_FAILURE";
            result.status = GameplayJobStatus::Failed;
            result.failure_stage = GameplayFailureStage::Replay;
            result.failure_code = "REPLAY_FAILURE";
            result.replay_admission_summary_identity = replay_admission_summary_identity(replay);
            return {std::move(result), std::move(replay)};
        }
        replay.replay_status = ReplayAdmissionStatus::Passed;

        std::string admission_error;
        const auto verification = trajectory::admission::verify_candidate_shard_for_admission(
            shard, evidence, evidence.candidate_shard_artifact_sha256, evidence_artifact,
            options, config.provenance_resolver, &admission_error);
        if (!verification.has_value()) {
            replay.admission_status = ReplayAdmissionStatus::Failed;
            replay.failure_stage = GameplayFailureStage::Admission;
            replay.failure_code = "ADMISSION_FAILURE";
            result.status = GameplayJobStatus::Failed;
            result.failure_stage = GameplayFailureStage::Admission;
            result.failure_code = "ADMISSION_FAILURE";
            result.replay_admission_summary_identity = replay_admission_summary_identity(replay);
            return {std::move(result), std::move(replay)};
        }
        std::string receipt_error;
        const auto receipt = trajectory::issue_admission_receipt(*verification, &receipt_error);
        if (!receipt.has_value()) {
            replay.admission_status = ReplayAdmissionStatus::Failed;
            replay.failure_stage = GameplayFailureStage::Admission;
            replay.failure_code = "ADMISSION_RECEIPT_FAILURE";
            result.status = GameplayJobStatus::Failed;
            result.failure_stage = GameplayFailureStage::Admission;
            result.failure_code = "ADMISSION_RECEIPT_FAILURE";
            result.replay_admission_summary_identity = replay_admission_summary_identity(replay);
            return {std::move(result), std::move(replay)};
        }
        replay.admission_status = ReplayAdmissionStatus::Passed;
        result.failure_stage = prior_failure_stage;
        result.failure_code = prior_failure_code;
        if (const auto* terminal = std::get_if<trajectory::TerminalClosure>(&envelope.closure)) {
            result.terminal_observed = true;
            result.terminal_outcome = TerminalOutcomeV1{
                true, terminal->winner, terminal->win_reason};
            if (terminal->winner == job.evaluated_policy_seat) {
                result.status = GameplayJobStatus::TrustedWin;
            } else if (terminal->winner == 2) {
                result.status = GameplayJobStatus::TrustedDraw;
            } else {
                result.status = GameplayJobStatus::TrustedLoss;
            }
        } else if (std::holds_alternative<trajectory::InterruptedClosure>(envelope.closure)) {
            result.status = GameplayJobStatus::Interrupted;
            result.terminal_observed = false;
            result.terminal_outcome.reset();
        } else {
            result.status = GameplayJobStatus::Failed;
            result.failure_stage = GameplayFailureStage::Environment;
            result.failure_code = "FAILED_TRAJECTORY_CLOSURE";
        }
        result.replay_admission_summary_identity = replay_admission_summary_identity(replay);
        return {std::move(result), std::move(replay)};
    } catch (const std::exception&) {
        return no_envelope_failure(config.evaluation_context, job,
                                   GameplayFailureStage::Admission,
                                   "GAMEPLAY_FINALIZATION_FAILURE");
    } catch (...) {
        return no_envelope_failure(config.evaluation_context, job,
                                   GameplayFailureStage::Admission,
                                   "GAMEPLAY_FINALIZATION_FAILURE");
    }
}

SingleRun run_one_job(const FrozenGameplayEvaluatorConfigV1& config,
                      const EvaluationJobV1& job) {
    try {
        const auto seat_assignment = job.seat_0_deck_role_id == kSwordsoulDeckIdentity
                                         ? environment::SeatAssignment::Normal
                                         : environment::SeatAssignment::Mirror;
        environment::EpisodeSpec spec;
        spec.contract_id = std::string(environment::kEpisodicEnvironmentV2ContractId);
        spec.root_seed = job.deterministic_seed;
        spec.seat_assignment = seat_assignment;
        spec.starting_player = job.starting_player;

        auto runtime = make_runtime_policies(config, job, spec);
        auto factory = environment::EpisodicEnvironment::create(config.environment_config);
        if (!std::holds_alternative<std::unique_ptr<environment::EpisodicEnvironment>>(factory)) {
            return no_envelope_failure(config.evaluation_context, job,
                                       GameplayFailureStage::Environment,
                                       "ENVIRONMENT_FACTORY_REJECTED");
        }
        auto environment = std::move(
            std::get<std::unique_ptr<environment::EpisodicEnvironment>>(factory));
        trajectory::TrajectoryRecorder recorder(
            config.environment_config, spec, runtime.provenance,
            config.provenance_resolver);
        const auto reset = environment->reset(spec, config.run_control);
        const auto* reset_accepted = std::get_if<environment::ResetAccepted>(&reset);
        if (reset_accepted == nullptr) {
            return no_envelope_failure(config.evaluation_context, job,
                                       GameplayFailureStage::Environment,
                                       "RESET_REJECTED");
        }
        auto boundary = reset_accepted->next;
        std::optional<environment::EpisodeInterrupted> interruption;
        std::optional<trajectory::TerminalViews> terminal_views;
        if (std::holds_alternative<environment::EpisodeTerminal>(boundary)) {
            const auto player_zero = environment->perspective_terminal_view(0);
            const auto player_one = environment->perspective_terminal_view(1);
            if (!player_zero.has_value() || !player_one.has_value()) {
                return no_envelope_failure(config.evaluation_context, job,
                                           GameplayFailureStage::Environment,
                                           "TERMINAL_VIEW_FAILURE");
            }
            terminal_views = trajectory::TerminalViews{*player_zero, *player_one};
        }
        std::string recorder_error;
        if (!recorder.on_reset_accepted(*reset_accepted, terminal_views, &recorder_error)) {
            return no_envelope_failure(config.evaluation_context, job,
                                       GameplayFailureStage::Environment,
                                       "RESET_RECORDING_FAILURE");
        }
        if (const auto* reset_interrupted =
                std::get_if<environment::EpisodeInterrupted>(&boundary)) {
            interruption = *reset_interrupted;
        }
        if (recorder.lifecycle() == trajectory::RecorderLifecycle::Closed) {
            return finalize_run(config, job, spec, recorder, *environment, interruption, false);
        }

        for (;;) {
            const auto* frame = std::get_if<environment::DecisionFrame>(&boundary);
            if (frame == nullptr || frame->acting_player > 1) {
                return finalize_run(config, job, spec, recorder, *environment, interruption,
                                    true, GameplayFailureStage::Environment,
                                    std::optional<std::string>{"INVALID_DECISION_FRAME"});
            }
            policy::PolicySelection selection;
        const bool evaluated_turn = frame->acting_player == job.evaluated_policy_seat;
            if (evaluated_turn) {
                selection = runtime.evaluated.select(*frame);
            } else {
                selection = runtime.opponent.policy.select(
                    policy::PolicyInput{frame->public_observation, frame->request.candidates});
            }
            if (!selection) {
                const auto evaluated_failure = evaluated_turn
                                                   ? runtime.evaluated.last_failure()
                                                   : std::optional<CheckpointPolicyFailureV1>{};
                const auto failure = detail::classify_policy_selection_failure(
                    evaluated_turn, evaluated_failure);
                if (evaluated_turn) runtime.evaluated.reject_pending_proposal();
                else runtime.opponent.policy.reject_pending_proposal();
                const auto interrupted = environment->interrupt(environment::InterruptRequest{
                    std::string(environment::kEpisodicEnvironmentV2ContractId),
                    environment::InterruptionReason::AdministrativeCancel});
                const auto* accepted_interrupt =
                    std::get_if<environment::InterruptAccepted>(&interrupted);
                if (accepted_interrupt == nullptr ||
                    !recorder.on_interrupt_accepted(
                        std::optional<environment::DecisionFrame>{*frame},
                        *accepted_interrupt, &recorder_error)) {
                    return no_envelope_failure(config.evaluation_context, job,
                                               GameplayFailureStage::Environment,
                                               "POLICY_FAILURE_INTERRUPT_FAILURE");
                }
                return finalize_run(
                    config, job, spec, recorder, *environment,
                    accepted_interrupt->interruption, true, failure.stage,
                    std::optional<std::string>{failure.code});
            }

            environment::ActionSelection action;
            action.contract_id = frame->contract_id;
            action.episode_semantic_id = frame->episode_semantic_id;
            action.public_semantic_decision_id = frame->public_semantic_decision_id;
            action.submission_token = frame->submission_token;
            action.public_action_key = selection.value->public_action_key;
            const auto pre_rejection_frame = *frame;
            const auto stepped = environment->step(action);
            if (const auto* rejected = std::get_if<environment::StepRejected>(&stepped)) {
                if (evaluated_turn) runtime.evaluated.reject_pending_proposal();
                else runtime.opponent.policy.reject_pending_proposal();
                if (!recorder.on_step_rejected(*rejected, true, &recorder_error)) {
                    return no_envelope_failure(config.evaluation_context, job,
                                               GameplayFailureStage::Environment,
                                               "STEP_REJECTION_RECORDING_FAILURE");
                }
                const auto interrupted = environment->interrupt(environment::InterruptRequest{
                    std::string(environment::kEpisodicEnvironmentV2ContractId),
                    environment::InterruptionReason::AdministrativeCancel});
                const auto* accepted_interrupt =
                    std::get_if<environment::InterruptAccepted>(&interrupted);
                if (accepted_interrupt == nullptr ||
                    !recorder.on_interrupt_accepted(
                        std::optional<environment::DecisionFrame>{pre_rejection_frame},
                        *accepted_interrupt, &recorder_error)) {
                    return no_envelope_failure(config.evaluation_context, job,
                                               GameplayFailureStage::Environment,
                                               "STEP_REJECTION_INTERRUPT_FAILURE");
                }
                return finalize_run(
                    config, job, spec, recorder, *environment,
                    accepted_interrupt->interruption, true, GameplayFailureStage::Environment,
                    std::optional<std::string>{"STEP_REJECTED"});
            }
            const auto* accepted = std::get_if<environment::StepAccepted>(&stepped);
            if (accepted == nullptr) {
                return finalize_run(config, job, spec, recorder, *environment, interruption,
                                    true, GameplayFailureStage::Environment,
                                    std::optional<std::string>{"UNKNOWN_STEP_RESULT"});
            }
            const auto binding = evaluated_turn
                                     ? runtime.evaluated.execution_binding()
                                     : runtime.opponent.execution_binding();
            const auto attribution = no_rng_attribution(*frame, binding);
            terminal_views.reset();
            if (std::holds_alternative<environment::EpisodeTerminal>(accepted->next)) {
                const auto player_zero = environment->perspective_terminal_view(0);
                const auto player_one = environment->perspective_terminal_view(1);
                if (!player_zero.has_value() || !player_one.has_value()) {
                    return no_envelope_failure(config.evaluation_context, job,
                                               GameplayFailureStage::Environment,
                                               "TERMINAL_VIEW_FAILURE");
                }
                terminal_views = trajectory::TerminalViews{*player_zero, *player_one};
            }
            if (!recorder.on_step_accepted(*accepted, attribution, terminal_views,
                                           &recorder_error)) {
                return no_envelope_failure(config.evaluation_context, job,
                                           GameplayFailureStage::Environment,
                                           "STEP_RECORDING_FAILURE");
            }
            const bool committed = evaluated_turn
                                       ? runtime.evaluated.commit(accepted->transition)
                                       : runtime.opponent.policy.commit(accepted->transition);
            if (!committed) {
                return finalize_run(config, job, spec, recorder, *environment, interruption,
                                    true, GameplayFailureStage::Environment,
                                    std::optional<std::string>{"POLICY_COMMIT_FAILURE"});
            }
            interruption.reset();
            if (const auto* next_interrupted =
                    std::get_if<environment::EpisodeInterrupted>(&accepted->next)) {
                interruption = *next_interrupted;
            }
            if (recorder.lifecycle() == trajectory::RecorderLifecycle::Closed) {
                return finalize_run(config, job, spec, recorder, *environment, interruption, false);
            }
            boundary = accepted->next;
        }
    } catch (const std::exception&) {
        return no_envelope_failure(config.evaluation_context, job,
                                   GameplayFailureStage::Environment,
                                   "GAMEPLAY_JOB_EXCEPTION");
    } catch (...) {
        return no_envelope_failure(config.evaluation_context, job,
                                   GameplayFailureStage::Environment,
                                   "GAMEPLAY_JOB_EXCEPTION");
    }
}

GameplaySummaryV1 summarize(
    const EvaluationContextV1& context,
    const std::vector<GameplayJobResultV1>& results,
    const std::vector<ReplayAdmissionSummaryV1>& replay_summaries) {
    GameplaySummaryV1 summary;
    summary.evaluation_identity = context.evaluation_identity;
    summary.evaluation_corpus_identity = context.evaluation_corpus_identity;
    summary.evaluation_job_manifest_identity = context.evaluation_job_manifest_identity;
    summary.checkpoint_identity = context.checkpoint_identity;
    summary.scheduled_job_count = static_cast<std::uint32_t>(results.size());
    for (const auto& result : results) {
        summary.gameplay_job_result_identities.push_back(
            gameplay_job_result_identity(result));
        if (result.started) ++summary.started_job_count;
        switch (result.status) {
        case GameplayJobStatus::TrustedWin:
            ++summary.trusted_win_count;
            ++summary.completed_terminal_job_count;
            break;
        case GameplayJobStatus::TrustedLoss:
            ++summary.trusted_loss_count;
            ++summary.completed_terminal_job_count;
            break;
        case GameplayJobStatus::TrustedDraw:
            ++summary.trusted_draw_count;
            ++summary.completed_terminal_job_count;
            break;
        case GameplayJobStatus::Interrupted: ++summary.interrupted_job_count; break;
        case GameplayJobStatus::Failed: ++summary.failed_job_count; break;
        case GameplayJobStatus::Quarantined: ++summary.quarantined_job_count; break;
        }
        if (result.fallback_assisted) ++summary.fallback_assisted_job_count;
        if (detail::counts_as_inference_failure(result)) {
            ++summary.inference_failure_count;
        }
    }
    for (const auto& replay : replay_summaries) {
        if (replay.replay_status == ReplayAdmissionStatus::Failed) {
            ++summary.replay_failure_count;
        }
        if (replay.admission_status == ReplayAdmissionStatus::Failed) {
            ++summary.admission_failure_count;
        }
    }
    summary.wilson_numerator = summary.trusted_win_count;
    summary.wilson_denominator = summary.trusted_win_count + summary.trusted_loss_count;
    summary.wilson_interval_status = summary.wilson_denominator == 0
                                         ? "NOT_APPLICABLE"
                                         : "AVAILABLE";
    std::string error;
    if (!validate_gameplay_summary(summary, &error)) fail(error);
    return summary;
}

}  // namespace

FrozenGameplayEvaluatorCreateResult create_frozen_gameplay_evaluator(
    FrozenGameplayEvaluatorConfigV1 config) noexcept {
    try {
        std::string error;
        if (!validate_evaluation_context(config.evaluation_context, &error)) {
            fail("evaluation context rejected: " + error);
        }
        if (config.evaluation_context.corpus_profile_identity !=
            kImplementationAcceptanceProfile) {
            fail("meaningful contexts require the separate meaningful evaluator constructor");
        }
        if (!trajectory::is_current_certified_environment(config.environment_config)) {
            fail("gameplay evaluator requires the current certified environment");
        }
        (void)config.card_vocabulary.canonical_bytes();
        if (config.evaluation_context.checkpoint_identity == kSmokeCheckpointIdentity &&
            config.card_vocabulary.identity() != kSmokeCardVocabularyIdentity) {
            fail("checkpoint-bound CardVocabulary identity is not the accepted smoke vocabulary");
        }
        if (!config.inference_provider ||
            config.evaluated_policy_artifact.policy_kind !=
                trajectory::PolicyKind::NeuralCheckpoint ||
            !config.evaluated_policy_artifact.model_checkpoint_identity.has_value() ||
            *config.evaluated_policy_artifact.model_checkpoint_identity !=
                config.evaluation_context.checkpoint_identity ||
            config.evaluated_policy_artifact.policy_artifact_id !=
                trajectory::compute_policy_artifact_id(config.evaluated_policy_artifact) ||
            !config.provenance_resolver.can_resolve(
                trajectory::ProvenanceKind::ModelCheckpointArtifact,
                config.evaluation_context.checkpoint_identity) ||
            config.run_control.engine_process_budget == 0 ||
            config.run_control.semantic_action_budget == 0) {
            fail("checkpoint-bound gameplay evaluator configuration is invalid");
        }
        return {std::optional<FrozenGameplayEvaluator>(
                    FrozenGameplayEvaluator(std::move(config))),
                std::nullopt};
    } catch (const std::exception& exception) {
        return {std::nullopt,
                policy::PolicyError{policy::PolicyErrorCode::InvalidConfiguration,
                                    exception.what()}};
    } catch (...) {
        return {std::nullopt,
                policy::PolicyError{policy::PolicyErrorCode::InvalidConfiguration,
                                    "frozen gameplay evaluator construction threw"}};
    }
}

FrozenGameplayEvaluatorCreateResult create_meaningful_frozen_gameplay_evaluator(
    MeaningfulFixedMatchupEvaluatorConfigV1 config) noexcept {
    try {
        std::string binding_error;
        if (!validate_meaningful_checkpoint_binding(
                config.evaluation_context.checkpoint_binding,
                config.card_vocabulary, &binding_error)) {
            fail("meaningful checkpoint binding rejected: " + binding_error);
        }
        std::string context_error;
        if (!validate_evaluation_context(
                config.evaluation_context.evaluation_context, &context_error)) {
            fail("meaningful evaluation context rejected: " + context_error);
        }
        if (config.evaluation_context.evaluation_context.corpus_profile_identity !=
                kMeaningfulFixedMatchupProfile ||
            config.evaluation_context.evaluation_context.corpus_kind !=
                kMeaningfulFixedMatchupKind ||
            config.evaluation_context.checkpoint_binding.checkpoint_identity() !=
                config.evaluation_context.evaluation_context.checkpoint_identity) {
            fail("meaningful checkpoint/context binding is inconsistent");
        }
        if (!trajectory::is_current_certified_environment(config.environment_config)) {
            fail("meaningful gameplay evaluator requires the current certified environment");
        }
        (void)config.card_vocabulary.canonical_bytes();
        if (!config.inference_provider ||
            config.evaluated_policy_artifact.policy_kind !=
                trajectory::PolicyKind::NeuralCheckpoint ||
            !config.evaluated_policy_artifact.model_checkpoint_identity.has_value() ||
            *config.evaluated_policy_artifact.model_checkpoint_identity !=
                config.evaluation_context.evaluation_context.checkpoint_identity ||
            config.evaluated_policy_artifact.policy_artifact_id !=
                trajectory::compute_policy_artifact_id(config.evaluated_policy_artifact) ||
            !config.provenance_resolver.can_resolve(
                trajectory::ProvenanceKind::ModelCheckpointArtifact,
                config.evaluation_context.evaluation_context.checkpoint_identity) ||
            config.run_control.engine_process_budget == 0 ||
            config.run_control.semantic_action_budget == 0) {
            fail("meaningful checkpoint-bound gameplay evaluator configuration is invalid");
        }

        FrozenGameplayEvaluatorConfigV1 base_config{
            config.evaluation_context.evaluation_context,
            std::move(config.environment_config),
            std::move(config.evaluated_policy_artifact),
            std::move(config.card_vocabulary),
            std::move(config.inference_provider),
            std::move(config.provenance_resolver),
            std::move(config.run_control)};
        return {std::optional<FrozenGameplayEvaluator>(
                    FrozenGameplayEvaluator(std::move(base_config))),
                std::nullopt};
    } catch (const std::exception& exception) {
        return {std::nullopt,
                policy::PolicyError{policy::PolicyErrorCode::InvalidConfiguration,
                                    exception.what()}};
    } catch (...) {
        return {std::nullopt,
                policy::PolicyError{policy::PolicyErrorCode::InvalidConfiguration,
                                    "meaningful frozen gameplay evaluator construction threw"}};
    }
}

GameplayEvaluationResultV1 FrozenGameplayEvaluator::run() noexcept {
    try {
        std::vector<GameplayJobResultV1> results;
        std::vector<ReplayAdmissionSummaryV1> replay_summaries;
        results.reserve(config_.evaluation_context.jobs.size());
        replay_summaries.reserve(config_.evaluation_context.jobs.size());
        const bool repeated = has_run_;
        has_run_ = true;
        for (const auto& job : config_.evaluation_context.jobs) {
            const auto single = repeated
                                    ? no_envelope_failure(config_.evaluation_context, job,
                                                          GameplayFailureStage::Environment,
                                                          "EVALUATOR_ALREADY_RAN")
                                    : run_one_job(config_, job);
            results.push_back(single.result);
            replay_summaries.push_back(single.replay_admission);
        }
        GameplayEvaluationResultV1 result;
        result.job_results = std::move(results);
        result.replay_admission_summaries = std::move(replay_summaries);
        result.summary = summarize(config_.evaluation_context, result.job_results,
                                   result.replay_admission_summaries);
        return result;
    } catch (...) {
        // The evaluator always returns a deterministic typed failure set.  A context accepted
        // by create() has a fixed schedule, so even an unexpected exception cannot erase jobs
        // from the result population.
        GameplayEvaluationResultV1 result;
        for (const auto& job : config_.evaluation_context.jobs) {
            const auto single = no_envelope_failure(config_.evaluation_context, job,
                                                    GameplayFailureStage::Environment,
                                                    "EVALUATOR_INTERNAL_FAILURE");
            result.job_results.push_back(single.result);
            result.replay_admission_summaries.push_back(single.replay_admission);
        }
        try {
            result.summary = summarize(config_.evaluation_context, result.job_results,
                                       result.replay_admission_summaries);
        } catch (...) {
            // This branch is unreachable for a valid context/result vector, but keeps the
            // noexcept boundary free of a process-terminating exception.
        }
        return result;
    }
}

}  // namespace ygo::phase6::task5c
