// component_texts.h - what the window says about the components a mod runs on.
#pragma once

#include <string>

namespace oreo {

// Why a mod needs this component, in a player's terms. "" for a component without one.
const char* why_needed(const std::string& component_id);

// The owner part of https://github.com/<owner>/<repo>, which is the author.
std::string repo_owner(const std::string& url);

}  // namespace oreo
