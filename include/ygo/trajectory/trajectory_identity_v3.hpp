#pragma once

#include <string>

#include "ygo/trajectory/types_v3.hpp"

namespace ygo::trajectory {

std::string public_gameplay_trajectory_id_v3(const EpisodeEnvelopeV3& value);
std::string trajectory_record_id_v3(const EpisodeEnvelopeV3& value);

}  // namespace ygo::trajectory
