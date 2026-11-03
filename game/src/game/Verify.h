#pragma once
#include <filesystem>

namespace lc {

/// Runs deterministic automated verification of Multiplayer (1..5P separate boards, unique
/// characters, turn rotation, Coordinate, elimination) and Attach / Guerrilla Camp mechanics.
/// Writes a human-readable report to `reportPath` (if non-empty) and returns 0 on 100% PASS.
int runMechanicsVerification(const std::filesystem::path& reportPath = {});

} // namespace lc
