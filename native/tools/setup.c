/* Party9Recomp-<version>-Setup.exe: the release zip with a small installer in front of it.

   The zip is appended to this program, followed by a 16-byte trailer: "PRSETUP1" and the zip's offset as a
   little-endian 64-bit number (scripts/make_release.ps1 does the appending). The installer asks for a folder,
   unpacks the zip there with Windows' own tar.exe (built into Windows 10 and 11), offers a desktop shortcut, and
   offers to open the launcher. It installs nothing else, writes no registry keys, and needs no administrator
   rights: Party9Recomp lives entirely in its folder, and removing it is deleting that folder.

   Setup.exe /S <folder> unpacks into <folder> (making <folder>\Party9Recomp) without asking anything, for scripts
   and tests; its exit code is 0 when it worked.

   Build: gcc -O2 -municode -mwindows setup.c setup_rc.o -o setup.exe -lole32 -lshell32 -luuid -lcomctl32 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

#ifndef SETUP_VERSION
#define SETUP_VERSION L"0.1.0"
#endif
#define TITLE L"Party9Recomp " SETUP_VERSION L" Setup"
#define MAGIC "PRSETUP1"

static void say(const wchar_t* text, UINT flags) { MessageBoxW(NULL, text, TITLE, flags); }

/* Where the zip starts inside this program, or 0 if nothing is appended. */
static uint64_t payload_offset(HANDLE self, uint64_t* end) {
    LARGE_INTEGER size;
    char trailer[16];
    DWORD got = 0;
    if (!GetFileSizeEx(self, &size) || size.QuadPart < 16) return 0;
    LARGE_INTEGER at = {.QuadPart = size.QuadPart - 16};
    if (!SetFilePointerEx(self, at, NULL, FILE_BEGIN) || !ReadFile(self, trailer, 16, &got, NULL) || got != 16) return 0;
    if (memcmp(trailer, MAGIC, 8)) return 0;
    uint64_t offset;
    memcpy(&offset, trailer + 8, 8);
    *end = (uint64_t)size.QuadPart - 16;
    return offset < *end ? offset : 0;
}

/* The zip, copied out of this program into a file of its own for tar to read. */
static int write_payload(const wchar_t* to) {
    wchar_t me[MAX_PATH];
    GetModuleFileNameW(NULL, me, MAX_PATH);
    HANDLE self = CreateFileW(me, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (self == INVALID_HANDLE_VALUE) return 0;
    uint64_t end = 0, offset = payload_offset(self, &end);
    HANDLE out = offset ? CreateFileW(to, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL)
                        : INVALID_HANDLE_VALUE;
    int ok = out != INVALID_HANDLE_VALUE;
    if (ok) {
        LARGE_INTEGER at = {.QuadPart = (LONGLONG)offset};
        SetFilePointerEx(self, at, NULL, FILE_BEGIN);
        static char buffer[1 << 20];
        for (uint64_t left = end - offset; ok && left;) {
            DWORD want = left > sizeof buffer ? (DWORD)sizeof buffer : (DWORD)left, got = 0, put = 0;
            ok = ReadFile(self, buffer, want, &got, NULL) && got == want && WriteFile(out, buffer, got, &put, NULL) &&
                 put == got;
            left -= got;
        }
        CloseHandle(out);
    }
    CloseHandle(self);
    return ok;
}

/* The folder picker; 1 with *out filled if a folder was chosen. */
static int pick_folder(wchar_t* out, size_t size) {
    IFileOpenDialog* dialog = NULL;
    int chosen = 0;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void**)&dialog)))
        return 0;
    DWORD options = 0;
    IFileOpenDialog_GetOptions(dialog, &options);
    IFileOpenDialog_SetOptions(dialog, options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    IFileOpenDialog_SetTitle(dialog, L"Choose where to put Party9Recomp (a Party9Recomp folder is made inside it)");
    IShellItem* start = NULL;
    wchar_t* profile = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_Profile, 0, NULL, &profile)) &&
        SUCCEEDED(SHCreateItemFromParsingName(profile, NULL, &IID_IShellItem, (void**)&start))) {
        IFileOpenDialog_SetFolder(dialog, start);
        IShellItem_Release(start);
    }
    CoTaskMemFree(profile);
    if (SUCCEEDED(IFileOpenDialog_Show(dialog, NULL))) {
        IShellItem* item = NULL;
        wchar_t* path = NULL;
        if (SUCCEEDED(IFileOpenDialog_GetResult(dialog, &item)) &&
            SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path))) {
            wcsncpy(out, path, size - 1);
            out[size - 1] = 0;
            chosen = 1;
            CoTaskMemFree(path);
        }
        if (item) IShellItem_Release(item);
    }
    IFileOpenDialog_Release(dialog);
    return chosen;
}

/* A small window with a moving bar while tar works, so it's clear something is happening. */
static HWND progress_window(void) {
    INITCOMMONCONTROLSEX controls = {sizeof controls, ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&controls);
    int w = 440, h = 120;
    HWND window = CreateWindowExW(WS_EX_TOPMOST, L"STATIC", TITLE, WS_POPUP | WS_CAPTION | WS_VISIBLE,
                                  (GetSystemMetrics(SM_CXSCREEN) - w) / 2, (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h,
                                  NULL, NULL, NULL, NULL);
    CreateWindowExW(0, L"STATIC", L"Unpacking Party9Recomp... (about a minute)", WS_CHILD | WS_VISIBLE, 20, 14, 400, 20,
                    window, NULL, NULL, NULL);
    HWND bar = CreateWindowExW(0, PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE | PBS_MARQUEE, 20, 42, 390, 20, window,
                               NULL, NULL, NULL);
    SendMessageW(bar, PBM_SETMARQUEE, TRUE, 30);
    return window;
}

/* Runs tar to unpack the zip into folder, keeping the window alive meanwhile. Returns tar's exit code. */
static DWORD unpack(const wchar_t* zip, const wchar_t* folder) {
    wchar_t tar[MAX_PATH], command[3 * MAX_PATH];
    GetSystemDirectoryW(tar, MAX_PATH);
    wcscat(tar, L"\\tar.exe");
    if (GetFileAttributesW(tar) == INVALID_FILE_ATTRIBUTES) return 9999;
    swprintf(command, sizeof command / sizeof *command, L"\"%ls\" -x -f \"%ls\" -C \"%ls\"", tar, zip, folder);
    STARTUPINFOW start = {sizeof start};
    PROCESS_INFORMATION process;
    if (!CreateProcessW(tar, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &start, &process)) return 9998;
    HWND window = progress_window();
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(1, &process.hProcess, FALSE, INFINITE, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0) break;
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DestroyWindow(window);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    return code;
}

static void desktop_shortcut(const wchar_t* folder) {
    wchar_t* desktop = NULL;
    wchar_t target[MAX_PATH], link[MAX_PATH];
    if (FAILED(SHGetKnownFolderPath(&FOLDERID_Desktop, 0, NULL, &desktop))) return;
    swprintf(target, MAX_PATH, L"%ls\\Launcher.exe", folder);
    swprintf(link, MAX_PATH, L"%ls\\Party9Recomp.lnk", desktop);
    CoTaskMemFree(desktop);
    IShellLinkW* shell_link = NULL;
    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void**)&shell_link)))
        return;
    IShellLinkW_SetPath(shell_link, target);
    IShellLinkW_SetWorkingDirectory(shell_link, folder);
    IShellLinkW_SetDescription(shell_link, L"Party9Recomp: Mario Party 9 on your PC, from your own disc");
    IPersistFile* file = NULL;
    if (SUCCEEDED(IShellLinkW_QueryInterface(shell_link, &IID_IPersistFile, (void**)&file))) {
        IPersistFile_Save(file, link, TRUE);
        IPersistFile_Release(file);
    }
    IShellLinkW_Release(shell_link);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR args, int show) {
    (void)instance, (void)previous, (void)args, (void)show;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    int count = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (argv && count == 3 && !_wcsicmp(argv[1], L"/S")) { /* unattended: Setup.exe /S <folder>, no questions */
        wchar_t zip[MAX_PATH];
        GetTempPathW(MAX_PATH, zip);
        wcscat(zip, L"Party9Recomp-setup.zip");
        CreateDirectoryW(argv[2], NULL);
        DWORD code = write_payload(zip) ? unpack(zip, argv[2]) : 2;
        DeleteFileW(zip);
        return (int)code;
    }
    if (MessageBoxW(NULL,
                    L"This puts Party9Recomp " SETUP_VERSION L" in a folder you choose.\n\n"
                    L"Party9Recomp contains no part of Mario Party 9. You need your own Mario Party 9 disc (USA, SSQE01) as a "
                    L"disc image, and Dolphin's DolphinTool.exe; the launcher then builds the game on this PC.\n\n"
                    L"Pick a place with about 20 GB free, not inside Program Files. A folder named Party9Recomp is made "
                    L"inside the one you pick. Continue?",
                    TITLE, MB_OKCANCEL | MB_ICONINFORMATION) != IDOK)
        return 0;
    wchar_t base[MAX_PATH], folder[MAX_PATH], zip[MAX_PATH];
    for (;;) {
        if (!pick_folder(base, MAX_PATH)) return 0;
        wchar_t lower[MAX_PATH];
        wcscpy(lower, base);
        _wcslwr(lower);
        if (wcsstr(lower, L"\\program files") || wcsstr(lower, L"\\windows")) {
            say(L"Please pick a folder outside Program Files and Windows: the launcher writes its build and your save "
                L"inside its own folder, which Windows doesn't allow there.", MB_ICONWARNING);
            continue;
        }
        break;
    }
    swprintf(folder, MAX_PATH, L"%ls\\Party9Recomp", base);
    if (GetFileAttributesW(folder) != INVALID_FILE_ATTRIBUTES &&
        MessageBoxW(NULL, L"There's already a Party9Recomp folder there. Update it? Your save, your built game, and "
                          L"your settings in it are kept; only Party9Recomp's own files are replaced.",
                    TITLE, MB_YESNO | MB_ICONQUESTION) != IDYES)
        return 0;
    GetTempPathW(MAX_PATH, zip);
    wcscat(zip, L"Party9Recomp-setup.zip");
    if (!write_payload(zip)) {
        say(L"This setup file seems to be damaged or incomplete. Please download it again.", MB_ICONERROR);
        return 1;
    }
    DWORD code = unpack(zip, base);
    DeleteFileW(zip);
    wchar_t launcher[MAX_PATH];
    swprintf(launcher, MAX_PATH, L"%ls\\Launcher.exe", folder);
    if (code || GetFileAttributesW(launcher) == INVALID_FILE_ATTRIBUTES) {
        wchar_t text[300];
        swprintf(text, 300, L"Unpacking didn't finish (code %lu). Check there's enough free space, or use the zip "
                            L"download instead.", code);
        say(text, MB_ICONERROR);
        return 1;
    }
    if (MessageBoxW(NULL, L"Party9Recomp is in place. Put a shortcut to it on the desktop?", TITLE,
                    MB_YESNO | MB_ICONQUESTION) == IDYES)
        desktop_shortcut(folder);
    if (MessageBoxW(NULL, L"Done. Open the launcher now? On its Play tab, pick your disc image and DolphinTool.exe, "
                          L"then press \"Set up the game\".\n\nTo remove Party9Recomp later, delete its folder.",
                    TITLE, MB_YESNO | MB_ICONINFORMATION) == IDYES)
        ShellExecuteW(NULL, L"open", launcher, NULL, folder, SW_SHOWNORMAL);
    CoUninitialize();
    return 0;
}
