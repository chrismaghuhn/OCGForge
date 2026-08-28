#pragma once

#include "ygo/trajectory/codec.hpp"

namespace ygo::trajectory {

DecodeResult<environment::CertifiedEnvironmentConfig> decode_environment_identity_input(
    const std::vector<std::uint8_t>& bytes) noexcept;

DecodeResult<environment::EpisodeSpec> decode_episode_identity_input(
    const std::vector<std::uint8_t>& bytes,
    const environment::CertifiedEnvironmentConfig& config) noexcept;

bool is_current_certified_environment(
    const environment::CertifiedEnvironmentConfig& config) noexcept;

}  // namespace ygo::trajectory
