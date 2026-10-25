#include "ui/page.h"

#include <windows.h>

#include <algorithm>

#include "system/log.h"
#include "system/text.h"

namespace oreo {

namespace {

// An RCDATA resource of this executable (launcher.rc); "" when it is missing.
std::string resource(const wchar_t* name) {
    HMODULE self = GetModuleHandleW(nullptr);
    HRSRC found = FindResourceW(self, name, RT_RCDATA);
    HGLOBAL loaded = found ? LoadResource(self, found) : nullptr;
    const void* bytes = loaded ? LockResource(loaded) : nullptr;
    DWORD size = found ? SizeofResource(self, found) : 0;
    if (!bytes || !size) {
        Log::error("resource missing from the executable: " + to_utf8(name));
        return "";
    }
    return std::string((const char*)bytes, size);
}

std::string text_resource(const wchar_t* name) {
    std::string text = resource(name);
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

// The page's parts, in the order they cascade.
const wchar_t* const kStyles[] = {L"PAGE_WINDOW_CSS", L"PAGE_CARDS_CSS", L"PAGE_SHEETS_CSS", L"PAGE_CONTRAST_CSS"};
const wchar_t* const kScripts[] = {L"PAGE_STAGE_JS", L"PAGE_CARDS_JS", L"PAGE_SHEETS_JS"};

template <size_t N>
std::string joined(const wchar_t* const (&names)[N]) {
    std::string text;
    for (const wchar_t* name : names) text += text_resource(name);
    return text;
}

void replace_once(std::string& text, const std::string& marker, const std::string& with) {
    size_t at = text.find(marker);
    if (at != std::string::npos) text.replace(at, marker.size(), with);
}

// The page cannot open files, so its typeface travels inside it. Without it the page falls back
// to the next font in its list.
std::string font_face_css() {
    std::string font = resource(L"MONTSERRAT_BOLD");
    if (font.empty()) return "";
    return "@font-face{font-family:'Montserrat';font-weight:700;font-style:normal;"
           "src:url(data:font/otf;base64," + base64_encode(font) + ") format('opentype');}";
}

}  // namespace

std::string page_html(bool light, bool high_contrast) {
    std::string page = text_resource(L"PAGE_HTML");
    replace_once(page, "/*PAGE_CSS*/", joined(kStyles));
    replace_once(page, "/*PAGE_JS*/", joined(kScripts));
    replace_once(page, "/*FONT_FACE*/", font_face_css());
    std::string classes = std::string(light ? "light" : "") + (light && high_contrast ? " " : "") +
                          (high_contrast ? "hc" : "");
    if (!classes.empty()) replace_once(page, "<body>", "<body class=\"" + classes + "\">");
    return page;
}

}  // namespace oreo
