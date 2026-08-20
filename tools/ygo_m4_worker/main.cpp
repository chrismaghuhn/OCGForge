#include "json_protocol.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "ygo/core/rules_bundle.hpp"
#include "ygo/m3/canonical_rules.hpp"
#include "ygo/simulation/canonical_simulation.hpp"

#ifndef YGO_M3_DECK_A
#error "YGO_M3_DECK_A must be supplied by CMake"
#endif
#ifndef YGO_M3_DECK_B
#error "YGO_M3_DECK_B must be supplied by CMake"
#endif
#ifndef YGO_M3_CARDSCRIPTS
#error "YGO_M3_CARDSCRIPTS must be supplied by CMake"
#endif
#ifndef YGO_M0_CARD_DATA_TSV
#error "YGO_M0_CARD_DATA_TSV must be supplied by CMake"
#endif

namespace {

std::uint32_t process_id() {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(::GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(::getpid());
#endif
}

std::vector<std::uint32_t> required_script_codes(const ygo::core::FixtureDeck& deck_a,
                                                 const ygo::core::FixtureDeck& deck_b) {
    std::vector<std::uint32_t> codes = deck_a.main_deck;
    codes.insert(codes.end(), deck_a.extra_deck.begin(), deck_a.extra_deck.end());
    codes.insert(codes.end(), deck_b.main_deck.begin(), deck_b.main_deck.end());
    codes.insert(codes.end(), deck_b.extra_deck.begin(), deck_b.extra_deck.end());
    std::sort(codes.begin(), codes.end());
    codes.erase(std::unique(codes.begin(), codes.end()), codes.end());
    return codes;
}

ygo::simulation::CanonicalSimulationConfig build_canonical_config() {
    const auto& rules = ygo::m3::canonical_rules();
    ygo::simulation::CanonicalSimulationConfig config;
    config.rules.card_scripts_root = YGO_M3_CARDSCRIPTS;
    config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    config.rules.bundle_id = std::string(rules.rules_bundle_id);
    config.rules.core_patchset_id = std::string(rules.core_patchset_id);
    config.rules.core_patchset_sha256 = std::string(rules.core_patchset_sha256);
    config.format = std::string(rules.format_id);
    config.duel_mode = std::string(rules.duel_mode_name);
    config.duel_flags = rules.duel_flags;
    config.rules_bundle_id = std::string(rules.rules_bundle_id);
    config.patchset_id = std::string(rules.core_patchset_id);
    config.patchset_sha256 = std::string(rules.core_patchset_sha256);
    config.deck_a = ygo::core::load_fixture_deck(YGO_M3_DECK_A);
    config.deck_b = ygo::core::load_fixture_deck(YGO_M3_DECK_B);
    config.required_script_codes = required_script_codes(config.deck_a, config.deck_b);
    config.mode = ygo::simulation::SimulationMode::Throughput;
    config.observation_mode = ygo::simulation::ObservationMode::Full;
    config.instrumentation = false;
    config.persist_trace = false;
    if (!ygo::simulation::is_canonical_identity(config)) {
        throw std::runtime_error("worker startup configuration is not canonical");
    }
    return config;
}

ygo::m4::worker::WorkerReadyInfo build_worker_info() {
    ygo::m4::worker::WorkerReadyInfo info;
    info.pid = process_id();
#ifdef YGO_M0_COMPILER_ID
    info.compiler_identity = YGO_M0_COMPILER_ID;
#else
    info.compiler_identity = "unknown";
#endif
#ifdef YGO_M0_BUILD_TYPE
    info.build_type = YGO_M0_BUILD_TYPE;
#else
    info.build_type = "unknown";
#endif
    return info;
}

ygo::simulation::SimulationResult failed_request_result(const std::string& job_id,
                                                         const std::string& failure_code,
                                                         const std::string& message,
                                                         const ygo::m4::worker::WorkerReadyInfo& info) {
    ygo::simulation::SimulationResult result;
    result.job_id = job_id;
    result.failure_code = failure_code.empty() ? "invalid_request" : failure_code;
    result.error_message = message;
    result.errors.worker_errors = 1;
    result.worker_pid = info.pid;
    return result;
}

void emit_line(const std::string& line) {
    std::cout << line << '\n';
    std::cout.flush();
}

int run_worker() {
    const auto config = build_canonical_config();
    const auto worker = build_worker_info();

    // The ready line is the only startup output and is emitted before stdin
    // is read. All subsequent stdout writes are complete JSONL envelopes.
    emit_line(ygo::m4::worker::serialize_ready(worker, config));

    std::string line;
    while (std::getline(std::cin, line)) {
        const auto parsed = ygo::m4::worker::parse_job_request(line);
        if (!parsed.ok()) {
            if (parsed.recoverable_job_id.has_value()) {
                emit_line(ygo::m4::worker::serialize_result(
                    failed_request_result(*parsed.recoverable_job_id, parsed.failure_code,
                                          parsed.error_message, worker),
                    worker));
            } else {
                emit_line(ygo::m4::worker::serialize_protocol_error(parsed));
            }
            continue;
        }

        ygo::simulation::SimulationResult result;
        try {
            result = ygo::simulation::run_canonical_simulation(*parsed.job, config);
        } catch (const std::exception& error) {
            result = failed_request_result(parsed.job->job_id, "worker_exception", error.what(), worker);
        }
        if (!result.pass && result.failure_code.empty()) {
            result.failure_code = "simulation_failed";
        }
        if (!result.pass && result.failure_code == "canonical_identity_mismatch" &&
            result.errors.worker_errors == 0) {
            ++result.errors.worker_errors;
        }
        if (!result.pass && result.errors.retries == 0 && result.errors.unsupported == 0 &&
            result.errors.automatic == 0 && result.errors.truncated == 0 &&
            result.errors.core_errors == 0 && result.errors.worker_errors == 0) {
            ++result.errors.worker_errors;
        }
        result.worker_pid = worker.pid;
        result.coordinator_elapsed_us = 0;
        emit_line(ygo::m4::worker::serialize_result(result, worker));
#ifdef YGO_M4_PERFORMANCE_AUDIT
        std::cerr << ygo::m4::worker::kPerformanceAuditSidecarPrefix
                  << ygo::m4::worker::serialize_performance_audit(result.job_id, result.performance_audit)
                  << '\n';
        std::cerr.flush();
#endif
    }
    return 0;
}

}  // namespace

int main() {
    try {
        return run_worker();
    } catch (const std::exception& error) {
        // Startup failures intentionally produce no ready line and no job
        // result. The coordinator must treat this as an abnormal worker exit.
        std::cerr << "M4_WORKER_STARTUP_ERROR " << error.what() << '\n';
        return 2;
    }
}
