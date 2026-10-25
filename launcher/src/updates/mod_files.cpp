#include "updates/mod_files.h"

#include <windows.h>

#include <set>

#include "system/files.h"
#include "system/log.h"
#include "system/text.h"

namespace oreo {

namespace {

const char* kRecordName = "oreo_installed_files.txt";

void walk(const std::string& root, const std::string& rel, std::vector<std::string>& out,
          bool& has_link) {
    const std::string dir = rel.empty() ? root : path_join(root, rel);
    for (const std::string& name : list_files(dir)) {
        std::string child = path_join(rel, name);
        if (is_link(path_join(root, child))) has_link = true;
        else out.push_back(child);
    }
    for (const std::string& name : list_dirs(dir)) {
        std::string child = path_join(rel, name);
        if (is_link(path_join(root, child))) has_link = true;
        else walk(root, child, out, has_link);
    }
}

std::vector<std::string> read_record(const std::string& target) {
    std::vector<std::string> out;
    std::string text;
    if (!read_file(path_join(target, kRecordName), text)) return out;
    for (std::string line : split(text, '\n')) {
        line = trim(line);
        if (line.empty() || line[0] == ';') continue;
        for (char& c : line)
            if (c == '/') c = '\\';
        if (relative_path_is_safe(line)) out.push_back(line);
    }
    return out;
}

bool write_record(const std::string& target, const std::vector<std::string>& files) {
    std::string text = "; Files the Oreo Launcher installed here. An update replaces these and nothing else.\r\n";
    for (const std::string& file : files) text += file + "\r\n";
    return write_file(path_join(target, kRecordName), text);
}

bool copy_over(const std::string& from, const std::string& to) {
    make_dirs(path_dir(to));
    if (file_exists(to)) {
        SetFileAttributesW(to_wide(to).c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!DeleteFileW(to_wide(to).c_str())) return false;
    }
    return CopyFileW(to_wide(from).c_str(), to_wide(to).c_str(), TRUE) != 0;
}

void remove_empty_parents(const std::string& target, std::string rel) {
    for (rel = path_dir(rel); !rel.empty(); rel = path_dir(rel))
        if (!RemoveDirectoryW(to_wide(path_join(target, rel)).c_str())) break;
}

}  // namespace

const char* install_record_name() { return kRecordName; }

bool relative_path_is_safe(const std::string& path) {
    if (path.empty() || path[0] == '/' || path[0] == '\\') return false;
    if (path.find_first_of(":*?\"<>|") != std::string::npos) return false;
    for (char c : path)
        if ((unsigned char)c < 0x20) return false;
    std::string normalised;
    for (char c : path) normalised.push_back(c == '\\' ? '/' : c);
    static const std::set<std::string> devices = {
        "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8",
        "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    for (const std::string& part : split(normalised, '/')) {
        if (part.empty()) continue;
        if (part.back() == '.' || part.back() == ' ') return false;
        if (devices.count(trim(to_lower(part.substr(0, part.find('.')))))) return false;
    }
    return true;
}

std::vector<std::string> list_tree(const std::string& dir, bool* has_link) {
    std::vector<std::string> out;
    bool link = false;
    walk(dir, "", out, link);
    if (has_link) *has_link = link;
    return out;
}

bool copy_tree(const std::string& from, const std::string& to, std::string* failed) {
    for (const std::string& file : list_tree(from)) {
        if (!copy_over(path_join(from, file), path_join(to, file)) && file_exists(path_join(from, file))) {
            if (failed) *failed = file;
            return false;
        }
    }
    return true;
}

bool replace_mod_files(const std::string& source, const std::string& target,
                       const std::string& backup_dir, std::string& error) {
    error.clear();

    bool link = is_link(target);
    if (!link && dir_exists(target)) list_tree(target, &link);
    if (link) {
        error = target + " is, or contains, a link to another folder. Nothing was changed: "
                "update this mod by hand.";
        return false;
    }
    std::vector<std::string> fresh = list_tree(source, &link);
    if (link) {
        error = "The update contains a link, which no mod needs. Nothing was changed.";
        return false;
    }

    std::vector<std::string> files;
    std::set<std::string> shipped;
    for (const std::string& file : fresh) {
        if (to_lower(file) == to_lower(kRecordName)) continue;
        files.push_back(file);
        shipped.insert(to_lower(file));
    }
    if (files.empty()) {
        error = "The update has no files in it. Nothing was changed.";
        return false;
    }

    std::vector<std::string> stale;
    for (const std::string& file : read_record(target))
        if (!shipped.count(to_lower(file)) && file_exists(path_join(target, file))) stale.push_back(file);

    std::string failed;
    if (!copy_tree(target, backup_dir, &failed)) {
        error = "Could not keep a copy of " + failed + " before updating. Nothing was changed.";
        return false;
    }
    std::vector<std::string> saved;
    for (const std::vector<std::string>* list : {&files, &stale})
        for (const std::string& file : *list)
            if (file_exists(path_join(target, file))) saved.push_back(file);

    std::vector<std::string> created;
    for (const std::string& file : files) {
        const std::string to = path_join(target, file);
        const bool existed = file_exists(to);
        if (!copy_over(path_join(source, file), to)) {
            DWORD code = GetLastError();
            for (const std::string& made : created) {
                DeleteFileW(to_wide(path_join(target, made)).c_str());
                remove_empty_parents(target, made);
            }
            for (const std::string& old : saved) copy_over(path_join(backup_dir, old), path_join(target, old));
            error = "Could not write " + file + " (Windows error " + std::to_string(code) +
                    "). Is the game or an editor holding it open? The previous version was put back.";
            return false;
        }
        if (!existed) created.push_back(file);
    }

    for (const std::string& file : stale) {
        const std::string path = path_join(target, file);
        SetFileAttributesW(to_wide(path).c_str(), FILE_ATTRIBUTE_NORMAL);
        if (DeleteFileW(to_wide(path).c_str())) remove_empty_parents(target, file);
        else Log::warn("update: could not remove " + path + ", which the new version no longer has");
    }

    if (!write_record(target, files))
        Log::warn("update: could not write " + std::string(kRecordName) + " in " + target +
                  " - the next update will not know which files are its own");
    return true;
}

}  // namespace oreo
