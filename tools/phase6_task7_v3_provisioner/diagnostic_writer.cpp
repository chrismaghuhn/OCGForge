#include "diagnostic_writer.hpp"

#include <array>
#include <exception>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <variant>

namespace ygo::phase6::tooling {
namespace {

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
                output << "\\u00" << hex[character >> 4]
                       << hex[character & 0x0f];
            } else {
                output << static_cast<char>(character);
            }
            break;
        }
    }
    output << '"';
    return output.str();
}

std::string placement_name(const environment::SeatAssignment placement) {
    switch (placement) {
    case environment::SeatAssignment::Normal: return "NORMAL";
    case environment::SeatAssignment::Mirror: return "MIRROR";
    }
    return "UNKNOWN";
}

std::string closure_name(
    const policy::TeacherRunnerV4TrajectoryRunResult& run) {
    if (!run.envelope.has_value()) return "ABSENT";
    if (std::holds_alternative<trajectory::TerminalClosureV3>(
            run.envelope->closure)) {
        return "TERMINAL";
    }
    if (std::holds_alternative<trajectory::InterruptedClosureV3>(
            run.envelope->closure)) {
        return "INTERRUPTED";
    }
    if (std::holds_alternative<trajectory::FailedClosureV3>(
            run.envelope->closure)) {
        return "FAILED";
    }
    return "UNKNOWN";
}

void write_optional_u64(std::ostream& output, const bool present,
                        const std::uint64_t value) {
    if (present) {
        output << value;
    } else {
        output << "null";
    }
}

void write_optional_u8(std::ostream& output, const std::uint8_t value) {
    if (value == 255) {
        output << "null";
    } else {
        output << static_cast<unsigned>(value);
    }
}

std::string event_json(const std::size_t job_index,
                       const std::string_view job_identity,
                       const diagnostics::Task7DiagnosticEvent& event) {
    std::ostringstream output;
    output << "{\"schema\":" << json_escape(kTask7V3ProvisionerDiagnosticsSchema)
           << ",\"record_type\":\"EVENT\""
           << ",\"job_index\":" << job_index
           << ",\"job_identity\":" << json_escape(job_identity)
           << ",\"phase\":" << json_escape(event.phase)
           << ",\"completed\":" << (event.completed ? "true" : "false")
           << ",\"duration_us\":" << event.duration_us
           << ",\"call_count\":" << event.call_count
           << ",\"max_single_call_us\":" << event.max_single_call_us
           << ",\"engine_process_count\":" << event.engine_process_count
           << ",\"semantic_action_count\":" << event.semantic_action_count
           << ",\"decision_index\":" << event.decision_index
           << ",\"engine_step_index\":" << event.engine_step_index
           << ",\"acting_player\":"
           << static_cast<unsigned>(event.acting_player)
           << ",\"candidate_count\":" << event.candidate_count
           << ",\"candidate_total\":" << event.candidate_total
           << ",\"candidate_max\":" << event.candidate_max
           << ",\"observation_count\":" << event.observation_count
           << ",\"continuation_decision_count\":"
           << event.continuation_decision_count
           << ",\"continuation_action_count\":"
           << event.continuation_action_count
           << ",\"step_accepted_count\":" << event.step_accepted_count
           << ",\"step_rejected_count\":" << event.step_rejected_count
           << ",\"supported_evaluations\":" << event.supported_evaluations
           << ",\"not_applicable_evaluations\":"
           << event.not_applicable_evaluations
           << ",\"unsupported_evaluations\":"
           << event.unsupported_evaluations
           << ",\"invalid_evaluations\":" << event.invalid_evaluations
           << ",\"f0_count\":" << event.f0_count
           << ",\"f1_count\":" << event.f1_count
           << ",\"f2_count\":" << event.f2_count
           << ",\"f3_count\":" << event.f3_count
           << ",\"f4_count\":" << event.f4_count
           << ",\"decision_family\":"
           << json_escape(event.decision_family)
           << ",\"request_kind\":" << json_escape(event.request_kind)
           << ",\"public_observation_digest\":"
           << json_escape(event.public_observation_digest)
           << ",\"public_current_state_fingerprint\":"
           << json_escape(event.public_current_state_fingerprint)
           << ",\"public_candidate_domain_digest\":"
           << json_escape(event.public_candidate_domain_digest)
           << ",\"public_semantic_decision_id\":"
           << json_escape(event.public_semantic_decision_id)
           << ",\"selected_public_action_key\":"
           << json_escape(event.selected_public_action_key)
           << ",\"fallback_level\":";
    write_optional_u8(output, event.fallback_level);
    output << ",\"public_turn_count_present\":"
           << (event.public_turn_count_present ? "true" : "false")
           << ",\"public_turn_count\":";
    write_optional_u64(output, event.public_turn_count_present,
                       event.public_turn_count);
    output << ",\"public_turn_player\":";
    write_optional_u8(output, event.public_turn_player);
    output << ",\"public_phase\":" << json_escape(event.public_phase)
           << ",\"public_life_points_present\":"
           << (event.public_life_points_present ? "true" : "false")
           << ",\"public_life_points_p0\":";
    write_optional_u64(output, event.public_life_points_present,
                       event.public_life_points_p0);
    output << ",\"public_life_points_p1\":";
    write_optional_u64(output, event.public_life_points_present,
                       event.public_life_points_p1);
    output << ",\"public_entity_count\":" << event.public_entity_count
           << ",\"public_visible_event_count\":"
           << event.public_visible_event_count
           << ",\"public_chain_length\":" << event.public_chain_length
           << ",\"continuation_present\":"
           << (event.continuation_present ? "true" : "false")
           << ",\"continuation_kind\":"
           << json_escape(event.continuation_kind)
           << ",\"continuation_step\":" << event.continuation_step
           << ",\"continuation_selected_count\":"
           << event.continuation_selected_count
           << ",\"continuation_remaining_count\":"
           << event.continuation_remaining_count
           << ",\"continuation_min_count\":"
           << event.continuation_min_count
           << ",\"continuation_max_count\":"
           << event.continuation_max_count
           << ",\"continuation_can_finish\":"
           << (event.continuation_can_finish ? "true" : "false")
           << ",\"continuation_can_cancel\":"
           << (event.continuation_can_cancel ? "true" : "false")
           << ",\"closure_kind\":" << json_escape(event.closure_kind)
           << ",\"failure_code\":" << json_escape(event.failure_code)
           << ",\"failure_stage\":" << json_escape(event.failure_stage)
           << ",\"teacher_ranking_detail_present\":"
           << (event.teacher_ranking_detail_present ? "true" : "false")
           << "}\n";
    return output.str();
}

}  // namespace

Task7V3ProvisionerDiagnosticWriter::Task7V3ProvisionerDiagnosticWriter(
    std::filesystem::path path) {
    if (std::filesystem::exists(path)) {
        throw std::invalid_argument(
            "Task7 V3 diagnostics output already exists");
    }
    output_.open(std::move(path), std::ios::out | std::ios::trunc);
    if (!output_) {
        throw std::runtime_error("Task7 V3 diagnostics output could not be opened");
    }
}

bool Task7V3ProvisionerDiagnosticWriter::failed() const noexcept {
    return failed_;
}

const std::string& Task7V3ProvisionerDiagnosticWriter::error() const noexcept {
    return error_;
}

bool Task7V3ProvisionerDiagnosticWriter::write_line(
    const std::string& line) noexcept {
    if (failed_) return false;
    try {
        output_ << line;
        output_.flush();
        if (output_) return true;
        failed_ = true;
        error_ = "Task7 V3 diagnostics output write failed";
    } catch (const std::exception& exception) {
        failed_ = true;
        error_ = exception.what();
    } catch (...) {
        failed_ = true;
        error_ = "Task7 V3 diagnostics output write threw";
    }
    return false;
}

bool Task7V3ProvisionerDiagnosticWriter::write_run_start(
    const std::string_view source_commit, const std::size_t job_count) noexcept {
    try {
        std::ostringstream output;
        output << "{\"schema\":"
               << json_escape(kTask7V3ProvisionerDiagnosticsSchema)
               << ",\"record_type\":\"RUN_START\",\"source_commit\":"
               << json_escape(source_commit) << ",\"job_count\":" << job_count
               << "}\n";
        return write_line(output.str());
    } catch (const std::exception& exception) {
        failed_ = true;
        error_ = exception.what();
    } catch (...) {
        failed_ = true;
        error_ = "Task7 V3 diagnostics RUN_START serialization threw";
    }
    return false;
}

bool Task7V3ProvisionerDiagnosticWriter::write_job_start(
    const std::size_t job_index, const Task7CollectionJobV3& job) noexcept {
    try {
        const auto job_identity = task7_collection_job_identity_v3(job);
        std::ostringstream output;
        output << "{\"schema\":"
               << json_escape(kTask7V3ProvisionerDiagnosticsSchema)
               << ",\"record_type\":\"JOB_START\",\"job_index\":"
               << job_index << ",\"job_identity\":"
               << json_escape(job_identity) << ",\"root_seed\":"
               << job.root_seed << ",\"placement\":"
               << json_escape(placement_name(job.seat_assignment))
               << ",\"starting_player\":"
               << static_cast<unsigned>(job.starting_player) << "}\n";
        return write_line(output.str());
    } catch (const std::exception& exception) {
        failed_ = true;
        error_ = exception.what();
    } catch (...) {
        failed_ = true;
        error_ = "Task7 V3 diagnostics JOB_START serialization threw";
    }
    return false;
}

bool Task7V3ProvisionerDiagnosticWriter::write_event(
    const std::size_t job_index, const std::string_view job_identity,
    const diagnostics::Task7DiagnosticEvent& event) noexcept {
    try {
        return write_line(event_json(job_index, job_identity, event));
    } catch (const std::exception& exception) {
        failed_ = true;
        error_ = exception.what();
    } catch (...) {
        failed_ = true;
        error_ = "Task7 V3 diagnostics EVENT serialization threw";
    }
    return false;
}

bool Task7V3ProvisionerDiagnosticWriter::write_job_end(
    const std::size_t job_index, const Task7CollectionJobV3& job,
    const policy::TeacherRunnerV4TrajectoryRunResult& run) noexcept {
    try {
        const auto job_identity = task7_collection_job_identity_v3(job);
        std::ostringstream output;
        output << "{\"schema\":"
               << json_escape(kTask7V3ProvisionerDiagnosticsSchema)
               << ",\"record_type\":\"JOB_END\",\"job_index\":"
               << job_index << ",\"job_identity\":"
               << json_escape(job_identity) << ",\"error_present\":"
               << ((run.error.has_value() || !run.diagnostic.empty()) ?
                       "true" : "false")
               << ",\"quarantined\":"
               << (run.quarantined ? "true" : "false")
               << ",\"envelope_present\":"
               << (run.envelope.has_value() ? "true" : "false")
               << ",\"closure_kind\":" << json_escape(closure_name(run))
               << "}\n";
        return write_line(output.str());
    } catch (const std::exception& exception) {
        failed_ = true;
        error_ = exception.what();
    } catch (...) {
        failed_ = true;
        error_ = "Task7 V3 diagnostics JOB_END serialization threw";
    }
    return false;
}

bool Task7V3ProvisionerDiagnosticWriter::write_run_end(
    const std::size_t job_count, const bool provisioning_success,
    const bool authority_generated) noexcept {
    try {
        std::ostringstream output;
        output << "{\"schema\":"
               << json_escape(kTask7V3ProvisionerDiagnosticsSchema)
               << ",\"record_type\":\"RUN_END\",\"job_count\":"
               << job_count << ",\"provisioning_success\":"
               << (provisioning_success ? "true" : "false")
               << ",\"authority_generated\":"
               << (authority_generated ? "true" : "false") << "}\n";
        return write_line(output.str());
    } catch (const std::exception& exception) {
        failed_ = true;
        error_ = exception.what();
    } catch (...) {
        failed_ = true;
        error_ = "Task7 V3 diagnostics RUN_END serialization threw";
    }
    return false;
}

diagnostics::Task7DiagnosticObserver
Task7V3ProvisionerDiagnosticWriter::observer(
    const std::size_t job_index, std::string job_identity) {
    return [this, job_index, job_identity = std::move(job_identity)](
               const diagnostics::Task7DiagnosticEvent& event) {
        write_event(job_index, job_identity, event);
    };
}

}  // namespace ygo::phase6::tooling
