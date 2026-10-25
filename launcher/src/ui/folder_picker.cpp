#include "ui/folder_picker.h"

#include <shlobj.h>
#include <wrl/client.h>

#include "system/log.h"
#include "system/text.h"

namespace oreo {

std::string pick_folder(HWND owner, const wchar_t* title, const std::string& initial) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        Log::error("the Windows folder picker could not be created");
        return "";
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    // FORCEFILESYSTEM keeps the result a real path: no Libraries, no virtual shell folders.
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(title);
    if (!initial.empty()) {
        Microsoft::WRL::ComPtr<IShellItem> start;
        if (SUCCEEDED(SHCreateItemFromParsingName(to_wide(initial).c_str(), nullptr, IID_PPV_ARGS(&start))))
            dialog->SetFolder(start.Get());
    }
    if (FAILED(dialog->Show(owner))) return "";   // cancelled

    Microsoft::WRL::ComPtr<IShellItem> result;
    if (FAILED(dialog->GetResult(&result))) return "";
    LPWSTR path = nullptr;
    std::string picked;
    if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        picked = to_utf8(path);
        CoTaskMemFree(path);
    }
    return picked;
}

}  // namespace oreo
