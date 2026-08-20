#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>

#include "ygo/observation/observed_zone.hpp"

#ifdef YGO_M4_PERFORMANCE_AUDIT
namespace ygo::observation {

// M4.2 is deliberately opt-in. This header is included only by audit builds;
// the normal M4 worker has no collector or sidecar state in its contract.
enum class PerformanceAuditBucket : std::uint8_t {
    QueryField,
    QueryLocation,
    QueryIndividual,
    QueryDecode,
    ZoneProjection,
    EntityProjection,
    RelationshipProjection,
    VisibilityPrivacy,
    CandidateConsistency,
    CanonicalSerialization,
    Hash,
    Other,
    Count,
};

enum class PerformanceAuditAuxiliaryBucket : std::uint8_t {
    PublicStateHash,
    PublicStateHashQueryField,
    Count,
};

enum class PerformanceAuditSetupBucket : std::uint8_t {
    CoreHostSetup,
    FixtureScriptLoad,
    ScriptLoad,
    Count,
};

struct PerformanceAuditTiming {
    std::uint64_t total_us = 0;
    std::uint64_t calls = 0;

    std::uint64_t mean_us_per_call() const noexcept {
        return calls == 0 ? 0 : total_us / calls;
    }
};

struct PerformanceAuditZoneCounters {
    std::uint64_t entities_projected = 0;
    std::uint64_t identity_known = 0;
    std::uint64_t redacted = 0;
};

struct PerformanceAuditCounters {
    std::uint64_t observations = 0;
    std::uint64_t query_field_calls = 0;
    std::uint64_t query_location_calls = 0;
    std::uint64_t query_individual_calls = 0;
    std::uint64_t entities_projected = 0;
    std::uint64_t identity_known_entities = 0;
    std::uint64_t redacted_entities = 0;
    std::uint64_t static_card_data_lookups = 0;
    std::uint64_t current_property_projections = 0;
    std::uint64_t relationship_objects = 0;
    std::uint64_t allocation_copy_events = 0;
    std::uint64_t script_loads = 0;
};

struct PerformanceAuditDetailCounters {
    std::uint64_t query_decode_calls = 0;
    std::uint64_t zone_projection_calls = 0;
    std::uint64_t relationship_resolution_events = 0;
    std::uint64_t observation_query_field_calls = 0;
    std::uint64_t public_state_hash_query_field_calls = 0;
    std::uint64_t script_reader_requests = 0;
};

struct PerformanceAuditSnapshot {
    std::uint64_t observation_total_us = 0;
    std::array<PerformanceAuditTiming, static_cast<std::size_t>(PerformanceAuditBucket::Count)>
        observation_timing{};
    std::array<PerformanceAuditTiming,
               static_cast<std::size_t>(PerformanceAuditAuxiliaryBucket::Count)>
        auxiliary_timing{};
    std::array<PerformanceAuditTiming, static_cast<std::size_t>(PerformanceAuditSetupBucket::Count)>
        setup_timing{};
    PerformanceAuditCounters counters;
    PerformanceAuditDetailCounters detail_counters;
    std::array<PerformanceAuditZoneCounters, 11> entities_by_zone{};
};

class PerformanceAuditCollector final {
public:
    using Clock = std::chrono::steady_clock;

    class Scope final {
    public:
        Scope() noexcept = default;

        Scope(PerformanceAuditCollector* collector, PerformanceAuditBucket bucket) noexcept
            : collector_(collector), bucket_(bucket), start_(Clock::now()), parent_(collector == nullptr
                                                                                         ? nullptr
                                                                                         : collector->active_scope_),
              active_(true) {
            if (collector_ != nullptr) {
                collector_->active_scope_ = this;
            }
        }

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

        Scope(Scope&& other) noexcept
            : collector_(other.collector_), bucket_(other.bucket_), start_(other.start_),
              parent_(other.parent_), child_elapsed_us_(other.child_elapsed_us_), active_(other.active_) {
            if (collector_ != nullptr && active_ && collector_->active_scope_ == &other) {
                collector_->active_scope_ = this;
            }
            other.collector_ = nullptr;
            other.active_ = false;
        }

        Scope& operator=(Scope&&) = delete;

        ~Scope() { finish(); }

        void finish() noexcept {
            if (!active_) {
                return;
            }
            active_ = false;
            if (collector_ == nullptr) {
                return;
            }
            const auto elapsed = elapsed_us(start_, Clock::now());
            const auto exclusive = elapsed >= child_elapsed_us_ ? elapsed - child_elapsed_us_ : 0;
            collector_->record_timing(bucket_, exclusive);
            collector_->active_scope_ = parent_;
            if (parent_ != nullptr) {
                parent_->child_elapsed_us_ += elapsed;
            }
        }

    private:
        static std::uint64_t elapsed_us(const Clock::time_point start,
                                        const Clock::time_point end) noexcept {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
        }

        PerformanceAuditCollector* collector_ = nullptr;
        PerformanceAuditBucket bucket_ = PerformanceAuditBucket::Other;
        Clock::time_point start_{};
        Scope* parent_ = nullptr;
        std::uint64_t child_elapsed_us_ = 0;
        bool active_ = false;
    };

    class AuxiliaryScope final {
    public:
        AuxiliaryScope() noexcept = default;

        AuxiliaryScope(PerformanceAuditCollector* collector,
                       PerformanceAuditAuxiliaryBucket bucket) noexcept
            : collector_(collector), bucket_(bucket), start_(Clock::now()), parent_(collector == nullptr
                                                                                           ? nullptr
                                                                                           : collector->active_auxiliary_scope_),
              active_(true) {
            if (collector_ != nullptr) {
                collector_->active_auxiliary_scope_ = this;
            }
        }

        AuxiliaryScope(const AuxiliaryScope&) = delete;
        AuxiliaryScope& operator=(const AuxiliaryScope&) = delete;

        AuxiliaryScope(AuxiliaryScope&& other) noexcept
            : collector_(other.collector_), bucket_(other.bucket_), start_(other.start_),
              parent_(other.parent_), child_elapsed_us_(other.child_elapsed_us_), active_(other.active_) {
            if (collector_ != nullptr && active_ && collector_->active_auxiliary_scope_ == &other) {
                collector_->active_auxiliary_scope_ = this;
            }
            other.collector_ = nullptr;
            other.active_ = false;
        }

        AuxiliaryScope& operator=(AuxiliaryScope&&) = delete;

        ~AuxiliaryScope() { finish(); }

        void finish() noexcept {
            if (!active_) {
                return;
            }
            active_ = false;
            if (collector_ == nullptr) {
                return;
            }
            const auto elapsed = elapsed_us(start_, Clock::now());
            const auto exclusive = elapsed >= child_elapsed_us_ ? elapsed - child_elapsed_us_ : 0;
            collector_->record_auxiliary_timing(bucket_, exclusive);
            collector_->active_auxiliary_scope_ = parent_;
            if (parent_ != nullptr) {
                parent_->child_elapsed_us_ += elapsed;
            }
        }

    private:
        static std::uint64_t elapsed_us(const Clock::time_point start,
                                        const Clock::time_point end) noexcept {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
        }

        PerformanceAuditCollector* collector_ = nullptr;
        PerformanceAuditAuxiliaryBucket bucket_ = PerformanceAuditAuxiliaryBucket::PublicStateHash;
        Clock::time_point start_{};
        AuxiliaryScope* parent_ = nullptr;
        std::uint64_t child_elapsed_us_ = 0;
        bool active_ = false;
    };

    class SetupScope final {
    public:
        SetupScope() noexcept = default;

        SetupScope(PerformanceAuditCollector* collector,
                   PerformanceAuditSetupBucket bucket) noexcept
            : collector_(collector), bucket_(bucket), start_(Clock::now()),
              parent_(collector == nullptr ? nullptr : collector->active_setup_scope_), active_(true) {
            if (collector_ != nullptr) {
                collector_->active_setup_scope_ = this;
            }
        }

        SetupScope(const SetupScope&) = delete;
        SetupScope& operator=(const SetupScope&) = delete;
        SetupScope(SetupScope&& other) noexcept
            : collector_(other.collector_), bucket_(other.bucket_), start_(other.start_),
              parent_(other.parent_), child_elapsed_us_(other.child_elapsed_us_), active_(other.active_) {
            if (collector_ != nullptr && active_ && collector_->active_setup_scope_ == &other) {
                collector_->active_setup_scope_ = this;
            }
            other.collector_ = nullptr;
            other.active_ = false;
        }
        SetupScope& operator=(SetupScope&&) = delete;

        ~SetupScope() { finish(); }

        void finish() noexcept {
            if (!active_) {
                return;
            }
            active_ = false;
            if (collector_ == nullptr) {
                return;
            }
            const auto elapsed = elapsed_us(start_, Clock::now());
            const auto exclusive = elapsed >= child_elapsed_us_ ? elapsed - child_elapsed_us_ : 0;
            collector_->record_setup_timing(bucket_, exclusive);
            collector_->active_setup_scope_ = parent_;
            if (parent_ != nullptr) {
                parent_->child_elapsed_us_ += elapsed;
            }
        }

        void cancel() noexcept {
            if (!active_) {
                return;
            }
            active_ = false;
            if (collector_ != nullptr) {
                collector_->active_setup_scope_ = parent_;
            }
        }

    private:
        static std::uint64_t elapsed_us(const Clock::time_point start,
                                        const Clock::time_point end) noexcept {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
        }

        PerformanceAuditCollector* collector_ = nullptr;
        PerformanceAuditSetupBucket bucket_ = PerformanceAuditSetupBucket::CoreHostSetup;
        Clock::time_point start_{};
        SetupScope* parent_ = nullptr;
        std::uint64_t child_elapsed_us_ = 0;
        bool active_ = false;
    };

    class ObservationScope final {
    public:
        ObservationScope() noexcept = default;

        explicit ObservationScope(PerformanceAuditCollector* collector) noexcept
            : collector_(collector), start_(Clock::now()), previous_(collector == nullptr
                                                                          ? false
                                                                          : collector->observation_active_) {
            if (collector_ != nullptr) {
                collector_->named_observation_time_us_ = 0;
                collector_->observation_active_ = true;
            }
        }

        ObservationScope(const ObservationScope&) = delete;
        ObservationScope& operator=(const ObservationScope&) = delete;
        ObservationScope(ObservationScope&& other) noexcept
            : collector_(other.collector_), start_(other.start_), previous_(other.previous_), active_(other.active_) {
            other.collector_ = nullptr;
            other.active_ = false;
        }
        ObservationScope& operator=(ObservationScope&&) = delete;

        ~ObservationScope() { finish(); }

        void finish() noexcept {
            if (!active_) {
                return;
            }
            active_ = false;
            if (collector_ == nullptr) {
                return;
            }
            const auto elapsed = elapsed_us(start_, Clock::now());
            collector_->snapshot_.observation_total_us += elapsed;
            ++collector_->snapshot_.counters.observations;
            auto& other = collector_->snapshot_.observation_timing[
                static_cast<std::size_t>(PerformanceAuditBucket::Other)];
            const auto named = collector_->named_observation_time_us_;
            other.total_us += elapsed >= named ? elapsed - named : 0;
            ++other.calls;
            collector_->observation_active_ = previous_;
        }

    private:
        static std::uint64_t elapsed_us(const Clock::time_point start,
                                        const Clock::time_point end) noexcept {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
        }

        PerformanceAuditCollector* collector_ = nullptr;
        Clock::time_point start_{};
        bool previous_ = false;
        bool active_ = true;
    };

    Scope scope(PerformanceAuditBucket bucket) noexcept { return Scope(this, bucket); }
    AuxiliaryScope auxiliary_scope(PerformanceAuditAuxiliaryBucket bucket) noexcept {
        return AuxiliaryScope(this, bucket);
    }
    SetupScope setup_scope(PerformanceAuditSetupBucket bucket) noexcept {
        return SetupScope(this, bucket);
    }
    ObservationScope observation_scope() noexcept { return ObservationScope(this); }

    void record_query_field_call(bool observation_path) noexcept {
        ++snapshot_.counters.query_field_calls;
        if (observation_path) {
            ++snapshot_.detail_counters.observation_query_field_calls;
        } else {
            ++snapshot_.detail_counters.public_state_hash_query_field_calls;
        }
    }

    void record_query_location_call() noexcept { ++snapshot_.counters.query_location_calls; }
    void record_query_individual_call() noexcept { ++snapshot_.counters.query_individual_calls; }
    void record_query_decode() noexcept { ++snapshot_.detail_counters.query_decode_calls; }
    void record_zone_projection() noexcept { ++snapshot_.detail_counters.zone_projection_calls; }

    void record_entity(SemanticZone zone, bool identity_known) noexcept {
        ++snapshot_.counters.entities_projected;
        if (identity_known) {
            ++snapshot_.counters.identity_known_entities;
        } else {
            ++snapshot_.counters.redacted_entities;
        }
        const auto index = static_cast<std::size_t>(zone);
        if (index < snapshot_.entities_by_zone.size()) {
            auto& counters = snapshot_.entities_by_zone[index];
            ++counters.entities_projected;
            if (identity_known) {
                ++counters.identity_known;
            } else {
                ++counters.redacted;
            }
        }
    }

    void record_static_card_data_lookup() noexcept { ++snapshot_.counters.static_card_data_lookups; }
    void record_current_property_projection() noexcept { ++snapshot_.counters.current_property_projections; }
    void record_relationship_resolution() noexcept {
        ++snapshot_.detail_counters.relationship_resolution_events;
    }
    void record_relationship_object() noexcept { ++snapshot_.counters.relationship_objects; }
    void record_copy_event() noexcept { ++snapshot_.counters.allocation_copy_events; }

    void set_script_metrics(const std::uint64_t script_loads,
                            const std::uint64_t script_reader_requests) noexcept {
        snapshot_.counters.script_loads = script_loads;
        snapshot_.detail_counters.script_reader_requests = script_reader_requests;
    }

    PerformanceAuditSnapshot snapshot() const noexcept { return snapshot_; }

    static std::string_view bucket_name(const PerformanceAuditBucket bucket) noexcept {
        constexpr std::array<std::string_view, static_cast<std::size_t>(PerformanceAuditBucket::Count)>
            names = {"observation_query_field", "observation_query_location",
                     "observation_query_individual", "observation_query_decode",
                     "observation_zone_projection", "observation_entity_projection",
                     "observation_relationship_projection", "observation_visibility_privacy",
                     "observation_candidate_consistency", "observation_canonical_serialization",
                     "observation_hash", "observation_other"};
        const auto index = static_cast<std::size_t>(bucket);
        return index < names.size() ? names[index] : "unknown";
    }

    static std::string_view auxiliary_bucket_name(const PerformanceAuditAuxiliaryBucket bucket) noexcept {
        constexpr std::array<std::string_view,
                             static_cast<std::size_t>(PerformanceAuditAuxiliaryBucket::Count)>
            names = {"public_state_hash", "public_state_hash_query_field"};
        const auto index = static_cast<std::size_t>(bucket);
        return index < names.size() ? names[index] : "unknown";
    }

    static std::string_view setup_bucket_name(const PerformanceAuditSetupBucket bucket) noexcept {
        constexpr std::array<std::string_view, static_cast<std::size_t>(PerformanceAuditSetupBucket::Count)>
            names = {"core_host_setup", "fixture_script_load", "script_load"};
        const auto index = static_cast<std::size_t>(bucket);
        return index < names.size() ? names[index] : "unknown";
    }

private:
    friend class Scope;
    friend class AuxiliaryScope;
    friend class ObservationScope;
    friend class SetupScope;

    void record_timing(const PerformanceAuditBucket bucket, const std::uint64_t exclusive_us) noexcept {
        const auto index = static_cast<std::size_t>(bucket);
        if (index >= snapshot_.observation_timing.size()) {
            return;
        }
        auto& timing = snapshot_.observation_timing[index];
        timing.total_us += exclusive_us;
        ++timing.calls;
        if (bucket != PerformanceAuditBucket::Other && observation_active_) {
            named_observation_time_us_ += exclusive_us;
        }
    }

    void record_auxiliary_timing(const PerformanceAuditAuxiliaryBucket bucket,
                                 const std::uint64_t exclusive_us) noexcept {
        const auto index = static_cast<std::size_t>(bucket);
        if (index >= snapshot_.auxiliary_timing.size()) {
            return;
        }
        auto& timing = snapshot_.auxiliary_timing[index];
        timing.total_us += exclusive_us;
        ++timing.calls;
    }

    void record_setup_timing(const PerformanceAuditSetupBucket bucket,
                             const std::uint64_t elapsed_us) noexcept {
        const auto index = static_cast<std::size_t>(bucket);
        if (index >= snapshot_.setup_timing.size()) {
            return;
        }
        auto& timing = snapshot_.setup_timing[index];
        timing.total_us += elapsed_us;
        ++timing.calls;
    }

    PerformanceAuditSnapshot snapshot_;
    std::uint64_t named_observation_time_us_ = 0;
    bool observation_active_ = false;
    Scope* active_scope_ = nullptr;
    AuxiliaryScope* active_auxiliary_scope_ = nullptr;
    SetupScope* active_setup_scope_ = nullptr;
};

}  // namespace ygo::observation
#endif
