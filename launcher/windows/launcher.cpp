// SPDX-License-Identifier: GPL-2.0-or-later
// CTR-NitroFueled-PC launcher (Windows). Sets up the game folder (the 1.21 update merged over
// the base game with hard links), edits the settings the game reads (bbport.ini, the same keys
// as the in-game menu) and starts run.py with the bundled Python, showing its output.
// Dear ImGui on Win32 + Direct3D 11, linked statically: one .exe next to run.py and bin\.
//
// Files: launcher.ini (this launcher: game folders, language, display...) and bbport.ini (the
// game's settings) in the port folder; run.py writes last_run.log there.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

namespace fs = std::filesystem;

namespace {

constexpr const char* kTitleId = "CUSA13795";
constexpr const char* kUpdateVersion = "01.21";
constexpr const char* kRepoUrl = "https://github.com/GetLostIn/CTR-NitroFueled-PC";
// The 1.21 update alone is about 8.5 GB; base game + update is much larger.
constexpr std::uintmax_t kCompleteGameMinBytes = 10ull << 30;

// ---------------------------------------------------------------------------------------------
// Text helpers

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string PathText(const fs::path& p) { return Narrow(p.wstring()); }
fs::path TextPath(const std::string& s) { return fs::path(Widen(s)); }

std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

std::string ReadText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool WriteTextAtomic(const fs::path& p, const std::string& text) {
    fs::path temp = p;
    temp += L".tmp";
    {
        std::ofstream f(temp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << text;
        if (!f) return false;
    }
    return MoveFileExW(temp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

std::string FormatBytes(std::uintmax_t bytes) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.1f GB", double(bytes) / double(1ull << 30));
    return buf;
}

// ---------------------------------------------------------------------------------------------
// Language: Spanish or English, chosen in the launcher (default: Windows' language).

bool g_spanish = true;
const char* T(const char* es, const char* en) { return g_spanish ? es : en; }

// ---------------------------------------------------------------------------------------------
// key=value files, edited in place: comments, order and unknown keys are kept.

struct Ini {
    std::vector<std::string> lines;
    std::map<std::string, std::string> values;

    void Load(const fs::path& path) {
        lines.clear();
        values.clear();
        std::stringstream ss(ReadText(path));
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
            const auto t = Trim(line);
            if (t.empty() || t[0] == '#' || t[0] == ';') continue;
            const auto eq = t.find('=');
            if (eq == std::string::npos) continue;
            values[Trim(t.substr(0, eq))] = Trim(t.substr(eq + 1));
        }
    }
    std::string Get(const std::string& key, const std::string& fallback = "") const {
        const auto it = values.find(key);
        return it == values.end() ? fallback : it->second;
    }
    bool Flag(const std::string& key, bool fallback) const {
        const auto v = Get(key);
        return v.empty() ? fallback : v != "0";
    }
    int Int(const std::string& key, int fallback) const {
        const auto v = Get(key);
        if (v.empty()) return fallback;
        char* end = nullptr;
        const long n = std::strtol(v.c_str(), &end, 10);
        return end && *end == '\0' ? int(n) : fallback;
    }
    void Set(const std::string& key, const std::string& value) { values[key] = value; }
    bool Save(const fs::path& path, const char* header) {
        std::vector<std::string> out;
        std::map<std::string, bool> written;
        for (const auto& line : lines) {
            const auto t = Trim(line);
            const auto eq = t.find('=');
            if (!t.empty() && t[0] != '#' && t[0] != ';' && eq != std::string::npos) {
                const auto key = Trim(t.substr(0, eq));
                const auto it = values.find(key);
                if (it != values.end()) {
                    if (!written[key]) out.push_back(key + "=" + it->second);
                    written[key] = true;
                    continue;
                }
            }
            out.push_back(line);
        }
        if (lines.empty() && header) out.push_back(header);
        for (const auto& [key, value] : values) {
            if (!written[key]) out.push_back(key + "=" + value);
        }
        std::string text;
        for (const auto& line : out) text += line + "\n";
        if (!WriteTextAtomic(path, text)) return false;
        lines = out;
        return true;
    }
};

// ---------------------------------------------------------------------------------------------
// param.sfo: the game's title, version and category (gd = game, gp = patch).

std::map<std::string, std::string> ReadSfo(const fs::path& file) {
    std::map<std::string, std::string> out;
    const std::string data = ReadText(file);
    auto u32 = [&](size_t at) -> uint32_t {
        if (at + 4 > data.size()) return 0;
        return uint32_t(uint8_t(data[at])) | uint32_t(uint8_t(data[at + 1])) << 8 |
               uint32_t(uint8_t(data[at + 2])) << 16 | uint32_t(uint8_t(data[at + 3])) << 24;
    };
    auto u16 = [&](size_t at) -> uint32_t {
        if (at + 2 > data.size()) return 0;
        return uint32_t(uint8_t(data[at])) | uint32_t(uint8_t(data[at + 1])) << 8;
    };
    if (data.size() < 20 || data.compare(0, 4, std::string("\0PSF", 4)) != 0) return out;
    const uint32_t keys = u32(8), values = u32(12), count = u32(16);
    for (uint32_t i = 0; i < count && 20 + i * 16 + 16 <= data.size(); ++i) {
        const size_t entry = 20 + size_t(i) * 16;
        const uint32_t key_off = u16(entry), fmt = u16(entry + 2), len = u32(entry + 4), off = u32(entry + 12);
        const size_t key_at = size_t(keys) + key_off;
        if (key_at >= data.size()) continue;
        const size_t key_end = data.find('\0', key_at);
        if (key_end == std::string::npos) continue;
        const std::string key = data.substr(key_at, key_end - key_at);
        const size_t value_at = size_t(values) + off;
        if (value_at + len > data.size()) continue;
        if (fmt == 0x0404) {
            out[key] = std::to_string(u32(value_at));
        } else {
            std::string v = data.substr(value_at, len);
            while (!v.empty() && v.back() == '\0') v.pop_back();
            out[key] = v;
        }
    }
    return out;
}

enum class Kind { Empty, Missing, NotGame, OtherTitle, OtherVersion, Base, Update, Complete };

struct FolderInfo {
    Kind kind = Kind::Empty;
    std::string title, version, category;
    std::uintmax_t bytes = 0;
};

std::uintmax_t FolderBytes(const fs::path& dir) {
    std::uintmax_t total = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2)) total += it->file_size(e2);
    }
    return total;
}

FolderInfo Analyze(const fs::path& dir) {
    FolderInfo info;
    if (dir.empty()) return info;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        info.kind = Kind::Missing;
        return info;
    }
    if (!fs::is_regular_file(dir / L"eboot.bin", ec) || !fs::is_regular_file(dir / L"sce_sys" / L"param.sfo", ec)) {
        info.kind = Kind::NotGame;
        return info;
    }
    const auto sfo = ReadSfo(dir / L"sce_sys" / L"param.sfo");
    auto get = [&](const char* k) {
        const auto it = sfo.find(k);
        return it == sfo.end() ? std::string() : it->second;
    };
    info.title = get("TITLE_ID");
    info.version = get("APP_VER");
    info.category = get("CATEGORY");
    if (info.title != kTitleId) {
        info.kind = Kind::OtherTitle;
    } else if (info.category == "gd") {
        info.kind = Kind::Base;
    } else if (info.version != kUpdateVersion) {
        info.kind = Kind::OtherVersion;
    } else {
        info.bytes = FolderBytes(dir);
        info.kind = info.bytes >= kCompleteGameMinBytes ? Kind::Complete : Kind::Update;
    }
    return info;
}

// ---------------------------------------------------------------------------------------------
// Processes

fs::path ExeDir() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    return fs::path(std::wstring(buf, n)).parent_path();
}

/// The port folder: the launcher's own (portable package), or the source tree above out\.
fs::path FindRoot() {
    fs::path dir = ExeDir();
    for (int i = 0; i < 3 && !dir.empty(); ++i) {
        std::error_code ec;
        if (fs::is_regular_file(dir / L"run.py", ec)) return dir;
        dir = dir.parent_path();
    }
    return ExeDir();
}

std::wstring Quote(const std::wstring& s) { return L"\"" + s + L"\""; }

struct Python {
    std::wstring exe;
    std::wstring args;  // before the script, e.g. L"-3 "
    std::string description;
};

std::optional<Python> FindPython(const fs::path& root) {
    std::error_code ec;
    const fs::path bundled = root / L"python" / L"python.exe";
    if (fs::is_regular_file(bundled, ec)) return Python{bundled.wstring(), L"", "python\\ (bundled)"};
    wchar_t found[MAX_PATH * 4];
    if (SearchPathW(nullptr, L"py.exe", nullptr, DWORD(std::size(found)), found, nullptr)) {
        return Python{found, L"-3 ", Narrow(found)};
    }
    if (SearchPathW(nullptr, L"python.exe", nullptr, DWORD(std::size(found)), found, nullptr)) {
        // Windows' "App execution alias" opens the Microsoft Store instead of running Python.
        if (std::wstring(found).find(L"WindowsApps") == std::wstring::npos) return Python{found, L"", Narrow(found)};
    }
    const fs::path msys = L"C:\\msys64\\clang64\\bin\\python.exe";
    if (fs::is_regular_file(msys, ec)) return Python{msys.wstring(), L"", PathText(msys)};
    return std::nullopt;
}

struct Captured {
    bool started = false;
    DWORD code = 1;
    std::string output;
};

/// Runs a console program without a window and returns its output (stdout + stderr).
Captured RunCaptured(const std::wstring& exe, const std::wstring& command_line, const fs::path& cwd,
                     DWORD timeout_ms) {
    Captured result;
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &sa, 0)) return result;
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = write;
    si.hStdError = write;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = command_line;
    const BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                   cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    CloseHandle(write);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        CloseHandle(read);
        return result;
    }
    result.started = true;
    std::thread reader([&] {
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(read, buf, sizeof buf, &n, nullptr) && n) result.output.append(buf, n);
    });
    if (WaitForSingleObject(pi.hProcess, timeout_ms) == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 1);
    reader.join();
    GetExitCodeProcess(pi.hProcess, &result.code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(read);
    return result;
}

void OpenInShell(const fs::path& target) {
    ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::optional<fs::path> PickFolder(HWND owner, const wchar_t* title, const fs::path& start) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(title);
    std::error_code ec;
    if (!start.empty() && fs::is_directory(start, ec)) {
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
            dialog->SetFolder(folder);
            folder->Release();
        }
    }
    std::optional<fs::path> result;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = fs::path(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

// ---------------------------------------------------------------------------------------------
// The launcher

struct Choice {
    const char* es;
    const char* en;
    const char* value;
};

// PS4 system language codes (BB_LANGUAGE), as in SacredFire's Linux launcher.
constexpr Choice kGameLanguages[] = {
    {"Inglés", "English", "1"},   {"Español", "Spanish", "3"},  {"Francés", "French", "2"},
    {"Alemán", "German", "4"},    {"Italiano", "Italian", "5"}, {"Japonés", "Japanese", "0"},
    {"Ruso", "Russian", "8"},
};
constexpr Choice kPresentModes[] = {
    {"Mailbox (recomendado)", "Mailbox (recommended)", "Mailbox"},
    {"FIFO (VSync)", "FIFO (VSync)", "Fifo"},
    {"FIFO relajado", "FIFO Relaxed", "FifoRelaxed"},
    {"Inmediato (sin VSync)", "Immediate (no VSync)", "Immediate"},
};
constexpr int kFpsLimits[] = {0, 30, 60, 72, 90, 120, 144, 240};

enum class Job { None, Merge, Check, Play };

struct App {
    HWND hwnd = nullptr;
    fs::path root, bin;
    std::string version;
    std::optional<Python> python;

    Ini launcher;  // launcher.ini
    Ini game;      // bbport.ini

    // Game folders.
    fs::path game_dir, base_dir, update_dir;
    FolderInfo game_info, base_info, update_info;
    std::atomic<bool> game_checked{false};
    bool game_ok = false;
    std::string check_message;

    // Background work (one at a time).
    std::atomic<Job> job{Job::None};
    std::thread worker;
    std::mutex mutex;  // guards the strings below and log
    std::string job_error;
    std::atomic<size_t> merge_done{0}, merge_total{0};
    std::atomic<bool> job_finished{false};

    // The running game.
    HANDLE process = nullptr, job_object = nullptr;
    std::deque<std::string> log;
    std::string last_status;
    std::atomic<bool> game_running{false};
    DWORD exit_code = 0;
    bool show_exit = false, stopped = false;

    // Monitors and controllers (bb-gpu-capabilities).
    std::vector<std::pair<std::string, std::string>> displays;  // value (number), label
    std::vector<std::pair<std::string, std::string>> gamepads;  // GUID, name
    std::thread probe;
    std::atomic<bool> probe_done{false};

    bool want_close = false;
    bool confirm_clear_cache = false;
    std::string notice;
};

App g_app;

fs::path LauncherIni() { return g_app.root / L"launcher.ini"; }
fs::path GameIni() { return g_app.root / L"bbport.ini"; }

void SaveLauncher() {
    App& a = g_app;
    a.launcher.Set("game_dir", PathText(a.game_dir));
    a.launcher.Set("base_dir", PathText(a.base_dir));
    a.launcher.Set("update_dir", PathText(a.update_dir));
    a.launcher.Set("ui_language", g_spanish ? "es" : "en");
    if (!a.launcher.Save(LauncherIni(), "# CTR-NitroFueled-PC launcher")) {
        a.notice = T("No se pudo guardar launcher.ini.", "Could not save launcher.ini.");
    }
}

void SaveGameIni() {
    if (!g_app.game.Save(GameIni(), "# bbport settings (in-game menu: Insert / L3+R3)")) {
        g_app.notice = T("No se pudo guardar bbport.ini.", "Could not save bbport.ini.");
    }
}

void AddLog(const std::string& line) {
    std::scoped_lock lock{g_app.mutex};
    g_app.log.push_back(line);
    while (g_app.log.size() > 2000) g_app.log.pop_front();
    if (!line.empty()) g_app.last_status = line;
}

void JoinWorker() {
    if (g_app.worker.joinable()) g_app.worker.join();
}

void StartCheck() {
    App& a = g_app;
    a.game_info = Analyze(a.game_dir);
    a.game_checked = false;
    a.game_ok = false;
    a.check_message.clear();
    if (a.game_info.kind != Kind::Complete || !a.python) return;
    JoinWorker();
    a.job = Job::Check;
    a.job_finished = false;
    const fs::path dir = a.game_dir;
    a.worker = std::thread([dir] {
        App& app = g_app;
        const std::wstring cmd = Quote(app.python->exe) + L" " + app.python->args + L"-X utf8 " +
                                 Quote((app.root / L"scripts" / L"game_check.py").wstring()) + L" " + Quote(dir.wstring());
        const Captured r = RunCaptured(app.python->exe, cmd, app.root, 120000);
        {
            std::scoped_lock lock{app.mutex};
            app.game_ok = r.started && r.code == 0;
            app.check_message = r.started ? Trim(r.output) : "Python could not be started.";
        }
        app.game_checked = true;
        app.job_finished = true;
    });
}

/// Base game + 1.21 update -> a new folder of hard links (copies across drives).
void StartMerge() {
    App& a = g_app;
    fs::path target = a.base_dir.parent_path() / L"CUSA13795-1.21";
    if (target == a.base_dir || target == a.update_dir) target = a.base_dir.parent_path() / L"CUSA13795-1.21-merged";
    JoinWorker();
    a.job = Job::Merge;
    a.job_finished = false;
    a.merge_done = 0;
    a.merge_total = 0;
    {
        std::scoped_lock lock{a.mutex};
        a.job_error.clear();
    }
    const fs::path base = a.base_dir, update = a.update_dir;
    a.worker = std::thread([base, update, target] {
        App& app = g_app;
        std::vector<std::pair<fs::path, fs::path>> files;  // source, relative
        std::error_code ec;
        for (const fs::path& from : {base, update}) {
            for (fs::recursive_directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
                std::error_code e2;
                if (it->is_regular_file(e2)) files.emplace_back(it->path(), fs::relative(it->path(), from, e2));
            }
            if (ec) break;
        }
        std::string error;
        if (ec) error = "Cannot read the game folders: " + ec.message();
        app.merge_total = files.size();
        size_t copied = 0;
        for (size_t i = 0; error.empty() && i < files.size(); ++i) {
            const fs::path dest = target / files[i].second;
            std::error_code e2;
            fs::create_directories(dest.parent_path(), e2);
            if (fs::exists(dest, e2)) fs::remove(dest, e2);
            if (!CreateHardLinkW(dest.c_str(), files[i].first.c_str(), nullptr)) {
                if (!CopyFileW(files[i].first.c_str(), dest.c_str(), FALSE)) {
                    error = "Cannot create " + PathText(dest) + " (error " + std::to_string(GetLastError()) + ")";
                    break;
                }
                ++copied;
            }
            app.merge_done = i + 1;
        }
        {
            std::scoped_lock lock{app.mutex};
            app.job_error = error;
            if (error.empty()) {
                app.game_dir = target;
                app.log.push_back("Merged " + std::to_string(files.size()) + " files into " + PathText(target) +
                                  (copied ? " (" + std::to_string(copied) + " copied: different drive)" : ""));
            }
        }
        app.job_finished = true;
    });
}

const char* DefaultGameLanguage() { return g_spanish ? "3" : "1"; }

void SetEnv(const wchar_t* name, const std::string& value) {
    SetEnvironmentVariableW(name, value.empty() ? nullptr : Widen(value).c_str());
}

void StartGame() {
    App& a = g_app;
    if (!a.python || a.game_running) return;
    SaveGameIni();
    SaveLauncher();
    JoinWorker();
    {
        std::scoped_lock lock{a.mutex};
        a.log.clear();
        a.last_status.clear();
    }
    SetEnv(L"BB_GAME_DIR", PathText(a.game_dir));
    SetEnv(L"BB_NEO", a.launcher.Flag("neo", false) ? "1" : "0");
    SetEnv(L"BB_LANGUAGE", a.launcher.Get("language", DefaultGameLanguage()));
    SetEnv(L"BB_DISPLAY", a.launcher.Get("display"));
    SetEnv(L"BB_PRESENT_MODE", a.launcher.Get("present_mode", "Mailbox"));
    SetEnv(L"BB_GAMEPAD", a.launcher.Get("gamepad"));
    SetEnv(L"BB_SAVE_LOG", a.launcher.Flag("save_log", false) ? "1" : "");
    SetEnv(L"BB_FULLSCREEN", "");  // bbport.ini's fullscreen= (the menu switch) decides
    SetEnv(L"PYTHONUTF8", "1");
    SetEnv(L"PYTHONIOENCODING", "utf-8");

    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &sa, 0)) return;
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = write;
    si.hStdError = write;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = Quote(a.python->exe) + L" " + a.python->args + L"-X utf8 " + Quote((a.root / L"run.py").wstring());
    const BOOL ok = CreateProcessW(a.python->exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, a.root.c_str(), &si, &pi);
    CloseHandle(write);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        CloseHandle(read);
        AddLog("Could not start Python (error " + std::to_string(GetLastError()) + ")");
        a.exit_code = 1;
        a.show_exit = true;
        return;
    }
    // A job object, so "Stop" ends run.py and the game it started.
    a.job_object = CreateJobObjectW(nullptr, nullptr);
    if (a.job_object) AssignProcessToJobObject(a.job_object, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    a.process = pi.hProcess;
    a.game_running = true;
    a.show_exit = false;
    a.stopped = false;
    a.job = Job::Play;
    a.job_finished = false;
    AddLog("Starting the game...");
    a.worker = std::thread([read] {
        App& app = g_app;
        std::string pending;
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(read, buf, sizeof buf, &n, nullptr) && n) {
            pending.append(buf, n);
            size_t nl;
            while ((nl = pending.find('\n')) != std::string::npos) {
                AddLog(Trim(pending.substr(0, nl)));
                pending.erase(0, nl + 1);
            }
        }
        if (!pending.empty()) AddLog(Trim(pending));
        CloseHandle(read);
        WaitForSingleObject(app.process, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(app.process, &code);
        app.exit_code = code;
        app.job_finished = true;
    });
    ShowWindow(a.hwnd, SW_MINIMIZE);
}

void StopGame() {
    g_app.stopped = true;
    if (g_app.job_object) TerminateJobObject(g_app.job_object, 1);
    else if (g_app.process) TerminateProcess(g_app.process, 1);
}

/// Called every frame: picks up finished background work.
void PollJobs() {
    App& a = g_app;
    if (a.probe_done && a.probe.joinable()) a.probe.join();
    if (!a.job_finished) return;
    const Job finished = a.job.exchange(Job::None);
    a.job_finished = false;
    JoinWorker();
    if (finished == Job::Merge) {
        std::string error;
        {
            std::scoped_lock lock{a.mutex};
            error = a.job_error;
        }
        if (error.empty()) {
            SaveLauncher();
            StartCheck();
        } else {
            a.notice = error;
        }
    } else if (finished == Job::Play) {
        CloseHandle(a.process);
        a.process = nullptr;
        if (a.job_object) CloseHandle(a.job_object);
        a.job_object = nullptr;
        a.game_running = false;
        a.show_exit = true;
        a.game.Load(GameIni());  // the in-game menu may have changed settings
        ShowWindow(a.hwnd, SW_RESTORE);
        SetForegroundWindow(a.hwnd);
    }
}

void StartProbe() {
    App& a = g_app;
    a.probe_done = false;
    a.probe = std::thread([] {
        App& app = g_app;
        const fs::path tool = app.bin / L"bb-gpu-capabilities.exe";
        std::vector<std::pair<std::string, std::string>> displays, pads;
        const Captured d = RunCaptured(tool.wstring(), Quote(tool.wstring()) + L" --displays", app.bin, 10000);
        std::stringstream ds(d.output);
        std::string line;
        int number = 0;
        while (std::getline(ds, line)) {
            line = Trim(line);
            const auto t1 = line.find('\t');
            if (t1 == std::string::npos) continue;
            const auto t2 = line.find('\t', t1 + 1);
            ++number;
            std::string label = std::to_string(number) + ": " + line.substr(0, t1) + " (" +
                                line.substr(t1 + 1, t2 == std::string::npos ? std::string::npos : t2 - t1 - 1) + ")";
            if (t2 != std::string::npos && line.substr(t2 + 1) == "1") label += g_spanish ? " - principal" : " - primary";
            displays.emplace_back(std::to_string(number), label);
        }
        const Captured p = RunCaptured(tool.wstring(), Quote(tool.wstring()) + L" --gamepads", app.bin, 10000);
        std::stringstream ps(p.output);
        while (std::getline(ps, line)) {
            line = Trim(line);
            const auto t = line.find('\t');
            if (t != std::string::npos) pads.emplace_back(line.substr(0, t), line.substr(t + 1));
        }
        std::scoped_lock lock{app.mutex};
        app.displays = std::move(displays);
        app.gamepads = std::move(pads);
        app.probe_done = true;
    });
}

void ChooseFolder(fs::path& slot, FolderInfo& info, const wchar_t* title) {
    App& a = g_app;
    fs::path start = slot.empty() ? (a.base_dir.empty() ? a.root : a.base_dir.parent_path()) : slot.parent_path();
    const auto picked = PickFolder(a.hwnd, title, start);
    if (!picked) return;
    const FolderInfo found = Analyze(*picked);
    // Whatever was picked goes where it belongs.
    switch (found.kind) {
    case Kind::Complete:
        a.game_dir = *picked;
        SaveLauncher();
        StartCheck();
        return;
    case Kind::Base:
        a.base_dir = *picked;
        a.base_info = found;
        break;
    case Kind::Update:
        a.update_dir = *picked;
        a.update_info = found;
        break;
    default:
        slot = *picked;
        info = found;
        break;
    }
    SaveLauncher();
}

// ---------------------------------------------------------------------------------------------
// UI

const ImVec4 kGreen{0.45f, 0.85f, 0.45f, 1.0f};
const ImVec4 kRed{1.0f, 0.45f, 0.40f, 1.0f};
const ImVec4 kYellow{1.0f, 0.80f, 0.35f, 1.0f};
const ImVec4 kGrey{0.65f, 0.65f, 0.68f, 1.0f};

std::string Describe(const FolderInfo& f) {
    switch (f.kind) {
    case Kind::Empty: return T("sin elegir", "not chosen");
    case Kind::Missing: return T("la carpeta no existe", "the folder does not exist");
    case Kind::NotGame: return T("no tiene eboot.bin y sce_sys\\param.sfo", "no eboot.bin and sce_sys\\param.sfo");
    case Kind::OtherTitle: return std::string(T("es otro juego o región: ", "another game or region: ")) + f.title;
    case Kind::OtherVersion: return std::string(T("actualización ", "update ")) + f.version + T(" (se necesita la 01.21)", " (01.21 is needed)");
    case Kind::Base: return std::string(T("juego base ", "base game ")) + f.title + " v" + f.version;
    case Kind::Update: return std::string(T("actualización ", "update ")) + f.version + " (" + FormatBytes(f.bytes) + ")";
    case Kind::Complete: return std::string(f.title) + " v" + f.version + " (" + FormatBytes(f.bytes) + ")";
    }
    return {};
}

void HelpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void GameFlag(const char* key, const char* label, bool fallback, const char* help = nullptr) {
    bool v = g_app.game.Flag(key, fallback);
    if (ImGui::Checkbox(label, &v)) {
        g_app.game.Set(key, v ? "1" : "0");
        SaveGameIni();
    }
    if (help) HelpMarker(help);
}

void LauncherFlag(const char* key, const char* label, bool fallback, const char* help = nullptr) {
    bool v = g_app.launcher.Flag(key, fallback);
    if (ImGui::Checkbox(label, &v)) {
        g_app.launcher.Set(key, v ? "1" : "0");
        SaveLauncher();
    }
    if (help) HelpMarker(help);
}

template <size_t N>
void ChoiceCombo(const char* label, const char* key, const Choice (&choices)[N], const char* fallback) {
    const std::string current = g_app.launcher.Get(key, fallback);
    const Choice* selected = &choices[0];
    for (const auto& c : choices)
        if (current == c.value) selected = &c;
    if (ImGui::BeginCombo(label, T(selected->es, selected->en))) {
        for (const auto& c : choices) {
            if (ImGui::Selectable(T(c.es, c.en), &c == selected)) {
                g_app.launcher.Set(key, c.value);
                SaveLauncher();
            }
        }
        ImGui::EndCombo();
    }
}

void PlayTab() {
    App& a = g_app;
    const bool busy = a.job != Job::None;
    ImGui::SeparatorText(T("Juego", "Game"));

    const bool ready = a.game_info.kind == Kind::Complete && a.game_checked && a.game_ok;
    if (!a.python) {
        ImGui::TextColored(kRed, "%s", T("Falta Python 3: no se encontró python\\python.exe ni Python instalado.",
                                         "Python 3 is missing: no python\\python.exe and no installed Python."));
    }
    if (a.game_info.kind == Kind::Complete) {
        if (!a.game_checked) {
            ImGui::TextColored(kYellow, "%s", T("Comprobando el juego...", "Checking the game..."));
        } else if (a.game_ok) {
            ImGui::TextColored(kGreen, "%s %s", T("Listo:", "Ready:"), Describe(a.game_info).c_str());
        } else {
            ImGui::TextColored(kRed, "%s", T("Estos archivos no sirven:", "These files do not work:"));
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(a.check_message.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::TextColored(kGrey, "%s", PathText(a.game_dir).c_str());
        if (ImGui::Button(T("Cambiar carpeta del juego...", "Change game folder...")) && !busy && !a.game_running) {
            ChooseFolder(a.game_dir, a.game_info, L"CUSA13795 + 1.21");
        }
    } else {
        if (a.game_info.kind != Kind::Empty) {
            ImGui::TextColored(kRed, "%s: %s", PathText(a.game_dir).c_str(), Describe(a.game_info).c_str());
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(T("Necesitas tu propio dump de PS4: el juego base (CUSA13795) y la actualización 1.21 "
                                 "(CUSA13795-patch). Si ya los tienes juntos en una carpeta, elige esa.",
                                 "You need your own PS4 dump: the base game (CUSA13795) and the 1.21 update "
                                 "(CUSA13795-patch). If they are already merged in one folder, choose that one."));
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::BeginTable("setup", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("state", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("button", ImGuiTableColumnFlags_WidthFixed);
            auto row = [&](const char* what, fs::path& slot, FolderInfo& info, Kind good, const wchar_t* title, const char* id) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(what);
                ImGui::TableNextColumn();
                const bool ok = info.kind == good;
                ImGui::TextColored(ok ? kGreen : (info.kind == Kind::Empty ? kGrey : kRed), "%s", Describe(info).c_str());
                if (!slot.empty()) ImGui::TextColored(kGrey, "%s", PathText(slot).c_str());
                ImGui::TableNextColumn();
                ImGui::PushID(id);
                if (ImGui::Button(T("Elegir...", "Choose...")) && !busy) ChooseFolder(slot, info, title);
                ImGui::PopID();
            };
            row(T("Juego base", "Base game"), a.base_dir, a.base_info, Kind::Base, L"CUSA13795", "base");
            row(T("Actualización 1.21", "Update 1.21"), a.update_dir, a.update_info, Kind::Update, L"CUSA13795-patch", "update");
            ImGui::EndTable();
        }
        const bool can_merge = a.base_info.kind == Kind::Base && a.update_info.kind == Kind::Update;
        if (a.job == Job::Merge) {
            const size_t total = a.merge_total, done = a.merge_done;
            const float fraction = total ? float(done) / float(total) : 0.0f;
            char overlay[64];
            std::snprintf(overlay, sizeof overlay, "%zu / %zu", done, total);
            ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0), overlay);
        } else {
            ImGui::BeginDisabled(!can_merge || busy);
            if (ImGui::Button(T("Preparar el juego (base + 1.21)", "Prepare the game (base + 1.21)"))) StartMerge();
            ImGui::EndDisabled();
            if (can_merge) {
                ImGui::SameLine();
                ImGui::TextColored(kGrey, "%s %s", T("se crea", "creates"),
                                   PathText(a.base_dir.parent_path() / L"CUSA13795-1.21").c_str());
            }
            HelpMarker(T("Une la actualización con el juego base en una carpeta nueva usando enlaces duros: no ocupa "
                         "espacio extra ni cambia tus carpetas originales (si están en discos distintos, se copia).",
                         "Merges the update over the base game into a new folder with hard links: no extra disk "
                         "space and your original folders are not changed (copied if they are on different drives)."));
        }
        ImGui::Spacing();
        if (ImGui::Button(T("Ya tengo el juego preparado...", "I already have the merged game...")) && !busy) {
            ChooseFolder(a.game_dir, a.game_info, L"CUSA13795-1.21");
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    const float play_height = ImGui::GetFontSize() * 3.0f;
    if (a.game_running) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.16f, 1.0f));
        if (ImGui::Button(T("Cerrar el juego", "Stop the game"), ImVec2(-FLT_MIN, play_height))) StopGame();
        ImGui::PopStyleColor();
    } else {
        ImGui::BeginDisabled(!ready || busy || !a.python);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.45f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.58f, 0.26f, 1.0f));
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5f);
        if (ImGui::Button(T("JUGAR", "PLAY"), ImVec2(-FLT_MIN, play_height))) StartGame();
        ImGui::PopFont();
        ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
    }

    std::string status;
    {
        std::scoped_lock lock{a.mutex};
        status = a.last_status;
    }
    if (a.game_running) {
        ImGui::TextColored(kYellow, "%s", T("El juego está abierto. El primer arranque compila shaders y tarda un poco.",
                                            "The game is running. The first start compiles shaders and takes a while."));
    } else if (a.show_exit) {
        if (a.exit_code == 0 || a.stopped) ImGui::TextColored(kGrey, "%s", T("El juego se cerró.", "The game was closed."));
        else ImGui::TextColored(kRed, T("El juego se cerró con un error (código %lu). Mira el registro abajo o en last_run.log.",
                                        "The game ended with an error (code %lu). See the log below or last_run.log."),
                                (unsigned long)a.exit_code);
    }
    if (!status.empty()) ImGui::TextColored(kGrey, "%s", status.c_str());

    ImGui::Spacing();
    if (ImGui::CollapsingHeader(T("Registro", "Log"), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        {
            std::scoped_lock lock{a.mutex};
            ImGuiListClipper clipper;
            clipper.Begin(int(a.log.size()));
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) ImGui::TextUnformatted(a.log[size_t(i)].c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
}

void SettingsTab() {
    App& a = g_app;
    ImGui::TextColored(kGrey, "%s", T("Se aplican al pulsar Jugar. El menú del juego (Insert o L3+R3) cambia los mismos ajustes.",
                                      "Applied when you press Play. The in-game menu (Insert or L3+R3) changes the same settings."));

    ImGui::SeparatorText(T("Parches del juego", "Game patches"));
    GameFlag("patch_60fps", T("60 FPS (illusion)", "60 FPS unlock (illusion)"), false,
             T("Desbloquea los 60 FPS. Con el límite de FPS de abajo puedes ir más alto.",
               "Unlocks 60 FPS. With the FPS limit below the game can go higher."));
    GameFlag("patch_no_motion_blur", T("Sin desenfoque de movimiento (illusion)", "Disable motion blur (illusion)"), false);
    const bool neo = a.launcher.Flag("neo", false);
    ImGui::BeginDisabled(!neo);
    GameFlag("patch_pro_1080p", T("1080p en modo PS4 Pro (TroyWarez)", "1080p in PS4 Pro mode (TroyWarez)"), false,
             T("Solo funciona con el modo PS4 Pro activado (sección Consola).", "Only with PS4 Pro mode on (Console section)."));
    ImGui::EndDisabled();

    ImGui::SeparatorText(T("Rendimiento", "Performance"));
    {
        int limit = a.game.Int("fps_limit", 60);
        auto name = [](int v) -> std::string {
            if (v == 0) return T("Sin límite", "No limit");
            return std::to_string(v) + " FPS";
        };
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
        if (ImGui::BeginCombo(T("Límite de FPS", "FPS limit"), name(limit).c_str())) {
            for (int v : kFpsLimits) {
                if (ImGui::Selectable(name(v).c_str(), v == limit)) {
                    a.game.Set("fps_limit", std::to_string(v));
                    SaveGameIni();
                }
            }
            ImGui::EndCombo();
        }
        HelpMarker(T("Con el parche de 60 FPS el juego va tan rápido como permita tu PC. En una pantalla de 144 Hz sin "
                     "G-Sync/FreeSync, 72 reparte los fotogramas de forma pareja.",
                     "With the 60 FPS patch the game runs as fast as your PC allows. On a 144 Hz screen without "
                     "G-Sync/FreeSync, 72 divides the refresh evenly."));
    }
    GameFlag("async_pipelines", T("Compilar shaders en segundo plano (experimental)", "Compile shaders in the background (experimental)"), false,
             T("Menos tirones la primera vez que aparece un efecto; a veces un efecto tarda un fotograma en verse.",
               "Fewer hitches when an effect is first seen; an effect may show up a frame late."));

    ImGui::SeparatorText(T("Pantalla", "Display"));
    GameFlag("fullscreen", T("Pantalla completa", "Fullscreen"), false);
    GameFlag("show_fps", T("Contador de FPS en la esquina", "FPS counter in the corner"), false);
    {
        std::scoped_lock lock{a.mutex};
        const std::string current = a.launcher.Get("display");
        std::string preview = T("Principal", "Primary");
        for (const auto& [value, label] : a.displays)
            if (value == current) preview = label;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22.0f);
        if (ImGui::BeginCombo(T("Monitor", "Monitor"), preview.c_str())) {
            if (ImGui::Selectable(T("Principal", "Primary"), current.empty())) {
                a.launcher.Set("display", "");
                SaveLauncher();
            }
            for (const auto& [value, label] : a.displays) {
                if (ImGui::Selectable(label.c_str(), value == current)) {
                    a.launcher.Set("display", value);
                    SaveLauncher();
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
    ChoiceCombo(T("Presentación", "Presentation"), "present_mode", kPresentModes, "Mailbox");

    ImGui::SeparatorText(T("Mando", "Controller"));
    {
        std::scoped_lock lock{a.mutex};
        const std::string current = a.launcher.Get("gamepad");
        std::string preview = T("El primero conectado", "First connected");
        bool listed = current.empty();
        for (const auto& [guid, name] : a.gamepads)
            if (guid == current) {
                preview = name;
                listed = true;
            }
        if (!listed) preview = a.launcher.Get("gamepad_name", current) + T(" (no conectado)", " (not connected)");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22.0f);
        if (ImGui::BeginCombo(T("Mando", "Controller"), preview.c_str())) {
            if (ImGui::Selectable(T("El primero conectado", "First connected"), current.empty())) {
                a.launcher.Set("gamepad", "");
                a.launcher.Set("gamepad_name", "");
                SaveLauncher();
            }
            for (const auto& [guid, name] : a.gamepads) {
                ImGui::PushID(guid.c_str());
                if (ImGui::Selectable(name.c_str(), guid == current)) {
                    a.launcher.Set("gamepad", guid);
                    a.launcher.Set("gamepad_name", name);
                    SaveLauncher();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!a.probe_done && a.probe.joinable());
    if (ImGui::Button(T("Buscar mandos", "Refresh"))) {
        if (a.probe.joinable()) a.probe.join();
        StartProbe();
    }
    ImGui::EndDisabled();
    ImGui::TextColored(kGrey, "%s", T("El teclado funciona siempre a la vez. La pantalla dividida se asigna en el menú del juego.",
                                      "The keyboard always works too. Split-screen ports are assigned in the in-game menu."));

    ImGui::SeparatorText(T("Consola", "Console"));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    ChoiceCombo(T("Idioma del juego", "Game language"), "language", kGameLanguages, DefaultGameLanguage());
    LauncherFlag("neo", T("Modo PS4 Pro", "PS4 Pro mode"), false,
                 T("La ruta de render de PS4 Pro (más resolución). Necesario para el parche de 1080p.",
                   "The game's PS4 Pro rendering path (higher resolution). Needed for the 1080p patch."));
}

void ToolsTab() {
    App& a = g_app;
    ImGui::SeparatorText(T("Carpetas y registro", "Folders and log"));
    if (ImGui::Button(T("Abrir last_run.log", "Open last_run.log"))) OpenInShell(a.root / L"last_run.log");
    ImGui::SameLine();
    if (ImGui::Button(T("Abrir la carpeta del port", "Open the port folder"))) OpenInShell(a.root);
    ImGui::SameLine();
    if (ImGui::Button(T("Abrir partidas guardadas", "Open saves"))) {
        std::error_code ec;
        fs::create_directories(a.root / L"user", ec);
        OpenInShell(a.root / L"user");
    }
    LauncherFlag("save_log", T("Guardar registro detallado (logs\\, con estadísticas de fotogramas)",
                               "Save a detailed log (logs\\, with frame statistics)"), false,
                 T("Para reportar fallos o tirones: adjunta el archivo de logs\\ en un issue.",
                   "To report crashes or stutter: attach the file from logs\\ to an issue."));

    ImGui::SeparatorText(T("Caché de shaders", "Shader cache"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kGrey, "%s", T("Los shaders se guardan para que los siguientes arranques vayan fluidos. Bórrala solo si "
                                      "actualizaste el driver de la GPU o ves fallos gráficos.",
                                      "Shaders are kept so later sessions run smoothly. Clear it only after a GPU driver "
                                      "update or if you see graphics glitches."));
    ImGui::PopTextWrapPos();
    ImGui::BeginDisabled(a.game_running);
    if (ImGui::Button(T("Borrar la caché de shaders", "Clear the shader cache"))) a.confirm_clear_cache = true;
    ImGui::EndDisabled();

    ImGui::SeparatorText(T("Idioma del launcher", "Launcher language"));
    if (ImGui::RadioButton("Español", g_spanish)) {
        g_spanish = true;
        SaveLauncher();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("English", !g_spanish)) {
        g_spanish = false;
        SaveLauncher();
    }

    ImGui::SeparatorText(T("Acerca de", "About"));
    ImGui::Text("CTR-NitroFueled-PC %s", a.version.c_str());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(T("Fork de Windows de SacredFire (T3CHNOLOG1C), basado en bbport (deadinside28), el port de Windows "
                             "de Supermedo y shadPS4. Parches de illusion y TroyWarez. Sin afiliación con Sony, Activision "
                             "ni Beenox. Licencia GPL v2 o posterior.",
                             "Windows fork of SacredFire (T3CHNOLOG1C), built on bbport (deadinside28), Supermedo's Windows "
                             "port and shadPS4. Patches by illusion and TroyWarez. Not affiliated with Sony, Activision or "
                             "Beenox. GPL v2 or later."));
    ImGui::PopTextWrapPos();
    ImGui::TextLinkOpenURL(kRepoUrl, kRepoUrl);
    if (a.python) ImGui::TextColored(kGrey, "Python: %s", a.python->description.c_str());
}

void ClearShaderCache() {
    std::error_code ec;
    const fs::path cache = g_app.root / L"user" / L"cache" / L"CUSA13795";
    const auto removed = fs::remove_all(cache, ec);
    g_app.notice = ec ? std::string(T("No se pudo borrar la caché: ", "Could not clear the cache: ")) + ec.message()
                      : std::string(T("Caché borrada (", "Cache cleared (")) + std::to_string(removed) +
                            T(" archivos).", " files).");
}

void Frame() {
    App& a = g_app;
    PollJobs();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("launcher", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.6f);
    ImGui::TextUnformatted("CTR Nitro-Fueled PC");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(kGrey, "%s", a.version.c_str());

    if (!a.notice.empty()) {
        ImGui::TextColored(kYellow, "%s", a.notice.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("OK")) a.notice.clear();
    }

    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem(T("Jugar", "Play"))) {
            PlayTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(T("Ajustes", "Settings"))) {
            ImGui::BeginChild("settings");
            SettingsTab();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(T("Herramientas", "Tools"))) {
            ToolsTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (a.confirm_clear_cache) {
        ImGui::OpenPopup("clear_cache");
        a.confirm_clear_cache = false;
    }
    if (ImGui::BeginPopupModal("clear_cache", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::TextUnformatted(T("¿Borrar la caché de shaders? El próximo arranque volverá a compilarlos.",
                                 "Clear the shader cache? The next start compiles them again."));
        if (ImGui::Button(T("Borrar", "Clear"))) {
            ClearShaderCache();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(T("Cancelar", "Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (a.want_close) {
        ImGui::OpenPopup("close");
        a.want_close = false;
    }
    if (ImGui::BeginPopupModal("close", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::TextUnformatted(T("El juego sigue abierto.", "The game is still running."));
        if (ImGui::Button(T("Cerrar el juego y el launcher", "Stop the game and quit"))) {
            StopGame();
            PostQuitMessage(0);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(T("Cancelar", "Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
}

void LoadState() {
    App& a = g_app;
    a.root = FindRoot();
    std::error_code ec;
    a.bin = fs::is_directory(a.root / L"bin", ec) ? a.root / L"bin" : a.root / L"out";
    for (const fs::path& v : {a.bin / L"VERSION", a.root / L"VERSION", a.root / L"out" / L"VERSION"}) {
        if (fs::is_regular_file(v, ec)) {
            a.version = Trim(ReadText(v));
            break;
        }
    }
    a.python = FindPython(a.root);
    a.launcher.Load(LauncherIni());
    a.game.Load(GameIni());
    const std::string lang = a.launcher.Get("ui_language");
    g_spanish = lang.empty() ? PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_SPANISH : lang == "es";
    a.game_dir = TextPath(a.launcher.Get("game_dir"));
    a.base_dir = TextPath(a.launcher.Get("base_dir"));
    a.update_dir = TextPath(a.launcher.Get("update_dir"));
    // First start of a portable package: a merged folder next to the launcher is picked up.
    if (a.game_dir.empty()) {
        for (const wchar_t* name : {L"CUSA13795-1.21", L"CUSA13795"}) {
            if (Analyze(a.root / name).kind == Kind::Complete) {
                a.game_dir = a.root / name;
                break;
            }
        }
        if (a.base_dir.empty() && Analyze(a.root / L"CUSA13795").kind == Kind::Base) a.base_dir = a.root / L"CUSA13795";
        if (a.update_dir.empty() && Analyze(a.root / L"CUSA13795-patch").kind == Kind::Update)
            a.update_dir = a.root / L"CUSA13795-patch";
    }
    a.base_info = Analyze(a.base_dir);
    a.update_info = Analyze(a.update_dir);
    StartCheck();
    StartProbe();
}

// ---------------------------------------------------------------------------------------------
// Win32 + Direct3D 11 (Dear ImGui's example_win32_directx11)

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapchain = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
UINT g_resize_w = 0, g_resize_h = 0;
bool g_occluded = false;

void CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    g_swapchain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}

void CleanupRenderTarget() {
    if (g_rtv) {
        g_rtv->Release();
        g_rtv = nullptr;
    }
}

bool CreateDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL level;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                               &sd, &g_swapchain, &g_device, &level, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd,
                                           &g_swapchain, &g_device, &level, &g_context);
    if (hr != S_OK) return false;
    CreateRenderTarget();
    return true;
}

void CleanupDevice() {
    CleanupRenderTarget();
    if (g_swapchain) g_swapchain->Release();
    if (g_context) g_context->Release();
    if (g_device) g_device->Release();
    g_swapchain = nullptr;
    g_context = nullptr;
    g_device = nullptr;
}

} // namespace

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wparam == SIZE_MINIMIZED) return 0;
        g_resize_w = LOWORD(lparam);
        g_resize_h = HIWORD(lparam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_CLOSE:
        if (g_app.game_running) {
            g_app.want_close = true;
            ShowWindow(hwnd, SW_RESTORE);
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ImGui_ImplWin32_EnableDpiAwareness();
    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc{sizeof wc, CS_CLASSDC, WndProc, 0, 0, instance, nullptr, nullptr, nullptr, nullptr, L"CtrNitroFueledPcLauncher", nullptr};
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
    const int width = int(820 * scale), height = int(720 * scale);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + std::max(0, int(work.right - work.left - width) / 2);
    const int y = work.top + std::max(0, int(work.bottom - work.top - height) / 2);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"CTR Nitro-Fueled PC", WS_OVERLAPPEDWINDOW, x, y, width, height, nullptr,
                              nullptr, instance, nullptr);
    g_app.hwnd = hwnd;
    if (!CreateDevice(hwnd)) {
        CleanupDevice();
        MessageBoxW(nullptr, L"Direct3D 11 is not available.", L"CTR Nitro-Fueled PC", MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = 17.0f;
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    style.WindowPadding = ImVec2(16 * scale, 14 * scale);
    style.FramePadding = ImVec2(10 * scale, 6 * scale);
    style.ItemSpacing = ImVec2(10 * scale, 8 * scale);
    style.FrameRounding = 4 * scale;
    style.TabRounding = 4 * scale;
    wchar_t fonts[MAX_PATH];
    const UINT n = GetWindowsDirectoryW(fonts, MAX_PATH);
    const fs::path segoe = fs::path(std::wstring(fonts, n)) / L"Fonts" / L"segoeui.ttf";
    std::error_code ec;
    if (fs::is_regular_file(segoe, ec)) io.Fonts->AddFontFromFileTTF(PathText(segoe).c_str());
    else io.Fonts->AddFontDefault();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);
    LoadState();

    bool done = false;
    while (!done) {
        MSG msg;
        // Idle while minimized or not in front: wait for input or 100 ms (background work).
        if (IsIconic(hwnd)) MsgWaitForMultipleObjects(0, nullptr, FALSE, 250, QS_ALLINPUT);
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (IsIconic(hwnd)) {
            PollJobs();
            continue;
        }
        if (g_occluded && g_swapchain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            Sleep(10);
            PollJobs();
            continue;
        }
        g_occluded = false;
        if (g_resize_w && g_resize_h) {
            CleanupRenderTarget();
            g_swapchain->ResizeBuffers(0, g_resize_w, g_resize_h, DXGI_FORMAT_UNKNOWN, 0);
            g_resize_w = g_resize_h = 0;
            CreateRenderTarget();
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        Frame();
        ImGui::Render();
        const float clear[4] = {0.08f, 0.08f, 0.10f, 1.0f};
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_occluded = g_swapchain->Present(1, 0) == DXGI_STATUS_OCCLUDED;
    }

    // Background work finishes on its own; a running game keeps running.
    if (g_app.worker.joinable()) {
        if (g_app.job == Job::Play) g_app.worker.detach();
        else g_app.worker.join();
    }
    if (g_app.probe.joinable()) g_app.probe.join();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDevice();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    CoUninitialize();
    return 0;
}
