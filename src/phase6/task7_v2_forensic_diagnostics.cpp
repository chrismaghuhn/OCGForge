#include "ygo/phase6/task7_v2_forensic_diagnostics.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <set>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "ygo/trace/sha256.hpp"
#include "ygo/diagnostics/task7_localization_analysis.hpp"

#if defined(_WIN32)
#include <Windows.h>
#include <Psapi.h>
#ifdef max
#undef max
#endif
#endif

namespace ygo::phase6 {
namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t elapsed_us(const Clock::time_point start, const Clock::time_point end) noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

std::string json_escape(const std::string_view value) {
    std::ostringstream output;
    output << '"';
    for (const unsigned char character : value) {
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                output << "\\u00" << hex[character >> 4] << hex[character & 0xf];
            } else {
                output << static_cast<char>(character);
            }
            break;
        }
    }
    output << '"';
    return output.str();
}

std::string diagnostic_event_json(const diagnostics::Task7DiagnosticEvent& event) {
    std::ostringstream output;
    output << "{\"phase\":" << json_escape(event.phase)
           << ",\"decision_index\":" << event.decision_index
           << ",\"engine_step_index\":" << event.engine_step_index
           << ",\"engine_process_count\":" << event.engine_process_count
           << ",\"semantic_action_count\":" << event.semantic_action_count
           << ",\"acting_player\":" << static_cast<unsigned>(event.acting_player)
           << ",\"request_kind\":" << json_escape(event.request_kind)
           << ",\"candidate_count\":" << event.candidate_count
           << ",\"public_candidate_domain_digest\":"
           << json_escape(event.public_candidate_domain_digest)
           << ",\"public_current_state_fingerprint\":"
           << json_escape(event.public_current_state_fingerprint)
           << ",\"public_semantic_decision_id\":"
           << json_escape(event.public_semantic_decision_id)
           << ",\"public_observation_digest\":"
           << json_escape(event.public_observation_digest)
           << ",\"selected_public_action_key\":"
           << json_escape(event.selected_public_action_key)
           << ",\"fallback_level\":" << static_cast<unsigned>(event.fallback_level)
           << ",\"public_turn_count_present\":"
           << (event.public_turn_count_present ? "true" : "false")
           << ",\"public_turn_count\":" << event.public_turn_count
           << ",\"public_turn_player\":"
           << static_cast<unsigned>(event.public_turn_player)
           << ",\"public_phase\":" << json_escape(event.public_phase)
           << ",\"public_life_points_present\":"
           << (event.public_life_points_present ? "true" : "false")
           << ",\"public_life_points_p0\":" << event.public_life_points_p0
           << ",\"public_life_points_p1\":" << event.public_life_points_p1
           << ",\"public_entity_count\":" << event.public_entity_count
           << ",\"public_visible_event_count\":"
           << event.public_visible_event_count
           << ",\"public_chain_length\":" << event.public_chain_length
           << ",\"continuation_present\":"
           << (event.continuation_present ? "true" : "false")
           << ",\"continuation_kind\":" << json_escape(event.continuation_kind)
           << ",\"continuation_step\":" << event.continuation_step
           << ",\"supported_evaluations\":" << event.supported_evaluations
           << ",\"not_applicable_evaluations\":"
           << event.not_applicable_evaluations
           << ",\"unsupported_evaluations\":" << event.unsupported_evaluations
           << ",\"invalid_evaluations\":" << event.invalid_evaluations
           << "}\n";
    return output.str();
}

std::string placement_name(const environment::SeatAssignment placement) {
    switch (placement) {
    case environment::SeatAssignment::Normal: return "NORMAL";
    case environment::SeatAssignment::Mirror: return "MIRROR";
    }
    return "UNKNOWN";
}

std::string closure_name(const policy::TeacherRunnerV3TrajectoryRunResult& run) {
    if (!run.envelope.has_value()) return "ABSENT";
    if (std::holds_alternative<trajectory::TerminalClosureV2>(run.envelope->closure)) {
        return "TERMINAL";
    }
    if (std::holds_alternative<trajectory::InterruptedClosureV2>(run.envelope->closure)) {
        return "INTERRUPTED";
    }
    if (std::holds_alternative<trajectory::FailedClosureV2>(run.envelope->closure)) {
        return "FAILED";
    }
    return "UNKNOWN";
}

std::string disposition_name(const policy::TeacherRunnerV3TrajectoryRunResult& run) {
    if (!run.envelope.has_value()) return "ABSENT";
    return run.envelope->manifest.collection_disposition.kind ==
                   trajectory::CollectionDispositionKind::Clean
               ? "CLEAN"
               : "QUARANTINED_AFTER_POLICY_REJECTION";
}

std::string now_iso_utc() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    return std::to_string(milliseconds);
}

struct ProcessSnapshot final {
    std::string cpu_seconds = "NOT_AVAILABLE";
    std::string working_set_bytes = "NOT_AVAILABLE";
    std::string private_bytes = "NOT_AVAILABLE";
    std::string io_read_bytes = "NOT_AVAILABLE";
    std::string io_write_bytes = "NOT_AVAILABLE";
};

ProcessSnapshot process_snapshot() noexcept {
#if defined(_WIN32)
    ProcessSnapshot result;
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user) != 0) {
        ULARGE_INTEGER kernel_value{};
        kernel_value.LowPart = kernel.dwLowDateTime;
        kernel_value.HighPart = kernel.dwHighDateTime;
        ULARGE_INTEGER user_value{};
        user_value.LowPart = user.dwLowDateTime;
        user_value.HighPart = user.dwHighDateTime;
        result.cpu_seconds = std::to_string(
            static_cast<double>(kernel_value.QuadPart + user_value.QuadPart) / 10000000.0);
    }
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                             sizeof(memory)) != 0) {
        result.working_set_bytes = std::to_string(memory.WorkingSetSize);
        result.private_bytes = std::to_string(memory.PrivateUsage);
    }
    IO_COUNTERS io{};
    if (GetProcessIoCounters(GetCurrentProcess(), &io) != 0) {
        result.io_read_bytes = std::to_string(io.ReadTransferCount);
        result.io_write_bytes = std::to_string(io.WriteTransferCount);
    }
    return result;
#else
    return {};
#endif
}

std::string cpu_delta_seconds(const ProcessSnapshot& start,
                              const ProcessSnapshot& end) {
    if (start.cpu_seconds == "NOT_AVAILABLE" || end.cpu_seconds == "NOT_AVAILABLE") {
        return "NOT_AVAILABLE";
    }
    try {
        const auto delta = std::stod(end.cpu_seconds) - std::stod(start.cpu_seconds);
        return std::to_string(delta < 0.0 ? 0.0 : delta);
    } catch (...) {
        return "NOT_AVAILABLE";
    }
}

std::string executable_sha256() {
#if defined(_WIN32)
    std::array<wchar_t, 32768> buffer{};
    const auto length = GetModuleFileNameW(nullptr, buffer.data(),
                                           static_cast<DWORD>(buffer.size()));
    if (length == 0 || length == buffer.size()) return "NOT_AVAILABLE";
    std::ifstream input(std::filesystem::path(std::wstring(buffer.data(), length)),
                        std::ios::binary);
    if (!input) return "NOT_AVAILABLE";
    std::vector<std::uint8_t> bytes;
    std::array<char, 8192> chunk{};
    while (input) {
        input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto count = input.gcount();
        if (count > 0) {
            bytes.insert(bytes.end(), reinterpret_cast<const std::uint8_t*>(chunk.data()),
                         reinterpret_cast<const std::uint8_t*>(chunk.data()) + count);
        }
    }
    return trace::sha256_bytes(bytes);
#else
    return "NOT_AVAILABLE";
#endif
}

struct PhaseStats final {
    std::uint64_t calls = 0;
    std::uint64_t total_us = 0;
    std::uint64_t max_us = 0;
    std::uint64_t summary_calls = 0;
    std::uint64_t summary_total_us = 0;
    std::uint64_t summary_max_us = 0;
};

struct WorkloadStats final {
    std::uint64_t decisions = 0;
    std::uint64_t semantic_actions = 0;
    std::uint64_t engine_process_calls = 0;
    std::uint64_t observations = 0;
    std::uint64_t candidates_total = 0;
    std::uint64_t candidates_max = 0;
    std::uint64_t teacher_proposals = 0;
    std::uint64_t continuation_decisions = 0;
    std::uint64_t continuation_actions = 0;
    std::uint64_t step_accepted = 0;
    std::uint64_t step_rejected = 0;
    std::uint64_t supported = 0;
    std::uint64_t not_applicable = 0;
    std::uint64_t unsupported = 0;
    std::uint64_t invalid = 0;
    std::uint64_t f0 = 0;
    std::uint64_t f1 = 0;
    std::uint64_t f2 = 0;
    std::uint64_t f3 = 0;
    std::uint64_t f4 = 0;
};

struct JobPerformance final {
    std::uint64_t wall_us = 0;
    std::string cpu_seconds = "NOT_AVAILABLE";
    std::map<std::string, PhaseStats> phases;
    WorkloadStats workload;
};

class DiagnosticWriter final {
public:
    DiagnosticWriter(const Task7V2DiagnosticOptions& options,
                     const Task7CollectionScheduleV2& schedule)
        : options_(options), schedule_(schedule), start_(Clock::now()),
          source_commit_(options.source_commit), schedule_id_(
              task7_collection_schedule_identity_v2(schedule)) {
        if (std::filesystem::exists(options_.output_directory)) {
            throw std::invalid_argument("diagnostic output directory already exists");
        }
        std::filesystem::create_directories(options_.output_directory);
        progress_.open(options_.output_directory / "progress.jsonl", std::ios::out);
        jobs_.open(options_.output_directory / "jobs.jsonl", std::ios::out);
        decisions_.open(options_.output_directory / "decisions.jsonl", std::ios::out);
        if (!progress_ || !jobs_ || !decisions_) {
            throw std::runtime_error("diagnostic output files could not be opened");
        }
        emit_progress("RUN_START", std::nullopt, "START");
        heartbeat_ = std::thread([this] { heartbeat_loop(); });
    }

    ~DiagnosticWriter() {
        stop_heartbeat();
    }

    diagnostics::Task7DiagnosticObserver observer(const std::size_t job_index,
                                                   const std::string& job_id) {
        return [this, job_index, job_id](const diagnostics::Task7DiagnosticEvent& event) {
            on_event(job_index, job_id, event);
        };
    }

    void begin_job(const std::size_t index, const Task7CollectionJobV2& job) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            current_job_index_ = index;
            current_job_id_ = task7_collection_job_identity_v2(job);
            current_phase_ = "JOB_SETUP";
            job_phase_stats_.clear();
            job_workload_ = {};
            reported_phases_.clear();
            decisions_for_job_.clear();
        }
        emit_progress("JOB_START", index, "JOB_SETUP");
        std::cout << "JOB_START index=" << index << " seed=" << job.root_seed
                  << " placement=" << placement_name(job.seat_assignment)
                  << " starting_player=" << static_cast<unsigned>(job.starting_player)
                  << std::endl;
    }

    void finish_job(const std::size_t index, const Task7CollectionJobV2& job,
                    const policy::TeacherRunnerV3TrajectoryRunResult& run,
                    const Task7V2EligibilityInspection& inspection,
                    const JobPerformance& performance) {
        std::ostringstream record;
        record << "{\"job_index\":" << index
               << ",\"job_identity\":"
               << json_escape(task7_collection_job_identity_v2(job))
               << ",\"root_seed\":" << job.root_seed
               << ",\"seat_assignment\":" << json_escape(placement_name(job.seat_assignment))
               << ",\"starting_player\":" << static_cast<unsigned>(job.starting_player)
               << ",\"engine_process_budget\":" << job.engine_process_budget
               << ",\"semantic_action_budget\":" << job.semantic_action_budget
               << ",\"run_error_present\":" << (inspection.run_error_present ? "true" : "false")
               << ",\"run_error\":" << json_escape(run.error.has_value() ? run.error->message : "")
               << ",\"run_diagnostic\":" << json_escape(run.diagnostic)
               << ",\"quarantined\":" << (inspection.quarantined ? "true" : "false")
               << ",\"envelope_present\":" << (inspection.envelope_present ? "true" : "false")
               << ",\"closure_kind\":" << json_escape(closure_name(run))
               << ",\"collection_disposition\":" << json_escape(disposition_name(run))
               << ",\"replay_evidence_present\":" << (inspection.replay_evidence_present ? "true" : "false")
               << ",\"candidate_shard_present\":" << (inspection.candidate_shard_present ? "true" : "false")
               << ",\"restricted_evidence_present\":" << (inspection.restricted_collection_evidence_present ? "true" : "false")
               << ",\"admission_verification_present\":" << (inspection.admission_verification_present ? "true" : "false")
               << ",\"admission_receipt_present\":" << (inspection.admission_receipt_present ? "true" : "false")
               << ",\"dataset_manifest_present\":" << (inspection.dataset_manifest_present ? "true" : "false")
               << ",\"candidate_shard_entry_count\":" << inspection.candidate_shard_entry_count
               << ",\"admission_receipt_entry_count\":" << inspection.admission_receipt_entry_count
               << ",\"dataset_manifest_member_count\":" << inspection.dataset_manifest_member_count
               << ",\"eligible\":" << (inspection.eligible ? "true" : "false")
               << ",\"failed_conditions\":[";
        for (std::size_t condition = 0; condition < inspection.failed_conditions.size(); ++condition) {
            if (condition != 0) record << ',';
            record << json_escape(inspection.failed_conditions[condition]);
        }
        record << "]"
               << ",\"job_wall_seconds\":"
               << (static_cast<double>(performance.wall_us) / 1000000.0)
               << ",\"process_cpu_seconds\":" << json_escape(performance.cpu_seconds)
               << ",\"decisions\":" << performance.workload.decisions
               << ",\"semantic_actions\":" << performance.workload.semantic_actions
               << ",\"engine_process_calls\":" << performance.workload.engine_process_calls
               << ",\"observations\":" << performance.workload.observations
               << ",\"candidates_total\":" << performance.workload.candidates_total
               << ",\"candidates_max\":" << performance.workload.candidates_max
               << "}";
        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_ << record.str() << '\n';
            jobs_.flush();
            completed_jobs_ = std::max(completed_jobs_, index + 1);
        }
        emit_progress("JOB_END", index, inspection.eligible ? "ELIGIBLE" : "INELIGIBLE");
        write_semantic_analysis();
        std::cout << "JOB_END index=" << index << " eligible="
                  << (inspection.eligible ? "YES" : "NO")
                  << " closure=" << closure_name(run)
                  << " failed_conditions=" << inspection.failed_conditions.size()
                  << std::endl;
    }

    void first_failure(const std::size_t index,
                       const Task7CollectionJobV2& job,
                       const Task7V2EligibilityInspection& inspection,
                       const policy::TeacherRunnerV3TrajectoryRunResult& run) {
        if (first_failure_index_.has_value()) return;
        first_failure_index_ = index;
        first_failure_id_ = task7_collection_job_identity_v2(job);
        std::ofstream output(options_.output_directory / "failed-job.json",
                             std::ios::out | std::ios::trunc);
        output << "{\"job_index\":" << index
               << ",\"job_identity\":" << json_escape(*first_failure_id_)
               << ",\"root_seed\":" << job.root_seed
               << ",\"seat_assignment\":" << json_escape(placement_name(job.seat_assignment))
               << ",\"starting_player\":" << static_cast<unsigned>(job.starting_player)
               << ",\"closure_kind\":" << json_escape(closure_name(run))
               << ",\"collection_disposition\":" << json_escape(disposition_name(run))
               << ",\"run_diagnostic\":" << json_escape(run.diagnostic)
               << ",\"failed_conditions\":[";
        for (std::size_t condition = 0; condition < inspection.failed_conditions.size(); ++condition) {
            if (condition != 0) output << ',';
            output << json_escape(inspection.failed_conditions[condition]);
        }
        output << "]}\n";
        emit_progress("FIRST_FAILURE", index, inspection.first_failed_condition);
        std::cout << "FIRST_FAILURE index=" << index
                  << " condition=" << inspection.first_failed_condition << std::endl;
    }

    void finish_run(const Task7V2DiagnosticRunResult& result,
                    const std::size_t jobs_executed,
                    const std::size_t eligible_jobs,
                    const std::size_t ineligible_jobs,
                    const ProcessSnapshot& start_process,
                    const ProcessSnapshot& end_process,
                    const std::uint64_t wall_us) {
        stop_heartbeat();
        write_perf_summary();
        std::ofstream run(options_.output_directory / "run.json",
                          std::ios::out | std::ios::trunc);
        run << "{\"diagnostic_schema_id\":"
            << json_escape(diagnostics::kTask7ForensicDiagnosticsSchemaId)
            << ",\"source_commit\":" << json_escape(source_commit_)
            << ",\"build_type\":" << json_escape(build_type())
            << ",\"compiler\":" << json_escape(compiler_identity())
            << ",\"diagnostic_executable_sha256\":" << json_escape(executable_sha256())
            << ",\"task7_schedule_id\":" << json_escape(schedule_id_)
            << ",\"rules_bundle_id\":" << json_escape(schedule_.jobs.front().rules_bundle_id)
            << ",\"matchup_id\":" << json_escape(schedule_.jobs.front().matchup_id)
            << ",\"job_count\":" << schedule_.jobs.size()
            << ",\"jobs_executed\":" << jobs_executed
            << ",\"eligible_jobs\":" << eligible_jobs
            << ",\"ineligible_jobs\":" << ineligible_jobs
            << ",\"overall_result\":" << json_escape(result.completed ? "COMPLETED" : "FAILED")
            << ",\"total_wall_seconds\":" << (static_cast<double>(wall_us) / 1000000.0)
            << ",\"start_process_cpu_seconds\":" << json_escape(start_process.cpu_seconds)
            << ",\"end_process_cpu_seconds\":" << json_escape(end_process.cpu_seconds)
            << ",\"start_working_set_bytes\":" << json_escape(start_process.working_set_bytes)
            << ",\"end_working_set_bytes\":" << json_escape(end_process.working_set_bytes)
            << ",\"start_io_read_bytes\":" << json_escape(start_process.io_read_bytes)
            << ",\"end_io_read_bytes\":" << json_escape(end_process.io_read_bytes)
            << ",\"start_io_write_bytes\":" << json_escape(start_process.io_write_bytes)
            << ",\"end_io_write_bytes\":" << json_escape(end_process.io_write_bytes)
            << ",\"error\":" << json_escape(result.error) << "}\n";
        emit_progress("RUN_END", std::nullopt, result.completed ? "COMPLETED" : "FAILED");
        std::cout << "RUN_END jobs_executed=" << jobs_executed
                  << " eligible=" << eligible_jobs
                  << " ineligible=" << ineligible_jobs << std::endl;
    }

    void on_event(const std::size_t job_index, const std::string& job_id,
                  const diagnostics::Task7DiagnosticEvent& event) {
        bool report_phase = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            current_job_index_ = job_index;
            current_job_id_ = job_id;
            current_phase_ = event.phase;
            report_phase = reported_phases_.insert(event.phase).second;
            const auto update = [&event](std::map<std::string, PhaseStats>& phases,
                                         WorkloadStats& workload) {
                auto phase = event.phase;
                const std::string summary_suffix = "_SUMMARY";
                const auto summary = phase.size() >= summary_suffix.size() &&
                                     phase.compare(phase.size() - summary_suffix.size(),
                                                   summary_suffix.size(), summary_suffix) == 0;
                if (summary) phase.resize(phase.size() - summary_suffix.size());
                auto& stats = phases[phase];
                if (summary) {
                    stats.summary_calls = event.call_count;
                    stats.summary_total_us = event.duration_us;
                    stats.summary_max_us = event.max_single_call_us;
                } else {
                    ++stats.calls;
                    stats.total_us = stats.total_us + event.duration_us;
                    stats.max_us = (std::max)(stats.max_us, event.max_single_call_us == 0
                                                                   ? event.duration_us
                                                                   : event.max_single_call_us);
                }
                workload.engine_process_calls =
                    (std::max)(workload.engine_process_calls, event.engine_process_count);
                workload.semantic_actions =
                    (std::max)(workload.semantic_actions, event.semantic_action_count);
                workload.observations =
                    (std::max)(workload.observations, event.observation_count);
                workload.candidates_total =
                    (std::max)(workload.candidates_total, event.candidate_total);
                workload.candidates_max =
                    (std::max)(workload.candidates_max, event.candidate_max);
                if (event.phase == "TEACHER") {
                    ++workload.teacher_proposals;
                    ++workload.decisions;
                    workload.supported += event.supported_evaluations;
                    workload.not_applicable += event.not_applicable_evaluations;
                    workload.unsupported += event.unsupported_evaluations;
                    workload.invalid += event.invalid_evaluations;
                    workload.f0 += event.f0_count;
                    workload.f1 += event.f1_count;
                    workload.f2 += event.f2_count;
                    workload.f3 += event.f3_count;
                    workload.f4 += event.f4_count;
                } else if (event.phase == "TEACHER_COMMIT") {
                    ++workload.step_accepted;
                } else if (event.phase == "STEP_REJECTED") {
                    ++workload.step_rejected;
                }
            };
            update(phase_stats_, workload_);
            update(job_phase_stats_, job_workload_);
            if (event.phase == "TEACHER") {
                decisions_for_job_.push_back(event);
                decisions_ << diagnostic_event_json(event);
                decisions_.flush();
            }
        }
        if (report_phase) {
            emit_progress("PHASE_CHANGE", job_index, event.phase);
        }
    }

    JobPerformance performance() const {
        std::lock_guard<std::mutex> lock(mutex_);
        JobPerformance result;
        result.phases = job_phase_stats_;
        result.workload = job_workload_;
        return result;
    }

    std::optional<std::size_t> first_failure_index() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return first_failure_index_;
    }

private:
    static std::string build_type() {
#ifdef YGO_M0_BUILD_TYPE
        return YGO_M0_BUILD_TYPE;
#else
        return "unknown";
#endif
    }

    static std::string compiler_identity() {
#ifdef YGO_M0_COMPILER_ID
        return YGO_M0_COMPILER_ID;
#else
        return "unknown";
#endif
    }

    void emit_progress(const std::string_view event, const std::optional<std::size_t> job,
                       const std::string_view phase) {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_ << "{\"event\":" << json_escape(event)
                  << ",\"timestamp_ms\":" << now_iso_utc()
                  << ",\"phase\":" << json_escape(phase);
        if (job.has_value()) progress_ << ",\"job_index\":" << *job;
        if (current_job_id_.has_value()) {
            progress_ << ",\"job_identity\":" << json_escape(*current_job_id_);
        }
        progress_ << ",\"completed_jobs\":" << completed_jobs_ << "}\n";
        progress_.flush();
    }

    void heartbeat_loop() {
        const auto interval = std::chrono::seconds(
            std::max<std::uint64_t>(1, options_.heartbeat_seconds));
        while (!stop_heartbeat_.load()) {
            std::this_thread::sleep_for(interval);
            if (stop_heartbeat_.load()) break;
            std::optional<std::size_t> job;
            std::optional<std::string> id;
            std::string phase;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                job = current_job_index_;
                id = current_job_id_;
                phase = current_phase_;
            }
            emit_progress("HEARTBEAT", job, phase);
            std::cout << "HEARTBEAT";
            if (job.has_value()) std::cout << " job_index=" << *job;
            std::cout << " phase=" << phase << std::endl;
        }
    }

    void stop_heartbeat() {
        if (stop_heartbeat_.exchange(true)) return;
        if (heartbeat_.joinable()) heartbeat_.join();
    }

    static void write_count_map(std::ostream& output,
                                const std::map<std::string, std::uint64_t>& values) {
        output << '{';
        bool first = true;
        for (const auto& [key, value] : values) {
            if (!first) output << ',';
            first = false;
            output << json_escape(key) << ':' << value;
        }
        output << '}';
    }

    void write_semantic_analysis() {
        std::vector<diagnostics::Task7DiagnosticEvent> decisions;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            decisions = decisions_for_job_;
        }
        if (decisions.empty()) return;
        const auto semantic_analysis =
            diagnostics::analyze_task7_localization(decisions);

        const auto top_turns = [&semantic_analysis] {
            std::vector<std::pair<std::string, std::uint64_t>> result(
                semantic_analysis.turn_counts.begin(), semantic_analysis.turn_counts.end());
            std::sort(result.begin(), result.end(),
                      [](const auto& left, const auto& right) {
                          if (left.second != right.second) return left.second > right.second;
                          return left.first < right.first;
                      });
            if (result.size() > 10) result.resize(10);
            return result;
        }();
        std::uint64_t total_engine_processes = 0;
        std::uint64_t total_semantic_actions = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            total_engine_processes = workload_.engine_process_calls;
            total_semantic_actions = workload_.semantic_actions;
        }
        std::ofstream analysis(options_.output_directory / "semantic-analysis.json",
                               std::ios::out | std::ios::trunc);
        analysis << "{\"diagnostic_schema_id\":"
                 << json_escape(diagnostics::kTask7ForensicDiagnosticsSchemaId)
                  << ",\"total_decisions\":" << semantic_analysis.total_decisions
                  << ",\"total_engine_processes\":" << total_engine_processes
                  << ",\"total_semantic_actions\":" << total_semantic_actions
                  << ",\"total_turns_observed\":" << semantic_analysis.total_turns_observed
                  << ",\"distinct_public_observation_digests\":"
                  << semantic_analysis.distinct_public_observation_digests
                  << ",\"distinct_public_semantic_decision_ids\":"
                  << semantic_analysis.distinct_public_semantic_decision_ids
                  << ",\"distinct_current_public_state_fingerprints\":"
                  << semantic_analysis.distinct_current_public_state_fingerprints
                  << ",\"distinct_selected_public_action_keys\":"
                  << semantic_analysis.distinct_selected_public_action_keys
                  << ",\"longest_identical_public_observation_streak\":"
                  << semantic_analysis.longest_identical_public_observation_streak
                  << ",\"longest_identical_current_public_state_streak\":"
                  << semantic_analysis.longest_identical_current_public_state_streak
                  << ",\"longest_same_turn_phase_streak\":"
                  << semantic_analysis.longest_same_turn_phase_streak
                  << ",\"max_decisions_without_structural_progress\":"
                  << semantic_analysis.max_decisions_without_structural_progress
                  << ",\"max_engine_processes_without_structural_progress\":"
                  << semantic_analysis.max_engine_processes_without_structural_progress
                  << ",\"max_structural_gap_start_decision\":"
                  << semantic_analysis.max_structural_gap_start_decision
                  << ",\"max_structural_gap_end_decision\":"
                  << semantic_analysis.max_structural_gap_end_decision
                  << ",\"longest_repeated_selected_action_streak\":"
                  << semantic_analysis.longest_selected_action_streak
                  << ",\"longest_repeated_cycle_length\":"
                  << semantic_analysis.longest_repeated_cycle.cycle_length
                  << ",\"longest_repeated_cycle_count\":"
                  << semantic_analysis.longest_repeated_cycle.repeat_count
                  << ",\"cycle_start_decision\":"
                  << (semantic_analysis.longest_repeated_cycle.found
                          ? static_cast<long long>(decisions[semantic_analysis.longest_repeated_cycle
                                                                 .start_index]
                                                       .decision_index)
                          : -1LL)
                  << ",\"cycle_end_decision\":"
                  << (semantic_analysis.longest_repeated_cycle.found
                          ? static_cast<long long>(decisions[semantic_analysis.longest_repeated_cycle
                                                                 .end_index]
                                                       .decision_index)
                          : -1LL)
                  << ",\"public_observation_changed_count\":"
                  << semantic_analysis.public_observation_changed_count
                  << ",\"public_observation_unchanged_count\":"
                  << semantic_analysis.public_observation_unchanged_count
                  << ",\"current_public_state_changed_count\":"
                  << semantic_analysis.current_public_state_changed_count
                  << ",\"current_public_state_unchanged_count\":"
                  << semantic_analysis.current_public_state_unchanged_count
                  << ",\"f4_longest_consecutive_streak\":"
                  << semantic_analysis.f4_longest_consecutive_streak
                  << ",\"f4_to_f4_transitions\":"
                  << semantic_analysis.f4_to_f4_transitions
                  << ",\"f0_to_f4_transitions\":"
                  << semantic_analysis.f0_to_f4_transitions
                  << ",\"f4_to_f0_transitions\":"
                  << semantic_analysis.f4_to_f0_transitions
                  << ",\"f4_with_unchanged_public_observation_count\":"
                  << semantic_analysis.f4_with_unchanged_public_observation_count
                  << ",\"f4_with_changed_public_observation_count\":"
                  << semantic_analysis.f4_with_changed_public_observation_count
                  << ",\"f4_with_unchanged_current_state_count\":"
                  << semantic_analysis.f4_with_unchanged_current_state_count
                  << ",\"f4_with_changed_current_state_count\":"
                  << semantic_analysis.f4_with_changed_current_state_count
                  << ",\"fallback_level_counts\":";
        write_count_map(analysis, semantic_analysis.fallback_level_counts);
        analysis << ",\"request_family_counts\":";
        write_count_map(analysis, semantic_analysis.request_counts);
        analysis << ",\"selected_action_key_counts\":";
        write_count_map(analysis, semantic_analysis.selected_action_counts);
        analysis << ",\"fallback_transition_counts\":";
        write_count_map(analysis, semantic_analysis.fallback_transition_counts);
        analysis << ",\"request_family_transition_counts\":";
        write_count_map(analysis, semantic_analysis.request_transition_counts);
        analysis << ",\"top_turns_by_decision_count\":[";
        for (std::size_t index = 0; index < top_turns.size(); ++index) {
            if (index != 0) analysis << ',';
            analysis << "{\"turn\":" << json_escape(top_turns[index].first)
                     << ",\"decisions\":" << top_turns[index].second << '}';
        }
        analysis << "],\"cycle_tuple_fields\":[\"acting_player\",\"request_kind\","
                    "\"public_current_state_fingerprint\",\"public_candidate_domain_digest\","
                    "\"selected_public_action_key\",\"continuation_present\","
                    "\"continuation_kind\",\"continuation_step\"]}\n";

        std::ofstream tail(options_.output_directory / "semantic-tail-256.jsonl",
                           std::ios::out | std::ios::trunc);
        for (const auto& decision :
             diagnostics::extract_task7_diagnostic_tail(decisions, 256)) {
            tail << diagnostic_event_json(decision);
        }
        std::ofstream cycle(options_.output_directory / "first-cycle.json",
                            std::ios::out | std::ios::trunc);
        cycle << "{\"cycle_found\":"
              << (semantic_analysis.longest_repeated_cycle.found ? "true" : "false");
        if (semantic_analysis.longest_repeated_cycle.found) {
            cycle << ",\"cycle_start_decision\":"
                  << decisions[semantic_analysis.longest_repeated_cycle.start_index].decision_index
                  << ",\"cycle_end_decision\":"
                  << decisions[semantic_analysis.longest_repeated_cycle.end_index].decision_index
                  << ",\"cycle_length\":"
                  << semantic_analysis.longest_repeated_cycle.cycle_length
                  << ",\"repeat_count\":"
                  << semantic_analysis.longest_repeated_cycle.repeat_count;
        }
        cycle << "}\n";
    }

    void write_perf_summary() {
        std::map<std::string, PhaseStats> phases;
        WorkloadStats workload;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            phases = phase_stats_;
            workload = workload_;
        }
        std::vector<std::pair<std::string, std::uint64_t>> totals;
        std::vector<std::pair<std::string, std::uint64_t>> maxima;
        for (const auto& [name, stats] : phases) {
            totals.push_back({name, stats.summary_calls != 0 ? stats.summary_total_us
                                                               : stats.total_us});
            maxima.push_back({name, stats.summary_calls != 0 ? stats.summary_max_us
                                                               : stats.max_us});
        }
        std::sort(totals.begin(), totals.end(),
                  [](const auto& left, const auto& right) { return left.second > right.second; });
        std::sort(maxima.begin(), maxima.end(),
                  [](const auto& left, const auto& right) { return left.second > right.second; });
        std::ofstream output(options_.output_directory / "perf-summary.json",
                             std::ios::out | std::ios::trunc);
        output << "{\"diagnostic_schema_id\":"
               << json_escape(diagnostics::kTask7ForensicDiagnosticsSchemaId)
               << ",\"phases\":{";
        bool first = true;
        for (const auto& [name, stats] : phases) {
            if (!first) output << ',';
            first = false;
            output << json_escape(name) << ":{\"calls\":"
                   << (stats.summary_calls != 0 ? stats.summary_calls : stats.calls)
                   << ",\"total_us\":"
                   << (stats.summary_calls != 0 ? stats.summary_total_us : stats.total_us)
                   << ",\"max_single_call_us\":"
                   << (stats.summary_calls != 0 ? stats.summary_max_us : stats.max_us)
                   << "}";
        }
        output << "},\"top_total_self_time\":[";
        for (std::size_t index = 0; index < std::min<std::size_t>(10, totals.size()); ++index) {
            if (index != 0) output << ',';
            output << "{\"phase\":" << json_escape(totals[index].first)
                   << ",\"microseconds\":" << totals[index].second << "}";
        }
        output << "],\"top_max_latency\":[";
        for (std::size_t index = 0; index < std::min<std::size_t>(10, maxima.size()); ++index) {
            if (index != 0) output << ',';
            output << "{\"phase\":" << json_escape(maxima[index].first)
                   << ",\"microseconds\":" << maxima[index].second << "}";
        }
        output << "],\"workload\":{\"decisions\":" << workload.decisions
               << ",\"semantic_actions\":" << workload.semantic_actions
               << ",\"engine_process_calls\":" << workload.engine_process_calls
               << ",\"observations\":" << workload.observations
               << ",\"candidates_total\":" << workload.candidates_total
               << ",\"candidates_max\":" << workload.candidates_max
               << ",\"teacher_proposals\":" << workload.teacher_proposals
               << ",\"f0\":" << workload.f0 << ",\"f1\":" << workload.f1
               << ",\"f2\":" << workload.f2 << ",\"f3\":" << workload.f3
               << ",\"f4\":" << workload.f4
               << ",\"step_accepted\":" << workload.step_accepted
               << ",\"step_rejected\":" << workload.step_rejected << "}}\n";
    }

    const Task7V2DiagnosticOptions& options_;
    const Task7CollectionScheduleV2& schedule_;
    const Clock::time_point start_;
    const std::string source_commit_;
    const std::string schedule_id_;
    std::ofstream progress_;
    std::ofstream jobs_;
    std::ofstream decisions_;
    mutable std::mutex mutex_;
    std::map<std::string, PhaseStats> phase_stats_;
    std::map<std::string, PhaseStats> job_phase_stats_;
    WorkloadStats workload_;
    WorkloadStats job_workload_;
    std::optional<std::size_t> current_job_index_;
    std::optional<std::string> current_job_id_;
    std::string current_phase_ = "START";
    std::set<std::string> reported_phases_;
    std::vector<diagnostics::Task7DiagnosticEvent> decisions_for_job_;
    std::optional<std::size_t> first_failure_index_;
    std::optional<std::string> first_failure_id_;
    std::size_t completed_jobs_ = 0;
    std::atomic<bool> stop_heartbeat_{false};
    std::thread heartbeat_;
};

JobPerformance run_job(const Task7CollectionJobV2& job, const std::size_t index,
                       DiagnosticWriter& writer,
                       policy::TeacherRunnerV3TrajectoryRunResult& run,
                       Task7V2EligibilityInspection& inspection) {
    writer.begin_job(index, job);
    const auto start = Clock::now();
    const auto start_process = process_snapshot();
    run = run_task7_collection_job_v2(
        job, writer.observer(index, task7_collection_job_identity_v2(job)));
    const auto eligibility_start = Clock::now();
    inspection = inspect_task7_v2_job_run(job, run);
    diagnostics::Task7DiagnosticEvent eligibility_event;
    eligibility_event.phase = "TASK7_ELIGIBILITY";
    eligibility_event.duration_us = elapsed_us(eligibility_start, Clock::now());
    writer.on_event(index, task7_collection_job_identity_v2(job), eligibility_event);
    const auto end = Clock::now();
    const auto end_process = process_snapshot();
    auto performance = writer.performance();
    performance.wall_us = elapsed_us(start, end);
    performance.cpu_seconds = cpu_delta_seconds(start_process, end_process);
    writer.finish_job(index, job, run, inspection, performance);
    if (!inspection.eligible) writer.first_failure(index, job, inspection, run);
    return performance;
}

}  // namespace

Task7V2DiagnosticRunResult run_task7_v2_forensic_diagnostic(
    const Task7V2DiagnosticOptions& options) noexcept {
    Task7V2DiagnosticRunResult result;
    try {
        if (options.output_directory.empty()) {
            result.error = "diagnostic output directory is required";
            return result;
        }
        const auto schedule = make_task7_collection_schedule_v2(options.source_commit);
        if (options.mode == Task7V2DiagnosticMode::SingleJob &&
            options.job_index >= schedule.jobs.size()) {
            result.error = "job index is outside the frozen schedule";
            return result;
        }
        DiagnosticWriter writer(options, schedule);
        const auto start_process = process_snapshot();
        const auto run_start = Clock::now();
        const auto execute = [&](const std::size_t index) {
            policy::TeacherRunnerV3TrajectoryRunResult run;
            Task7V2EligibilityInspection inspection;
            (void)run_job(schedule.jobs[index], index, writer, run, inspection);
            ++result.jobs_executed;
            if (inspection.eligible) {
                ++result.eligible_jobs;
            } else {
                ++result.ineligible_jobs;
                if (!result.first_failed_job_index.has_value()) {
                    result.first_failed_job_index = index;
                    result.first_failed_job_id = task7_collection_job_identity_v2(
                        schedule.jobs[index]);
                }
            }
            return inspection.eligible;
        };
        if (options.mode == Task7V2DiagnosticMode::SingleJob) {
            execute(options.job_index);
        } else {
            for (std::size_t index = 0; index < schedule.jobs.size(); ++index) {
                if (!execute(index)) break;
            }
        }
        const auto run_end = Clock::now();
        const auto end_process = process_snapshot();
        const auto wall_us = elapsed_us(run_start, run_end);
        writer.finish_run(result, result.jobs_executed, result.eligible_jobs,
                          result.ineligible_jobs, start_process, end_process, wall_us);
        result.completed = true;
        return result;
    } catch (const std::exception& exception) {
        result.error = exception.what();
        return result;
    } catch (...) {
        result.error = "Task7 V2 forensic diagnostic threw";
        return result;
    }
}

}  // namespace ygo::phase6
