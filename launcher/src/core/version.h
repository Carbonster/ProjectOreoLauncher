// version.h - version string comparison.
// Handles the two schemes we actually deal with: pyMHF's semver-ish "0.2.4" and NMSpy's
// game-build scheme "170671.3". Anything unparsable is reported as unknown instead of guessed.
#pragma once

#include <string>

namespace oreo {

enum class VersionOrder {
    Unknown = 0,   // at least one side could not be parsed
    Less,
    Equal,
    Greater,
};

bool version_parsable(const std::string& v);

// Compares `a` against `b` numerically, component by component ("1.10" > "1.9").
// A trailing pre-release tag ("1.2.0rc1") sorts below the same release ("1.2.0").
VersionOrder compare_versions(const std::string& a, const std::string& b);

// Convenience wrappers reading as English.
bool version_newer(const std::string& a, const std::string& b);   // a > b
bool version_older(const std::string& a, const std::string& b);   // a < b
bool version_same(const std::string& a, const std::string& b);

// NMSpy releases are versioned as "<NMS build>.<patch>" (e.g. 170671.3 targets game build
// 170671). Returns the leading build component, or empty when the scheme does not match.
std::string nmspy_target_build(const std::string& nmspy_version);

}  // namespace oreo
