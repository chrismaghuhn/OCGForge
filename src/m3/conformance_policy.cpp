#include "ygo/m3/conformance_policy.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "ocgapi_constants.h"
#include "ygo/protocol/message_decoder.hpp"

namespace ygo::m3 {
namespace {

const protocol::ActionCandidate& choose_min(
    const std::vector<const protocol::ActionCandidate*>& candidates) {
    if (candidates.empty()) {
        throw std::runtime_error("conformance policy received an empty candidate domain");
    }
    return **std::min_element(candidates.begin(), candidates.end(), [](const auto* left, const auto* right) {
        return left->semantic_key < right->semantic_key;
    });
}

}  // namespace

DeterministicConformancePolicy::DeterministicConformancePolicy(std::vector<std::uint32_t> focus_codes,
                                                               bool completion_mode)
    : focus_codes_(std::move(focus_codes)), completion_mode_(completion_mode) {}

const protocol::ActionCandidate& DeterministicConformancePolicy::choose(
    const protocol::DecisionRequest& request) const {
    protocol::validate_candidate_set(request);
    std::vector<const protocol::ActionCandidate*> candidates;
    candidates.reserve(request.candidates.size());
    for (const auto& candidate : request.candidates) {
        candidates.push_back(&candidate);
    }

    // MSG_SELECT_UNSELECT_CARD exposes two wire lists: the first list is the
    // currently selectable set and the second is the currently selected set.
    // Resolve this toggle domain before focus-code preference so a focused
    // card cannot undo an already valid material choice.
    if (request.kind == protocol::DecisionRequestKind::UnselectCard) {
        for (const auto* candidate : candidates) {
            if (candidate->action_kind == protocol::ActionKind::Finish &&
                candidate->semantic_key == "unselect.finish") {
                return *candidate;
            }
        }
        for (const auto code : focus_codes_) {
            const auto focused_selected = std::find_if(
                candidates.begin(), candidates.end(), [code](const auto* candidate) {
                    return candidate->action_kind != protocol::ActionKind::Cancel &&
                           candidate->action_kind != protocol::ActionKind::Finish &&
                           candidate->semantic_key.rfind("unselect.selected.", 0) == 0 &&
                           candidate->source_card == code;
                });
            if (focused_selected != candidates.end()) {
                return **focused_selected;
            }
        }
        for (const auto* candidate : candidates) {
            if (candidate->action_kind != protocol::ActionKind::Cancel &&
                candidate->semantic_key.rfind("unselect.selected.", 0) == 0) {
                return *candidate;
            }
        }
        for (const auto* candidate : candidates) {
            if (candidate->action_kind != protocol::ActionKind::Cancel &&
                candidate->action_kind != protocol::ActionKind::Finish) {
                return *candidate;
            }
        }
    }

    for (const auto code : focus_codes_) {
        const auto focused = std::find_if(candidates.begin(), candidates.end(), [code, &request](const auto* candidate) {
            if (request.kind == protocol::DecisionRequestKind::YesNo) {
                return false;
            }
            return candidate->source_card == code || candidate->target_card == code;
        });
        if (focused != candidates.end()) {
            return **focused;
        }
    }

    if (request.kind == protocol::DecisionRequestKind::Chain) {
        for (const auto* candidate : candidates) {
            if (candidate->semantic_key == "chain.pass") {
                return *candidate;
            }
        }
    }
    if (request.kind == protocol::DecisionRequestKind::YesNo) {
        if (completion_mode_) {
            for (const auto* candidate : candidates) {
                if (candidate->semantic_key == "yes_no.no") {
                    return *candidate;
                }
            }
        }
        for (const auto* candidate : candidates) {
            if (candidate->semantic_key == "yes_no.yes") {
                return *candidate;
            }
        }
    }
    if (request.kind == protocol::DecisionRequestKind::UnselectCard) {
        for (const auto* candidate : candidates) {
            if (candidate->action_kind != protocol::ActionKind::Cancel &&
                candidate->action_kind != protocol::ActionKind::Finish) {
                return *candidate;
            }
        }
    }
    if (request.kind == protocol::DecisionRequestKind::IdleCommand) {
        constexpr std::uint32_t fixture_priority[] = {0, 1, 5, 3, 4, 6, 7, 8, 2};
        constexpr std::uint32_t completion_priority[] = {6, 7, 8, 0, 1, 5, 3, 4, 2};
        const auto& priority = completion_mode_ ? completion_priority : fixture_priority;
        for (const auto phase : priority) {
            std::vector<const protocol::ActionCandidate*> matching;
            for (const auto* candidate : candidates) {
                if (candidate->phase == phase) {
                    matching.push_back(candidate);
                }
            }
            if (!matching.empty()) {
                return choose_min(matching);
            }
        }
    }
    if (request.kind == protocol::DecisionRequestKind::BattleCommand) {
        constexpr std::uint32_t priority[] = {1, 0, 2, 3};
        for (const auto phase : priority) {
            std::vector<const protocol::ActionCandidate*> matching;
            for (const auto* candidate : candidates) {
                if (candidate->phase == phase) {
                    matching.push_back(candidate);
                }
            }
            if (!matching.empty()) {
                return choose_min(matching);
            }
        }
    }
    if (request.kind == protocol::DecisionRequestKind::CardSelection) {
        if (completion_mode_) {
            for (const auto* candidate : candidates) {
                if (candidate->action_kind == protocol::ActionKind::Cancel) {
                    return *candidate;
                }
            }
        }
        std::vector<const protocol::ActionCandidate*> selectable;
        for (const auto* candidate : candidates) {
            if (candidate->action_kind != protocol::ActionKind::Cancel) {
                selectable.push_back(candidate);
            }
        }
        if (!selectable.empty()) {
            return choose_min(selectable);
        }
    }
    if (request.kind == protocol::DecisionRequestKind::Position) {
        for (const auto* candidate : candidates) {
            if (candidate->position == POS_FACEUP_ATTACK) {
                return *candidate;
            }
        }
    }
    if (request.continuation.has_value()) {
        if (completion_mode_) {
            for (const auto* candidate : candidates) {
                if (candidate->action_kind == protocol::ActionKind::Finish) {
                    return *candidate;
                }
            }
        }
        for (const auto* candidate : candidates) {
            if (candidate->action_kind == protocol::ActionKind::Pick ||
                candidate->action_kind == protocol::ActionKind::AssignAmount) {
                return *candidate;
            }
        }
    }
    return choose_min(candidates);
}

}  // namespace ygo::m3
