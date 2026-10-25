#pragma once

#include <string>
#include <vector>

namespace oreo {

const char* install_record_name();

bool relative_path_is_safe(const std::string& path);

std::vector<std::string> list_tree(const std::string& dir, bool* has_link = nullptr);

bool copy_tree(const std::string& from, const std::string& to, std::string* failed = nullptr);

bool replace_mod_files(const std::string& source, const std::string& target,
                       const std::string& backup_dir, std::string& error);

}  // namespace oreo
