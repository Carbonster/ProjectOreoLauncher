#include "version.h"

#include <vector>

#include "util.h"

namespace oreo {

namespace {

struct ParsedVersion {
    bool valid = false;
    std::vector<long long> numbers;
    bool prerelease = false;   // "1.2.0rc1", "1.2.0b3" - sorts below plain "1.2.0"
};

ParsedVersion parse(const std::string& raw) {
    ParsedVersion out;
    std::string v = trim(raw);
    if (v.empty()) return out;
    if (v[0] == 'v' || v[0] == 'V') v = v.substr(1);

    std::string current;
    for (size_t i = 0; i <= v.size(); ++i) {
        char c = i < v.size() ? v[i] : '.';
        if (c >= '0' && c <= '9') {
            current.push_back(c);
            continue;
        }
        if (c == '.') {
            if (current.empty()) break;
            out.numbers.push_back(std::stoll(current));
            current.clear();
            continue;
        }
        // A letter or dash starts a pre-release / local tag; the numeric part ends here.
        if (!current.empty()) out.numbers.push_back(std::stoll(current));
        current.clear();
        std::string tail = to_lower(v.substr(i));
        out.prerelease = tail.find("rc") != std::string::npos || tail.find('a') != std::string::npos ||
                         tail.find('b') != std::string::npos || tail.find("dev") != std::string::npos;
        break;
    }
    out.valid = !out.numbers.empty();
    return out;
}

}  // namespace

bool version_parsable(const std::string& v) { return parse(v).valid; }

VersionOrder compare_versions(const std::string& a, const std::string& b) {
    ParsedVersion pa = parse(a);
    ParsedVersion pb = parse(b);
    if (!pa.valid || !pb.valid) return VersionOrder::Unknown;

    size_t n = pa.numbers.size() > pb.numbers.size() ? pa.numbers.size() : pb.numbers.size();
    for (size_t i = 0; i < n; ++i) {
        long long x = i < pa.numbers.size() ? pa.numbers[i] : 0;
        long long y = i < pb.numbers.size() ? pb.numbers[i] : 0;
        if (x < y) return VersionOrder::Less;
        if (x > y) return VersionOrder::Greater;
    }
    if (pa.prerelease != pb.prerelease) return pa.prerelease ? VersionOrder::Less : VersionOrder::Greater;
    return VersionOrder::Equal;
}

bool version_newer(const std::string& a, const std::string& b) {
    return compare_versions(a, b) == VersionOrder::Greater;
}

bool version_older(const std::string& a, const std::string& b) {
    return compare_versions(a, b) == VersionOrder::Less;
}

bool version_same(const std::string& a, const std::string& b) {
    return compare_versions(a, b) == VersionOrder::Equal;
}

std::string nmspy_target_build(const std::string& nmspy_version) {
    ParsedVersion p = parse(nmspy_version);
    // Game builds are six digit numbers; a "0.1.15" style version is not a build scheme.
    if (!p.valid || p.numbers.empty() || p.numbers[0] < 100000) return "";
    return std::to_string(p.numbers[0]);
}

}  // namespace oreo
