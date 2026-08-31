#include <vector>

#include "ygo/environment/public_decision.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/policy/policy.hpp"

int main() {
    ygo::environment::PublicEnvironmentObservationInput observation;
    std::vector<ygo::environment::EnvironmentActionCandidate> candidates;
    const ygo::policy::PolicyInput input{observation, candidates};
    const ygo::policy::PolicySelectionResult result;
    const ygo::policy::PolicyExecutionBinding binding;
    (void)input;
    (void)result;
    (void)binding;
    return 0;
}
