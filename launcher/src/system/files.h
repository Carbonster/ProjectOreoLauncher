// files.h - paths, files and folders on this machine.
#pragma once

#include <string>
#include <vector>

namespace oreo {

std::string path_join(const std::string& a, const std::string& b);
std::string path_dir(const std::string& p);          // directory part, no trailing slash
std::string path_filename(const std::string& p);
std::string path_stem(const std::string& p);         // filename without extension
std::string path_ext(const std::string& p);          // lowercase extension, with dot

bool file_exists(const std::string& p);
bool dir_exists(const std::string& p);
bool make_dirs(const std::string& p);                // recursive mkdir
bool is_link(const std::string& p);
// Deletes a folder and everything under it. Missing is success - the caller wanted it gone.
bool remove_tree(const std::string& p);
std::vector<std::string> list_dirs(const std::string& p);   // immediate subdirectory names
std::vector<std::string> list_files(const std::string& p);  // immediate file names

bool read_file(const std::string& p, std::string& out);
bool write_file(const std::string& p, const std::string& data);

// Absolute path of the running executable, and the directory holding it.
std::string exe_path();
std::string exe_dir();

// A file's bytes as base64, "" when it cannot be read.
std::string read_file_base64(const std::string& path);

// Reads a StringFileInfo field ("FileVersion", "ProductName", "CompanyName", ...) out of an
// exe or dll. Returns "" when the file has no version resource or the field is absent.
std::string file_version_field(const std::string& path, const std::string& field);

}  // namespace oreo
