// page.h - the window's page: ui/page/, built into the executable (launcher.rc).
#pragma once

#include <string>

namespace oreo {

// The whole page as one document, with its font, already in the right theme and contrast -
// switching either afterwards fades the buttons across, which on the first paint would look
// like the window flickering.
std::string page_html(bool light, bool high_contrast);

}  // namespace oreo
