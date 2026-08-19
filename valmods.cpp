// ============================================================================
//  ValMods - petit gestionnaire manuel de mods Valheim (Win32 natif, C++)
//  - liste de mods (nom / categorie / lien / derniere verification / note)
//  - bouton "Watch"        : ouvre le lien
//  - bouton "Check update" : ouvre le lien ET horodate la verification
//  - bouton "Verifie"      : horodate sans ouvrir
//  - acces rapide aux dossiers plugins / config / sauvegardes
//  - onglet Sauvegardes : liste des mondes et des personnages + backup
//
//  Aucune dependance externe. Donnees stockees a cote de l'exe :
//      valmods.tsv  (les mods)      valmods.ini  (le chemin du jeu)
// ============================================================================

#define _CRT_SECURE_NO_WARNINGS
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <ctime>

#include "minijson.h"

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

// ---------------------------------------------------------------- identifiants
#define IDC_TAB        1000
#define IDC_MODLIST    1001
#define IDC_BADD       1002
#define IDC_BEDIT      1003
#define IDC_BDEL       1004
#define IDC_BWATCH     1005
#define IDC_BCHECK     1006
#define IDC_BMARK      1007
#define IDC_BCOPY      1008
#define IDC_WORLDS     1010
#define IDC_CHARS      1011
#define IDC_LBL1       1012
#define IDC_LBL2       1013
#define IDC_BREFRESH   1014
#define IDC_BOPENSAVE  1015
#define IDC_BBACKUP    1016
#define IDC_BOPENBK    1017

#define IDM_OPENDATA   2001
#define IDM_EXIT       2002
#define IDM_PLUGINS    2003
#define IDM_CONFIG     2004
#define IDM_SAVES      2005
#define IDM_GAMEDIR    2006
#define IDM_SETDIR     2007
#define IDM_ABOUT      2008

#define VALMODS_VERSION "1.1.0"
#define IDM_BEPINEX    2009

// ---------------------------------------------------------------- donnees
struct Mod {
    std::wstring name, cat, url, last, note;
};

static HINSTANCE g_hInst = NULL;
static HWND  g_hMain = NULL, g_hTab = NULL, g_hMods = NULL;
static HWND  g_hWorlds = NULL, g_hChars = NULL;
static HFONT g_font = NULL;
static std::vector<Mod> g_mods;
static std::wstring g_valheimDir;
static int  g_sortCol = 0;
static bool g_sortAsc = true;
static bool g_lastListIsWorld = true;   // pour le bouton backup

static HWND g_pageMods[8];  static int g_nMods = 0;
static HWND g_pageSaves[8]; static int g_nSaves = 0;

// ---------------------------------------------------------------- utilitaires
static std::string W2U(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}
static std::wstring U2W(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
static std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}
static std::wstring Clean(const std::wstring& s) {   // pas de tab / retour ligne
    std::wstring r = s;
    for (size_t i = 0; i < r.size(); ++i)
        if (r[i] == L'\t' || r[i] == L'\r' || r[i] == L'\n') r[i] = L' ';
    return Trim(r);
}
static bool DirExists(const std::wstring& p) {
    if (p.empty()) return false;
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool FileExists(const std::wstring& p) {
    if (p.empty()) return false;
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring s = buf;
    size_t p = s.find_last_of(L'\\');
    return (p == std::wstring::npos) ? L"." : s.substr(0, p);
}
static std::wstring GetTextOf(HWND h) {
    int n = GetWindowTextLengthW(h);
    if (n <= 0) return L"";
    std::wstring s((size_t)n + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize((size_t)n);
    return s;
}
static void Info(HWND h, const wchar_t* msg) {
    MessageBoxW(h, msg, L"ValMods", MB_OK | MB_ICONINFORMATION);
}
static void MakeDirs(const std::wstring& path) {
    std::wstring cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur += path[i];
        if (path[i] == L'\\' && cur.size() > 3) CreateDirectoryW(cur.c_str(), NULL);
    }
    CreateDirectoryW(path.c_str(), NULL);
}
static void OpenFolder(HWND h, const std::wstring& path, const wchar_t* label) {
    if (!DirExists(path)) {
        std::wstring m = L"Dossier introuvable :\n";
        m += path.empty() ? L"(non defini)" : path;
        m += L"\n\n(";
        m += label;
        m += L")\n\nVerifie le chemin du jeu dans Parametres.";
        MessageBoxW(h, m.c_str(), L"ValMods", MB_OK | MB_ICONWARNING);
        return;
    }
    ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
}
static void RevealFile(const std::wstring& file) {
    std::wstring args = L"/select,\"" + file + L"\"";
    ShellExecuteW(NULL, L"open", L"explorer.exe", args.c_str(), NULL, SW_SHOWNORMAL);
}
static void CopyToClipboard(HWND h, const std::wstring& txt) {
    if (!OpenClipboard(h)) return;
    EmptyClipboard();
    size_t bytes = (txt.size() + 1) * sizeof(wchar_t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (g) {
        void* p = GlobalLock(g);
        memcpy(p, txt.c_str(), bytes);
        GlobalUnlock(g);
        SetClipboardData(CF_UNICODETEXT, g);
    }
    CloseClipboard();
}
static std::wstring ClipboardText() {
    std::wstring r;
    if (!OpenClipboard(NULL)) return r;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        wchar_t* p = (wchar_t*)GlobalLock(h);
        if (p) { r = p; GlobalUnlock(h); }
    }
    CloseClipboard();
    return Clean(r);
}

// --- lecture / ecriture de fichiers via l'API Win32 (chemins Unicode surs) ---
static bool ReadAllBytes(const std::wstring& path, std::string& out) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > (LONGLONG)32 * 1024 * 1024) {
        CloseHandle(h); return false;
    }
    out.resize((size_t)sz.QuadPart);
    DWORD got = 0;
    bool ok = true;
    if (sz.QuadPart > 0)
        ok = (ReadFile(h, &out[0], (DWORD)sz.QuadPart, &got, NULL) != 0) && got == sz.QuadPart;
    CloseHandle(h);
    if (!ok) out.clear();
    return ok;
}
static bool WriteAllBytes(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    bool ok = data.empty() ||
              ((WriteFile(h, data.data(), (DWORD)data.size(), &wrote, NULL) != 0)
               && wrote == data.size());
    CloseHandle(h);
    return ok;
}

// ---------------------------------------------------------------- dates
static std::wstring NowStamp() {
    time_t t = time(NULL);
    struct tm* lt = localtime(&t);
    wchar_t buf[32];
    wsprintfW(buf, L"%04d-%02d-%02d %02d:%02d",
        lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min);
    return buf;
}
// -1 = jamais verifie
static int DaysSince(const std::wstring& stamp) {
    if (stamp.size() < 10) return -1;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    if (swscanf(stamp.c_str(), L"%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) < 3) return -1;
    struct tm a; memset(&a, 0, sizeof(a));
    a.tm_year = y - 1900; a.tm_mon = mo - 1; a.tm_mday = d;
    a.tm_hour = h; a.tm_min = mi; a.tm_isdst = -1;
    time_t t0 = mktime(&a);
    if (t0 == (time_t)-1) return -1;
    double diff = difftime(time(NULL), t0);
    if (diff < 0) diff = 0;
    return (int)(diff / 86400.0);
}
static std::wstring DaysText(const std::wstring& stamp) {
    int d = DaysSince(stamp);
    if (d < 0) return L"-";
    wchar_t b[24]; wsprintfW(b, L"%d j", d);
    return b;
}
static std::wstring FileTimeText(const FILETIME& ft) {
    FILETIME lf; SYSTEMTIME st;
    FileTimeToLocalFileTime(&ft, &lf);
    FileTimeToSystemTime(&lf, &st);
    wchar_t b[40];
    wsprintfW(b, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return b;
}

// ---------------------------------------------------------------- chemins jeu
static std::wstring SavesRoot() {
    wchar_t p[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, p)))
        return std::wstring(p) + L"\\AppData\\LocalLow\\IronGate\\Valheim";
    return L"";
}
static std::wstring WorldsDir() {
    std::wstring r = SavesRoot();
    if (r.empty()) return L"";
    if (DirExists(r + L"\\worlds_local")) return r + L"\\worlds_local";
    return r + L"\\worlds";
}
static std::wstring CharsDir() {
    std::wstring r = SavesRoot();
    if (r.empty()) return L"";
    if (DirExists(r + L"\\characters_local")) return r + L"\\characters_local";
    return r + L"\\characters";
}
static std::wstring PluginsDir() { return g_valheimDir.empty() ? L"" : g_valheimDir + L"\\BepInEx\\plugins"; }
static std::wstring ConfigDir()  { return g_valheimDir.empty() ? L"" : g_valheimDir + L"\\BepInEx\\config"; }
static std::wstring BepInExDir() { return g_valheimDir.empty() ? L"" : g_valheimDir + L"\\BepInEx"; }
static std::wstring BackupRoot() { return ExeDir() + L"\\backups"; }

static std::wstring RegSteamPath() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &k) != ERROR_SUCCESS)
        return L"";
    wchar_t buf[MAX_PATH]; DWORD sz = sizeof(buf), type = 0;
    std::wstring res;
    if (RegQueryValueExW(k, L"SteamPath", NULL, &type, (LPBYTE)buf, &sz) == ERROR_SUCCESS && type == REG_SZ)
        res = buf;
    RegCloseKey(k);
    for (size_t i = 0; i < res.size(); ++i) if (res[i] == L'/') res[i] = L'\\';
    return res;
}
// lit libraryfolders.vdf pour trouver les bibliotheques sur d'autres disques
static void SteamLibraries(std::vector<std::wstring>& out) {
    std::wstring steam = RegSteamPath();
    if (!steam.empty()) out.push_back(steam);
    if (steam.empty()) return;
    std::wstring vdf = steam + L"\\steamapps\\libraryfolders.vdf";
    std::string content;
    if (!ReadAllBytes(vdf, content)) return;
    std::istringstream f(content);
    std::string line;
    while (std::getline(f, line)) {
        size_t k = line.find("\"path\"");
        if (k == std::string::npos) continue;
        size_t q1 = line.find('"', k + 6);
        if (q1 == std::string::npos) continue;
        size_t q2 = line.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        std::string raw = line.substr(q1 + 1, q2 - q1 - 1);
        std::string clean;
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '\\') { clean += '\\'; ++i; }
            else clean += raw[i];
        }
        out.push_back(U2W(clean));
    }
}
static std::wstring DetectValheim() {
    std::vector<std::wstring> libs;
    SteamLibraries(libs);
    for (size_t i = 0; i < libs.size(); ++i) {
        std::wstring c = libs[i] + L"\\steamapps\\common\\Valheim";
        if (DirExists(c)) return c;
    }
    const wchar_t* fixed[] = {
        L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Valheim",
        L"C:\\Program Files\\Steam\\steamapps\\common\\Valheim",
        L"D:\\Steam\\steamapps\\common\\Valheim",
        L"D:\\SteamLibrary\\steamapps\\common\\Valheim",
        L"E:\\SteamLibrary\\steamapps\\common\\Valheim"
    };
    for (int i = 0; i < 5; ++i) if (DirExists(fixed[i])) return fixed[i];
    return L"";
}

// ---------------------------------------------------------------- persistance
// Tout tient dans un seul fichier lisible/editable a la main, a cote de l'exe :
//
//   valmods.json
//   {
//     "version": 1,
//     "valheimDir": "D:\\SteamLibrary\\steamapps\\common\\Valheim",
//     "mods": [
//       { "name": "...", "category": "...", "url": "...",
//         "lastCheck": "2026-08-19 14:30", "note": "..." }
//     ]
//   }
static std::wstring DataFile()   { return ExeDir() + L"\\valmods.json"; }
static std::wstring LegacyFile() { return ExeDir() + L"\\valmods.tsv"; }

static void SaveMods() {
    std::string out;
    out += "{\n";
    out += "  \"version\": 1,\n";
    out += "  \"valheimDir\": " + mj::quote(W2U(g_valheimDir)) + ",\n";
    out += "  \"mods\": [\n";
    for (size_t i = 0; i < g_mods.size(); ++i) {
        const Mod& m = g_mods[i];
        out += "    {\n";
        out += "      \"name\":      " + mj::quote(W2U(m.name)) + ",\n";
        out += "      \"category\":  " + mj::quote(W2U(m.cat))  + ",\n";
        out += "      \"url\":       " + mj::quote(W2U(m.url))  + ",\n";
        out += "      \"lastCheck\": " + mj::quote(W2U(m.last)) + ",\n";
        out += "      \"note\":      " + mj::quote(W2U(m.note)) + "\n";
        out += (i + 1 < g_mods.size()) ? "    },\n" : "    }\n";
    }
    out += "  ]\n";
    out += "}\n";

    // ecriture atomique : on ecrit un .tmp puis on remplace, comme ca une
    // coupure en plein enregistrement ne detruit pas la liste existante.
    std::wstring tmp = DataFile() + L".tmp";
    if (!WriteAllBytes(tmp, out)) return;
    if (!MoveFileExW(tmp.c_str(), DataFile().c_str(), MOVEFILE_REPLACE_EXISTING))
        DeleteFileW(tmp.c_str());
}
static void SaveIni() { SaveMods(); }

// Import de l'ancien format TSV (migration automatique, une seule fois).
static void ImportLegacyTsv(const std::string& raw) {
    std::istringstream f(raw);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (line.empty() || line[0] == '#') continue;
        std::wstring w = U2W(line);
        std::vector<std::wstring> col;
        size_t start = 0;
        for (;;) {
            size_t p = w.find(L'\t', start);
            if (p == std::wstring::npos) { col.push_back(w.substr(start)); break; }
            col.push_back(w.substr(start, p - start));
            start = p + 1;
        }
        while (col.size() < 5) col.push_back(L"");
        Mod m;
        m.name = col[0]; m.cat = col[1]; m.url = col[2]; m.last = col[3]; m.note = col[4];
        if (!m.name.empty() || !m.url.empty()) g_mods.push_back(m);
    }
}

static void LoadData() {
    g_mods.clear();
    g_valheimDir.clear();
    std::string raw;

    if (ReadAllBytes(DataFile(), raw)) {
        mj::Value root;
        if (mj::parse(raw, root) && root.type == mj::OBJ) {
            g_valheimDir = U2W(root.s("valheimDir"));
            const mj::Value* mods = root.find("mods");
            if (mods && mods->type == mj::ARR) {
                for (size_t i = 0; i < mods->arr.size(); ++i) {
                    const mj::Value& v = mods->arr[i];
                    if (v.type != mj::OBJ) continue;
                    Mod m;
                    m.name = Clean(U2W(v.s("name")));
                    m.cat  = Clean(U2W(v.s("category")));
                    m.url  = Clean(U2W(v.s("url")));
                    m.last = Clean(U2W(v.s("lastCheck")));
                    m.note = Clean(U2W(v.s("note")));
                    if (!m.name.empty() || !m.url.empty()) g_mods.push_back(m);
                }
            }
        } else {
            // fichier corrompu : on le met de cote au lieu de l'ecraser
            std::wstring bad = DataFile() + L".bad";
            DeleteFileW(bad.c_str());
            MoveFileW(DataFile().c_str(), bad.c_str());
            MessageBoxW(NULL,
                L"valmods.json est illisible (JSON invalide).\n"
                L"Il a ete renomme en valmods.json.bad et la liste repart a vide.\n"
                L"Tu peux corriger le fichier a la main puis le renommer.",
                L"ValMods", MB_OK | MB_ICONWARNING);
        }
    } else if (ReadAllBytes(LegacyFile(), raw)) {
        ImportLegacyTsv(raw);      // ancienne version -> on migre vers JSON
    }

    if (!DirExists(g_valheimDir)) g_valheimDir = DetectValheim();
    SaveMods();                    // normalise / cree le fichier au premier lancement
}

// ---------------------------------------------------------------- tri
static bool ModLess(const Mod& a, const Mod& b) {
    int r = 0;
    switch (g_sortCol) {
        case 0: r = lstrcmpiW(a.name.c_str(), b.name.c_str()); break;
        case 1: r = lstrcmpiW(a.cat.c_str(), b.cat.c_str()); break;
        case 2:
        case 3: {
            int da = DaysSince(a.last), db = DaysSince(b.last);
            if (da < 0) da = 100000;            // jamais verifie = tout en haut
            if (db < 0) db = 100000;
            r = (da > db) ? 1 : (da < db ? -1 : 0);
            if (r == 0) r = lstrcmpiW(a.name.c_str(), b.name.c_str());
            break;
        }
        case 4: r = lstrcmpiW(a.url.c_str(), b.url.c_str()); break;
        default: r = lstrcmpiW(a.note.c_str(), b.note.c_str()); break;
    }
    return g_sortAsc ? (r < 0) : (r > 0);
}

// ---------------------------------------------------------------- liste mods
static void RefillMods() {
    std::stable_sort(g_mods.begin(), g_mods.end(), ModLess);
    SendMessageW(g_hMods, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_hMods);
    for (size_t i = 0; i < g_mods.size(); ++i) {
        LVITEMW it; memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = (int)i;
        it.pszText = (LPWSTR)g_mods[i].name.c_str();
        it.lParam = (LPARAM)i;
        ListView_InsertItem(g_hMods, &it);
        ListView_SetItemText(g_hMods, (int)i, 1, (LPWSTR)g_mods[i].cat.c_str());
        std::wstring last = g_mods[i].last.empty() ? L"jamais" : g_mods[i].last;
        ListView_SetItemText(g_hMods, (int)i, 2, (LPWSTR)last.c_str());
        std::wstring dt = DaysText(g_mods[i].last);
        ListView_SetItemText(g_hMods, (int)i, 3, (LPWSTR)dt.c_str());
        ListView_SetItemText(g_hMods, (int)i, 4, (LPWSTR)g_mods[i].url.c_str());
        ListView_SetItemText(g_hMods, (int)i, 5, (LPWSTR)g_mods[i].note.c_str());
    }
    SendMessageW(g_hMods, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hMods, NULL, TRUE);
}
static int SelectedMod() { return ListView_GetNextItem(g_hMods, -1, LVNI_SELECTED); }
static void SelectMod(int i) {
    if (i < 0 || i >= (int)g_mods.size()) return;
    ListView_SetItemState(g_hMods, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(g_hMods, i, FALSE);
}

// ---------------------------------------------------------------- editeur mod
struct EditCtx {
    Mod m;
    bool ok;
    HWND hName, hCat, hUrl, hNote;
    EditCtx() : ok(false), hName(0), hCat(0), hUrl(0), hNote(0) {}
};

static HWND MkLabel(HWND p, const wchar_t* t, int x, int y, int w) {
    HWND h = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE,
        x, y, w, 18, p, NULL, g_hInst, NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    return h;
}
static HWND MkEdit(HWND p, const std::wstring& txt, int x, int y, int w) {
    HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", txt.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        x, y, w, 24, p, NULL, g_hInst, NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    return h;
}

static LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    EditCtx* c = (EditCtx*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        c = (EditCtx*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)c);
        const int X = 12, W = 446;
        MkLabel(hwnd, L"Nom du mod *", X, 10, W);
        c->hName = MkEdit(hwnd, c->m.name, X, 28, W);
        MkLabel(hwnd, L"Categorie / auteur", X, 58, W);
        c->hCat = MkEdit(hwnd, c->m.cat, X, 76, W);
        MkLabel(hwnd, L"Lien (Thunderstore, Nexus, GitHub...) *", X, 106, W);
        c->hUrl = MkEdit(hwnd, c->m.url, X, 124, W);
        MkLabel(hwnd, L"Note (version installee, remarques...)", X, 154, W);
        c->hNote = MkEdit(hwnd, c->m.note, X, 172, W);
        HWND ok = CreateWindowExW(0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            268, 210, 92, 28, hwnd, (HMENU)IDOK, g_hInst, NULL);
        HWND ca = CreateWindowExW(0, L"BUTTON", L"Annuler",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            366, 210, 92, 28, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
        SendMessageW(ok, WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(ca, WM_SETFONT, (WPARAM)g_font, TRUE);
        SetFocus(c->hName);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK && c) {
            std::wstring n = Clean(GetTextOf(c->hName));
            std::wstring u = Clean(GetTextOf(c->hUrl));
            if (n.empty() || u.empty()) {
                MessageBoxW(hwnd, L"Le nom et le lien sont obligatoires.",
                    L"ValMods", MB_OK | MB_ICONWARNING);
                return 0;
            }
            c->m.name = n;
            c->m.cat  = Clean(GetTextOf(c->hCat));
            c->m.url  = u;
            c->m.note = Clean(GetTextOf(c->hNote));
            c->ok = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hwnd); return 0; }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool ShowModEditor(HWND parent, const wchar_t* title, Mod& io) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc; memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = EditProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"ValModsEditor";
        RegisterClassExW(&wc);
        reg = true;
    }
    EditCtx ctx; ctx.m = io;
    RECT r = { 0, 0, 470, 250 };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&r, style, FALSE);
    RECT pr; GetWindowRect(parent, &pr);
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = pr.left + ((pr.right - pr.left) - w) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - h) / 2;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        L"ValModsEditor", title, style, x, y, w, h, parent, NULL, g_hInst, &ctx);
    if (!dlg) return false;
    EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);

    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (ctx.ok) io = ctx.m;
    return ctx.ok;
}

// ---------------------------------------------------------------- sauvegardes
struct SaveEntry { std::wstring name, file; };
static std::vector<SaveEntry> g_worlds, g_chars;

static void FillSaveList(HWND list, const std::wstring& dir, const wchar_t* ext,
                         std::vector<SaveEntry>& out, bool worldSizes)
{
    out.clear();
    ListView_DeleteAllItems(list);
    if (!DirExists(dir)) return;
    std::wstring pat = dir + L"\\*" + ext;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    int i = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring fn = fd.cFileName;
        size_t dot = fn.find_last_of(L'.');
        std::wstring base = (dot == std::wstring::npos) ? fn : fn.substr(0, dot);

        SaveEntry e; e.name = base; e.file = dir + L"\\" + fn;
        out.push_back(e);

        LVITEMW it; memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = i;
        it.pszText = (LPWSTR)out[out.size() - 1].name.c_str();
        it.lParam = (LPARAM)i;
        ListView_InsertItem(list, &it);

        // taille : pour un monde on prend le .db (le gros fichier)
        ULONGLONG size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        FILETIME ft = fd.ftLastWriteTime;
        if (worldSizes) {
            std::wstring db = dir + L"\\" + base + L".db";
            WIN32_FILE_ATTRIBUTE_DATA ad;
            if (GetFileAttributesExW(db.c_str(), GetFileExInfoStandard, &ad)) {
                size = ((ULONGLONG)ad.nFileSizeHigh << 32) | ad.nFileSizeLow;
                ft = ad.ftLastWriteTime;
            }
        }
        wchar_t sz[40];
        wsprintfW(sz, L"%lu Ko", (unsigned long)(size / 1024));
        ListView_SetItemText(list, i, 1, sz);
        std::wstring dt = FileTimeText(ft);
        ListView_SetItemText(list, i, 2, (LPWSTR)dt.c_str());
        ++i;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static void RefreshSaves() {
    FillSaveList(g_hWorlds, WorldsDir(), L".fwl", g_worlds, true);
    FillSaveList(g_hChars,  CharsDir(),  L".fch", g_chars,  false);
}

static void BackupSelection(HWND hwnd) {
    HWND list = g_lastListIsWorld ? g_hWorlds : g_hChars;
    std::vector<SaveEntry>& vec = g_lastListIsWorld ? g_worlds : g_chars;
    std::wstring dir = g_lastListIsWorld ? WorldsDir() : CharsDir();
    int sel = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= (int)vec.size()) {
        Info(hwnd, L"Selectionne d'abord un monde ou un personnage.");
        return;
    }
    std::wstring name = vec[sel].name;
    std::wstring stamp = NowStamp();
    for (size_t i = 0; i < stamp.size(); ++i)
        if (stamp[i] == L':' || stamp[i] == L' ') stamp[i] = L'-';
    std::wstring dest = BackupRoot() + L"\\" +
        (g_lastListIsWorld ? L"mondes\\" : L"personnages\\") + name + L"_" + stamp;
    MakeDirs(dest);

    int n = 0;
    std::wstring pat = dir + L"\\" + name + L".*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring src = dir + L"\\" + fd.cFileName;
            std::wstring dst = dest + L"\\" + fd.cFileName;
            if (CopyFileW(src.c_str(), dst.c_str(), FALSE)) ++n;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    wchar_t msg[512];
    wsprintfW(msg, L"%d fichier(s) copie(s) dans :\n%s", n, dest.c_str());
    Info(hwnd, msg);
}

// ---------------------------------------------------------------- actions mod
static void ActionOpen(HWND hwnd, bool stamp) {
    int i = SelectedMod();
    if (i < 0) { Info(hwnd, L"Selectionne un mod dans la liste."); return; }
    std::wstring url = g_mods[i].url;
    std::wstring name = g_mods[i].name;
    if (url.empty()) { Info(hwnd, L"Ce mod n'a pas de lien."); return; }
    ShellExecuteW(NULL, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    if (stamp) {
        g_mods[i].last = NowStamp();
        SaveMods();
        RefillMods();
        for (size_t k = 0; k < g_mods.size(); ++k)
            if (g_mods[k].name == name) { SelectMod((int)k); break; }
    }
}
static void ActionMark(HWND hwnd) {
    int i = SelectedMod();
    if (i < 0) { Info(hwnd, L"Selectionne un mod dans la liste."); return; }
    std::wstring name = g_mods[i].name;
    g_mods[i].last = NowStamp();
    SaveMods();
    RefillMods();
    for (size_t k = 0; k < g_mods.size(); ++k)
        if (g_mods[k].name == name) { SelectMod((int)k); break; }
}
static void ActionAdd(HWND hwnd) {
    Mod m;
    std::wstring clip = ClipboardText();
    if (clip.compare(0, 4, L"http") == 0 && clip.size() < 400) m.url = clip;
    if (ShowModEditor(hwnd, L"Ajouter un mod", m)) {
        g_mods.push_back(m);
        SaveMods();
        RefillMods();
        for (size_t k = 0; k < g_mods.size(); ++k)
            if (g_mods[k].name == m.name) { SelectMod((int)k); break; }
    }
}
static void ActionEdit(HWND hwnd) {
    int i = SelectedMod();
    if (i < 0) { Info(hwnd, L"Selectionne un mod dans la liste."); return; }
    Mod m = g_mods[i];
    if (ShowModEditor(hwnd, L"Modifier le mod", m)) {
        g_mods[i] = m;
        SaveMods();
        RefillMods();
        for (size_t k = 0; k < g_mods.size(); ++k)
            if (g_mods[k].name == m.name) { SelectMod((int)k); break; }
    }
}
static void ActionDelete(HWND hwnd) {
    int i = SelectedMod();
    if (i < 0) { Info(hwnd, L"Selectionne un mod dans la liste."); return; }
    std::wstring q = L"Supprimer \"" + g_mods[i].name + L"\" de la liste ?\n"
                     L"(le mod n'est pas desinstalle, seule la fiche est supprimee)";
    if (MessageBoxW(hwnd, q.c_str(), L"ValMods", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    g_mods.erase(g_mods.begin() + i);
    SaveMods();
    RefillMods();
}
static void ActionCopy(HWND hwnd) {
    int i = SelectedMod();
    if (i < 0) { Info(hwnd, L"Selectionne un mod dans la liste."); return; }
    CopyToClipboard(hwnd, g_mods[i].url);
}

// ---------------------------------------------------------------- parametres
static int CALLBACK BrowseCB(HWND hwnd, UINT msg, LPARAM lp, LPARAM data) {
    if (msg == BFFM_INITIALIZED && data)
        SendMessageW(hwnd, BFFM_SETSELECTION, TRUE, data);
    return 0;
}
static void ChooseValheimDir(HWND hwnd) {
    BROWSEINFOW bi; memset(&bi, 0, sizeof(bi));
    wchar_t path[MAX_PATH] = L"";
    bi.hwndOwner = hwnd;
    bi.lpszTitle = L"Selectionne le dossier d'installation de Valheim (celui qui contient valheim.exe)";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = BrowseCB;
    bi.lParam = g_valheimDir.empty() ? 0 : (LPARAM)g_valheimDir.c_str();
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    if (SHGetPathFromIDListW(pidl, path)) {
        g_valheimDir = path;
        SaveIni();
        if (!FileExists(g_valheimDir + L"\\valheim.exe"))
            Info(hwnd, L"Attention : valheim.exe n'a pas ete trouve dans ce dossier.\n"
                       L"Le chemin est quand meme enregistre.");
    }
    CoTaskMemFree(pidl);
}

// ---------------------------------------------------------------- interface
static void ShowPage(int idx) {
    for (int i = 0; i < g_nMods; ++i)  ShowWindow(g_pageMods[i],  idx == 0 ? SW_SHOW : SW_HIDE);
    for (int i = 0; i < g_nSaves; ++i) ShowWindow(g_pageSaves[i], idx == 1 ? SW_SHOW : SW_HIDE);
    if (idx == 1) RefreshSaves();
}

static void LayoutAll(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    MoveWindow(g_hTab, 8, 8, W - 16, H - 16, TRUE);

    RECT pr = { 8, 8, W - 8, H - 8 };
    TabCtrl_AdjustRect(g_hTab, FALSE, &pr);
    int px = pr.left + 4, py = pr.top + 4;
    int pw = (pr.right - pr.left) - 8, ph = (pr.bottom - pr.top) - 8;
    if (pw < 100) pw = 100;
    if (ph < 100) ph = 100;

    // --- onglet mods : rangee de boutons + liste
    const int bw = 104, bh = 28, gap = 6;
    int ids[7] = { IDC_BADD, IDC_BEDIT, IDC_BDEL, IDC_BWATCH, IDC_BCHECK, IDC_BMARK, IDC_BCOPY };
    for (int i = 0; i < 7; ++i) {
        HWND b = GetDlgItem(hwnd, ids[i]);
        if (b) MoveWindow(b, px + i * (bw + gap), py, bw, bh, TRUE);
    }
    MoveWindow(g_hMods, px, py + bh + 8, pw, ph - bh - 8, TRUE);

    // --- onglet sauvegardes
    int lh = (ph - 84) / 2;
    if (lh < 60) lh = 60;
    int y = py;
    MoveWindow(GetDlgItem(hwnd, IDC_LBL1), px, y, pw, 18, TRUE); y += 20;
    MoveWindow(g_hWorlds, px, y, pw, lh, TRUE);                  y += lh + 8;
    MoveWindow(GetDlgItem(hwnd, IDC_LBL2), px, y, pw, 18, TRUE); y += 20;
    MoveWindow(g_hChars, px, y, pw, lh, TRUE);                   y += lh + 8;
    int sids[4] = { IDC_BREFRESH, IDC_BOPENSAVE, IDC_BBACKUP, IDC_BOPENBK };
    for (int i = 0; i < 4; ++i) {
        HWND b = GetDlgItem(hwnd, sids[i]);
        if (b) MoveWindow(b, px + i * (140 + gap), y, 140, bh, TRUE);
    }
}

static HWND MkButton(HWND p, int id, const wchar_t* txt) {
    HWND h = CreateWindowExW(0, L"BUTTON", txt,
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10,
        p, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    return h;
}
static HWND MkList(HWND p, int id) {
    HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 10, 10, p, (HMENU)(INT_PTR)id, g_hInst, NULL);
    ListView_SetExtendedListViewStyle(h,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    return h;
}
static void AddCol(HWND list, int i, const wchar_t* txt, int w) {
    LVCOLUMNW c; memset(&c, 0, sizeof(c));
    c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    c.iSubItem = i;
    c.pszText = (LPWSTR)txt;
    c.cx = w;
    ListView_InsertColumn(list, i, &c);
}

static void BuildMenu(HWND hwnd) {
    HMENU bar = CreateMenu();
    HMENU f = CreatePopupMenu();
    AppendMenuW(f, MF_STRING, IDM_OPENDATA, L"Ouvrir le dossier de donnees\tvalmods.tsv");
    AppendMenuW(f, MF_SEPARATOR, 0, NULL);
    AppendMenuW(f, MF_STRING, IDM_EXIT, L"Quitter");
    HMENU d = CreatePopupMenu();
    AppendMenuW(d, MF_STRING, IDM_PLUGINS, L"BepInEx\\plugins");
    AppendMenuW(d, MF_STRING, IDM_CONFIG,  L"BepInEx\\config");
    AppendMenuW(d, MF_STRING, IDM_BEPINEX, L"BepInEx");
    AppendMenuW(d, MF_STRING, IDM_GAMEDIR, L"Dossier du jeu");
    AppendMenuW(d, MF_SEPARATOR, 0, NULL);
    AppendMenuW(d, MF_STRING, IDM_SAVES,   L"Sauvegardes (LocalLow)");
    HMENU p = CreatePopupMenu();
    AppendMenuW(p, MF_STRING, IDM_SETDIR, L"Definir le dossier de Valheim...");
    HMENU a = CreatePopupMenu();
    AppendMenuW(a, MF_STRING, IDM_ABOUT, L"A propos");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)f, L"Fichier");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)d, L"Dossiers");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)p, L"Parametres");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)a, L"?");
    SetMenu(hwnd, bar);
}

static void UpdateTitle() {
    std::wstring t = L"ValMods - ";
    wchar_t n[32]; wsprintfW(n, L"%d mod(s)", (int)g_mods.size());
    t += n;
    if (!g_valheimDir.empty()) t += L"  |  " + g_valheimDir;
    else t += L"  |  dossier Valheim non defini";
    SetWindowTextW(g_hMain, t.c_str());
}

// ---------------------------------------------------------------- proc princ
static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hMain = hwnd;
        BuildMenu(hwnd);

        g_hTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 10, 10,
            hwnd, (HMENU)IDC_TAB, g_hInst, NULL);
        SendMessageW(g_hTab, WM_SETFONT, (WPARAM)g_font, TRUE);
        TCITEMW ti; memset(&ti, 0, sizeof(ti));
        ti.mask = TCIF_TEXT;
        ti.pszText = (LPWSTR)L"Mods";
        TabCtrl_InsertItem(g_hTab, 0, &ti);
        ti.pszText = (LPWSTR)L"Sauvegardes";
        TabCtrl_InsertItem(g_hTab, 1, &ti);

        // page mods
        g_nMods = 0;
        g_pageMods[g_nMods++] = MkButton(hwnd, IDC_BADD,   L"Ajouter");
        g_pageMods[g_nMods++] = MkButton(hwnd, IDC_BEDIT,  L"Modifier");
        g_pageMods[g_nMods++] = MkButton(hwnd, IDC_BDEL,   L"Supprimer");
        g_pageMods[g_nMods++] = MkButton(hwnd, IDC_BWATCH, L"Watch");
        g_pageMods[g_nMods++] = MkButton(hwnd, IDC_BCHECK, L"Check update");
        g_pageMods[g_nMods++] = MkButton(hwnd, IDC_BMARK,  L"Verifie");
        g_pageMods[g_nMods++] = MkButton(hwnd, IDC_BCOPY,  L"Copier lien");
        g_hMods = MkList(hwnd, IDC_MODLIST);
        g_pageMods[g_nMods++] = g_hMods;
        AddCol(g_hMods, 0, L"Mod", 210);
        AddCol(g_hMods, 1, L"Categorie", 130);
        AddCol(g_hMods, 2, L"Derniere verif", 130);
        AddCol(g_hMods, 3, L"Age", 60);
        AddCol(g_hMods, 4, L"Lien", 320);
        AddCol(g_hMods, 5, L"Note", 180);

        // page sauvegardes
        g_nSaves = 0;
        HWND l1 = CreateWindowExW(0, L"STATIC", L"Mondes", WS_CHILD, 0, 0, 10, 10,
            hwnd, (HMENU)IDC_LBL1, g_hInst, NULL);
        SendMessageW(l1, WM_SETFONT, (WPARAM)g_font, TRUE);
        g_pageSaves[g_nSaves++] = l1;
        g_hWorlds = MkList(hwnd, IDC_WORLDS);
        g_pageSaves[g_nSaves++] = g_hWorlds;
        AddCol(g_hWorlds, 0, L"Monde", 300);
        AddCol(g_hWorlds, 1, L"Taille", 100);
        AddCol(g_hWorlds, 2, L"Modifie le", 160);

        HWND l2 = CreateWindowExW(0, L"STATIC", L"Personnages", WS_CHILD, 0, 0, 10, 10,
            hwnd, (HMENU)IDC_LBL2, g_hInst, NULL);
        SendMessageW(l2, WM_SETFONT, (WPARAM)g_font, TRUE);
        g_pageSaves[g_nSaves++] = l2;
        g_hChars = MkList(hwnd, IDC_CHARS);
        g_pageSaves[g_nSaves++] = g_hChars;
        AddCol(g_hChars, 0, L"Personnage", 300);
        AddCol(g_hChars, 1, L"Taille", 100);
        AddCol(g_hChars, 2, L"Modifie le", 160);

        g_pageSaves[g_nSaves++] = MkButton(hwnd, IDC_BREFRESH,  L"Rafraichir");
        g_pageSaves[g_nSaves++] = MkButton(hwnd, IDC_BOPENSAVE, L"Ouvrir les saves");
        g_pageSaves[g_nSaves++] = MkButton(hwnd, IDC_BBACKUP,   L"Backup selection");
        g_pageSaves[g_nSaves++] = MkButton(hwnd, IDC_BOPENBK,   L"Ouvrir les backups");

        LoadData();
        RefillMods();
        ShowPage(0);
        UpdateTitle();
        return 0;
    }

    case WM_SIZE:
        LayoutAll(hwnd);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize.x = 820;
        mm->ptMinTrackSize.y = 480;
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BADD:   ActionAdd(hwnd);          return 0;
        case IDC_BEDIT:  ActionEdit(hwnd);         return 0;
        case IDC_BDEL:   ActionDelete(hwnd);       return 0;
        case IDC_BWATCH: ActionOpen(hwnd, false);  return 0;
        case IDC_BCHECK: ActionOpen(hwnd, true);   return 0;
        case IDC_BMARK:  ActionMark(hwnd);         return 0;
        case IDC_BCOPY:  ActionCopy(hwnd);         return 0;

        case IDC_BREFRESH:  RefreshSaves(); return 0;
        case IDC_BOPENSAVE: OpenFolder(hwnd, SavesRoot(), L"sauvegardes"); return 0;
        case IDC_BBACKUP:   BackupSelection(hwnd); return 0;
        case IDC_BOPENBK:   MakeDirs(BackupRoot());
                            OpenFolder(hwnd, BackupRoot(), L"backups"); return 0;

        case IDM_PLUGINS:  OpenFolder(hwnd, PluginsDir(), L"BepInEx\\plugins"); return 0;
        case IDM_CONFIG:   OpenFolder(hwnd, ConfigDir(),  L"BepInEx\\config");  return 0;
        case IDM_BEPINEX:  OpenFolder(hwnd, BepInExDir(), L"BepInEx");          return 0;
        case IDM_GAMEDIR:  OpenFolder(hwnd, g_valheimDir, L"dossier du jeu");   return 0;
        case IDM_SAVES:    OpenFolder(hwnd, SavesRoot(),  L"sauvegardes");      return 0;
        case IDM_OPENDATA: OpenFolder(hwnd, ExeDir(),     L"donnees");          return 0;
        case IDM_SETDIR:   ChooseValheimDir(hwnd); UpdateTitle(); return 0;
        case IDM_EXIT:     DestroyWindow(hwnd); return 0;
        case IDM_ABOUT: {
            std::wstring about =
                L"ValMods " + U2W(VALMODS_VERSION) + L" - gestionnaire manuel de mods Valheim\n\n";
            about +=
                L"Watch          : ouvre le lien\n"
                L"Check update   : ouvre le lien + horodate la verification\n"
                L"Verifie        : horodate sans ouvrir\n"
                L"Double-clic    : ouvre le lien\n"
                L"Clic sur un en-tete de colonne : tri\n\n"
                L"Les mods non verifies depuis plus de 30 jours sont en rouge,\n"
                L"ceux jamais verifies en gris.\n\n"
                L"Donnees : valmods.json, a cote de l'exe.";
            Info(hwnd, about.c_str());
            return 0;
        }
        }
        break;

    case WM_NOTIFY: {
        NMHDR* nh = (NMHDR*)lp;
        if (nh->hwndFrom == g_hTab && nh->code == TCN_SELCHANGE) {
            ShowPage(TabCtrl_GetCurSel(g_hTab));
            return 0;
        }
        if (nh->hwndFrom == g_hMods) {
            if (nh->code == NM_DBLCLK) { ActionOpen(hwnd, false); return 0; }
            if (nh->code == LVN_COLUMNCLICK) {
                NMLISTVIEW* nv = (NMLISTVIEW*)lp;
                if (nv->iSubItem == g_sortCol) g_sortAsc = !g_sortAsc;
                else { g_sortCol = nv->iSubItem; g_sortAsc = true; }
                RefillMods();
                return 0;
            }
            if (nh->code == NM_CUSTOMDRAW) {
                LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lp;
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT)
                    return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    size_t idx = (size_t)cd->nmcd.lItemlParam;
                    if (idx < g_mods.size()) {
                        int d = DaysSince(g_mods[idx].last);
                        if (d < 0)        cd->clrText = RGB(130, 130, 130);
                        else if (d >= 30) cd->clrText = RGB(200, 40, 40);
                        else if (d >= 14) cd->clrText = RGB(190, 120, 0);
                    }
                    return CDRF_DODEFAULT;
                }
            }
        }
        if (nh->hwndFrom == g_hWorlds || nh->hwndFrom == g_hChars) {
            if (nh->code == NM_CLICK || nh->code == NM_SETFOCUS)
                g_lastListIsWorld = (nh->hwndFrom == g_hWorlds);
            if (nh->code == NM_DBLCLK) {
                bool w = (nh->hwndFrom == g_hWorlds);
                std::vector<SaveEntry>& v = w ? g_worlds : g_chars;
                int sel = ListView_GetNextItem(nh->hwndFrom, -1, LVNI_SELECTED);
                if (sel >= 0 && sel < (int)v.size()) RevealFile(v[sel].file);
                return 0;
            }
        }
        break;
    }

    case WM_DESTROY:
        SaveMods();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// GUI app (/SUBSYSTEM:WINDOWS) has no console by default, so plain printf
// goes nowhere. --version reattaches to the parent console (if the exe was
// launched from one, e.g. CI or a terminal) so the CI smoke test - and
// anyone running `valmods.exe --version` from a shell - gets real output,
// then exits immediately without ever creating a window.
static bool HandleVersionFlag() {
    LPWSTR cmd = GetCommandLineW();
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmd, &argc);
    bool wantsVersion = false;
    for (int i = 1; i < argc; ++i)
        if (lstrcmpiW(argv[i], L"--version") == 0 || lstrcmpiW(argv[i], L"-v") == 0)
            wantsVersion = true;
    if (argv) LocalFree(argv);
    if (!wantsVersion) return false;

    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f = NULL;
        freopen_s(&f, "CONOUT$", "w", stdout);
        printf("ValMods %s\n", VALMODS_VERSION);
        fflush(stdout);
    }
    return true;
}

// ---------------------------------------------------------------- WinMain
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    if (HandleVersionFlag()) return 0;

    g_hInst = hInst;
    OleInitialize(NULL);

    INITCOMMONCONTROLSEX ic;
    ic.dwSize = sizeof(ic);
    ic.dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&ic);

    NONCLIENTMETRICSW ncm;
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    if (!g_font) g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    WNDCLASSEXW wc; memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"ValModsMain";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(0, L"ValModsMain", L"ValMods",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1060, 640,
        NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    HACCEL hAcc = NULL;
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    (void)hAcc;
    OleUninitialize();
    return (int)msg.wParam;
}
