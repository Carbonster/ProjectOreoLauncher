// page_test.cpp - the window's page and what crosses between it and C++: the page is put
// together whole, JSON going in is escaped, and messages coming out are read safely.
#include <iostream>
#include <string>

#include "system/json.h"
#include "ui/page.h"

namespace {

using namespace oreo;

int g_failures = 0;

bool expect(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
    return false;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

void the_page_is_put_together_whole() {
    std::string page = page_html(false, false);
    expect(!contains(page, "/*PAGE_") && !contains(page, "/*FONT_FACE*/"), "every part should be filled in");
    expect(contains(page, "@font-face"), "the typeface should travel inside the page");
    expect(contains(page, "function render(state)"), "the cards' script should be there");
    expect(contains(page, "function saveSettings()"), "the sheets' script should be there");
    expect(contains(page, "body.hc"), "the high contrast styles should be there");
    expect(!contains(page, "\r"), "the page should not depend on how git checked it out");
}

void the_page_opens_in_the_right_theme() {
    expect(contains(page_html(false, false), "<body>"), "dark is the page's own look");
    expect(contains(page_html(true, false), "<body class=\"light\">"), "light should be set before the first paint");
    expect(contains(page_html(true, true), "<body class=\"light hc\">"), "both should be set together");
}

void json_strings_are_escaped() {
    expect(json_quote("say \"hi\"\\\n") == "\"say \\\"hi\\\"\\\\\\n\"", "quotes, backslashes and newlines");
    expect(json_quote(std::string("\x01", 1)) == "\"\\u0001\"", "control characters as \\u");
    expect(json_quote("C:\\Games\\No Man's Sky") == "\"C:\\\\Games\\\\No Man's Sky\"", "a Windows path");
}

void messages_from_the_page_are_read_safely() {
    const std::string message = "{\"action\":\"savesettings\",\"gamePath\":\"D:\\\\Games\\\\NMS\",\"id\":\"\"}";
    expect(json_field(message, "action") == "savesettings", "a plain field");
    expect(json_field(message, "gamePath") == "D:\\Games\\NMS", "escapes are undone");
    expect(json_field(message, "missing").empty(), "a missing field reads as empty");
    expect(json_field("{\"action\":\"unterminated", "action") == "unterminated", "a cut message does not overrun");
    expect(json_field("{\"id\":3}", "id").empty(), "a field that is not a string reads as empty");
}

}  // namespace

int main() {
    the_page_is_put_together_whole();
    the_page_opens_in_the_right_theme();
    json_strings_are_escaped();
    messages_from_the_page_are_read_safely();

    if (g_failures) std::cerr << g_failures << " check(s) failed\n";
    return g_failures ? 1 : 0;
}
