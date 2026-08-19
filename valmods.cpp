// ============================================================================
//  ValMods - petit gestionnaire manuel de mods Valheim (Win32 natif, C++)
//  - liste de mods (icone / nom / categorie / lien / historique / DLL lie /
//    derniere verification / note)
//  - bouton "Watch"        : ouvre le lien du mod
//  - bouton "Historique"   : ouvre la page des changements / versions
//  - bouton "Check update" : ouvre le lien ET horodate la verification
//  - bouton "Verifie"      : horodate SANS ouvrir (deja verifie ailleurs)
//  - clic droit sur un mod : menu avec toutes les actions + DLL
//  - acces rapide aux dossiers plugins / config / sauvegardes
//  - onglet Sauvegardes : liste des mondes et des personnages + backup
//
//  Aucune dependance externe : GDI+ (icones) et Common Dialogs (parcourir)
//  sont livres avec Windows. Donnees stockees a cote de l'exe :
//      valmods.json
// ============================================================================

#define _CRT_SECURE_NO_WARNINGS
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX            // gdiplus.h utilise std::min/max ; les macros
                             // min/max de windows.h les masqueraient sinon.
#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>         // GetOpenFileNameW (parcourir un DLL / une icone)
#include <shlobj.h>
#include <shellapi.h>
#include <winver.h>          // GetFileVersionInfoW / VerQueryValueW (version DLL)
#include <winhttp.h>          // appels HTTPS vers l'API Thunderstore (verif de version)
#include <objidl.h>
#include <gdiplus.h>          // decode PNG/JPG/BMP/ICO/GIF pour les icones de mod
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <sstream>
#include <cstdlib>
#include <ctime>

#include "minijson.h"

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "winhttp.lib")
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
#define IDC_BHIST      1009
#define IDC_BTSCHECK   1018
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
#define IDM_BEPINEX    2009

// menu contextuel (clic droit) sur une ligne de la liste des mods
#define IDM_CTX_WATCH       2100
#define IDM_CTX_HIST        2101
#define IDM_CTX_CHECK       2102
#define IDM_CTX_MARK        2103
#define IDM_CTX_LOCATE_DLL  2104
#define IDM_CTX_OPEN_DLLDIR 2105
#define IDM_CTX_COPY        2106
#define IDM_CTX_EDIT        2107
#define IDM_CTX_DELETE      2108
#define IDM_CTX_TSCHECK     2109

// boutons propres a la fenetre d'edition d'un mod
#define IDC_E_BROWSEDLL   3001
#define IDC_E_BROWSEICON  3002
#define IDC_E_CLEARICON   3003
#define IDC_E_AUTOFILL    3004

#define VALMODS_VERSION "1.2.0"

// indices des colonnes de la liste des mods (utilises pour le tri et le
// dessin personnalise des colonnes DLL / MAJ Thunderstore)
enum { COL_NAME = 0, COL_CAT = 1, COL_LASTCHECK = 2, COL_AGE = 3,
       COL_DLL = 4, COL_TSVER = 5, COL_URL = 6, COL_NOTE = 7 };

// ---------------------------------------------------------------- donnees
struct Mod {
    std::wstring name, cat, url, changelogUrl, dllPath, iconPath, tsVersion, last, note;
};

static HINSTANCE g_hInst = NULL;
static HWND  g_hMain = NULL, g_hTab = NULL, g_hMods = NULL;
static HWND  g_hWorlds = NULL, g_hChars = NULL;
static HWND  g_hTooltip = NULL;
static HFONT g_font = NULL;
static std::vector<Mod> g_mods;
static std::wstring g_valheimDir;
static int  g_sortCol = 0;
static bool g_sortAsc = true;
static bool g_lastListIsWorld = true;   // pour le bouton backup

static ULONG_PTR g_gdiplusToken = 0;
static HIMAGELIST g_imgList = NULL;
static int g_defaultIconIdx = -1;
static std::map<std::wstring, int> g_iconCache;   // chemin icone -> index image list

// 8 boutons + la liste sur l'onglet Mods : la taille doit rester >= au
// nombre d'elements pousses dans WM_CREATE, sous peine d'ecrire hors
// tableau (c'etait juste-juste a 8 avant l'ajout du bouton Historique).
static HWND g_pageMods[16];  static int g_nMods = 0;
static HWND g_pageSaves[8];  static int g_nSaves = 0;

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

// ---------------------------------------------------------------- icones (GDI+)
// Chaque mod peut avoir un petit logo, charge depuis un fichier local
// (PNG/JPG/BMP/ICO/GIF) via GDI+. GDI+ est livre avec Windows depuis XP :
// aucune dependance ajoutee, et il decode bien plus de formats que LoadImage
// (qui ne sait lire nativement que BMP/ICO/CUR).
static HICON MakeDefaultIcon(int size) {
    Gdiplus::Bitmap bmp((INT)size, (INT)size, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    Gdiplus::SolidBrush fill(Gdiplus::Color(255, 100, 100, 112));
    Gdiplus::Pen pen(Gdiplus::Color(255, 60, 60, 70), 1.0f);
    float pad = 1.0f, s = (float)size;
    g.FillRectangle(&fill, pad, pad, s - 2 * pad, s - 2 * pad);
    g.DrawRectangle(&pen, pad, pad, s - 2 * pad, s - 2 * pad);
    HICON h = NULL;
    if (bmp.GetHICON(&h) != Gdiplus::Ok) return NULL;
    return h;   // NULL si GDI+ n'a pas pu s'initialiser : les appelants le gerent
}
static HICON LoadScaledIconFromFile(const std::wstring& path, int size) {
    if (path.empty() || !FileExists(path)) return NULL;
    Gdiplus::Bitmap* src = Gdiplus::Bitmap::FromFile(path.c_str(), FALSE);
    if (!src) return NULL;
    if (src->GetLastStatus() != Gdiplus::Ok || src->GetWidth() == 0 || src->GetHeight() == 0) {
        delete src;
        return NULL;
    }
    Gdiplus::Bitmap dest((INT)size, (INT)size, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&dest);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    float sw = (float)src->GetWidth(), sh = (float)src->GetHeight();
    float scale = (sw > sh) ? (float)size / sw : (float)size / sh;   // conserve le ratio
    float dw = sw * scale, dh = sh * scale;
    float dx = ((float)size - dw) / 2.0f, dy = ((float)size - dh) / 2.0f;
    g.DrawImage(src, dx, dy, dw, dh);
    delete src;
    HICON h = NULL;
    if (dest.GetHICON(&h) != Gdiplus::Ok) return NULL;
    return h;
}
// Index dans l'image list (16x16) pour ce chemin d'icone, charge et mis en
// cache au premier appel ; un chemin vide ou illisible retombe sur l'icone
// par defaut. Le cache n'est PAS invalide si le fichier change sur disque a
// chemin egal - re-parcourir le fichier dans l'editeur force un rechargement.
static int GetOrLoadIcon(const std::wstring& path) {
    if (path.empty() || !g_imgList) return g_defaultIconIdx;
    std::map<std::wstring, int>::iterator it = g_iconCache.find(path);
    if (it != g_iconCache.end()) return it->second;
    HICON hi = LoadScaledIconFromFile(path, 16);
    int idx = g_defaultIconIdx;
    if (hi) {
        int added = ImageList_AddIcon(g_imgList, hi);
        DestroyIcon(hi);
        if (added >= 0) idx = added;
    }
    g_iconCache[path] = idx;
    return idx;
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
//     "version": 3,
//     "valheimDir": "D:\\SteamLibrary\\steamapps\\common\\Valheim",
//     "mods": [
//       { "name": "...", "category": "...", "url": "...",
//         "changelogUrl": "...", "dllPath": "...", "iconPath": "...",
//         "tsVersion": "1.3.0", "lastCheck": "2026-08-19 14:30", "note": "..." }
//     ]
//   }
static std::wstring DataFile()   { return ExeDir() + L"\\valmods.json"; }
static std::wstring LegacyFile() { return ExeDir() + L"\\valmods.tsv"; }

static void SaveMods() {
    std::string out;
    out += "{\n";
    out += "  \"version\": 3,\n";
    out += "  \"valheimDir\": " + mj::quote(W2U(g_valheimDir)) + ",\n";
    out += "  \"mods\": [\n";
    for (size_t i = 0; i < g_mods.size(); ++i) {
        const Mod& m = g_mods[i];
        out += "    {\n";
        out += "      \"name\":         " + mj::quote(W2U(m.name))         + ",\n";
        out += "      \"category\":     " + mj::quote(W2U(m.cat))          + ",\n";
        out += "      \"url\":          " + mj::quote(W2U(m.url))          + ",\n";
        out += "      \"changelogUrl\": " + mj::quote(W2U(m.changelogUrl)) + ",\n";
        out += "      \"dllPath\":      " + mj::quote(W2U(m.dllPath))      + ",\n";
        out += "      \"iconPath\":     " + mj::quote(W2U(m.iconPath))     + ",\n";
        out += "      \"tsVersion\":    " + mj::quote(W2U(m.tsVersion))    + ",\n";
        out += "      \"lastCheck\":    " + mj::quote(W2U(m.last))         + ",\n";
        out += "      \"note\":         " + mj::quote(W2U(m.note))         + "\n";
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
                    m.name         = Clean(U2W(v.s("name")));
                    m.cat          = Clean(U2W(v.s("category")));
                    m.url          = Clean(U2W(v.s("url")));
                    m.changelogUrl = Clean(U2W(v.s("changelogUrl")));
                    m.dllPath      = Clean(U2W(v.s("dllPath")));
                    m.iconPath     = Clean(U2W(v.s("iconPath")));
                    m.tsVersion    = Clean(U2W(v.s("tsVersion")));
                    m.last         = Clean(U2W(v.s("lastCheck")));
                    m.note         = Clean(U2W(v.s("note")));
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
        case COL_NAME: r = lstrcmpiW(a.name.c_str(), b.name.c_str()); break;
        case COL_CAT:  r = lstrcmpiW(a.cat.c_str(), b.cat.c_str()); break;
        case COL_LASTCHECK:
        case COL_AGE: {
            int da = DaysSince(a.last), db = DaysSince(b.last);
            if (da < 0) da = 100000;            // jamais verifie = tout en haut
            if (db < 0) db = 100000;
            r = (da > db) ? 1 : (da < db ? -1 : 0);
            if (r == 0) r = lstrcmpiW(a.name.c_str(), b.name.c_str());
            break;
        }
        case COL_DLL: r = lstrcmpiW(a.dllPath.c_str(), b.dllPath.c_str()); break;
        case COL_TSVER: r = lstrcmpiW(a.tsVersion.c_str(), b.tsVersion.c_str()); break;
        case COL_URL: r = lstrcmpiW(a.url.c_str(), b.url.c_str()); break;
        default:      r = lstrcmpiW(a.note.c_str(), b.note.c_str()); break;
    }
    return g_sortAsc ? (r < 0) : (r > 0);
}

// ---------------------------------------------------------------- DLL lie
static std::wstring DllFileName(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? path : path.substr(p + 1);
}
static std::wstring GetDllVersionString(const std::wstring& path) {
    DWORD handle = 0;
    DWORD sz = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (!sz) return L"";
    std::vector<BYTE> buf(sz);
    if (!GetFileVersionInfoW(path.c_str(), 0, sz, &buf[0])) return L"";
    VS_FIXEDFILEINFO* ffi = NULL;
    UINT len = 0;
    if (!VerQueryValueW(&buf[0], L"\\", (LPVOID*)&ffi, &len) || !ffi || len == 0) return L"";
    wchar_t out[64];
    wsprintfW(out, L"%u.%u.%u.%u",
        HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
        HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
    return out;
}
// missingOut, si fourni, est mis a true si un DLL est renseigne mais introuvable
static std::wstring DllStatusText(const Mod& m, bool* missingOut) {
    if (missingOut) *missingOut = false;
    if (m.dllPath.empty()) return L"-";
    std::wstring fn = DllFileName(m.dllPath);
    if (!FileExists(m.dllPath)) {
        if (missingOut) *missingOut = true;
        return L"manquant : " + fn;
    }
    std::wstring ver = GetDllVersionString(m.dllPath);
    return ver.empty() ? fn : (fn + L" (v" + ver + L")");
}

// ---------------------------------------------------------------- Thunderstore
// Verification en ligne de la derniere version publiee, via l'API publique
// de Thunderstore (aucune cle requise, en lecture seule) :
//   GET https://thunderstore.io/api/experimental/package/{namespace}/{name}/
// Ne fonctionne QUE pour les mods dont le lien pointe vers thunderstore.io ;
// Nexus/GitHub ne sont pas interroges (pas d'equivalent aussi simple et sans
// cle). L'appel est synchrone (bloque l'interface le temps de la requete,
// quelques secondes au pire vu les timeouts ci-dessous) : c'est deliberement
// simple plutot que threade, puisqu'il est declenche par un clic explicite
// sur un seul mod, jamais automatiquement au demarrage ou sur toute la liste.

// Extrait namespace/nom d'une URL de page Thunderstore, ex:
// https://thunderstore.io/c/valheim/p/Namespace/PackageName/ -> ns=Namespace, name=PackageName
// Accepte aussi les sous-domaines par communaute (.../package/Namespace/PackageName/).
static bool ParseThunderstoreUrl(const std::wstring& url, std::wstring& ns, std::wstring& name) {
    if (url.find(L"thunderstore.io") == std::wstring::npos) return false;
    size_t marker = url.find(L"/p/");
    size_t skip = 3;
    if (marker == std::wstring::npos) { marker = url.find(L"/package/"); skip = 9; }
    if (marker == std::wstring::npos) return false;
    std::wstring rest = url.substr(marker + skip);
    size_t q = rest.find_first_of(L"?#");
    if (q != std::wstring::npos) rest = rest.substr(0, q);
    std::vector<std::wstring> parts;
    size_t start = 0;
    for (size_t i = 0; i <= rest.size(); ++i) {
        if (i == rest.size() || rest[i] == L'/') {
            if (i > start) parts.push_back(rest.substr(start, i - start));
            start = i + 1;
        }
    }
    if (parts.size() < 2) return false;
    ns = parts[0];
    name = parts[1];
    return true;
}
// Compare deux numeros de version "a.b.c[...]" composant par composant
// (numeriquement) ; un composant manquant vaut 0. Suffit pour du SemVer
// simple sans suffixe, seul format accepte par Thunderstore.
static std::vector<int> ParseVersionParts(const std::wstring& v) {
    std::vector<int> parts;
    size_t start = 0;
    for (size_t i = 0; i <= v.size(); ++i) {
        if (i == v.size() || v[i] == L'.') {
            if (i > start) parts.push_back(_wtoi(v.substr(start, i - start).c_str()));
            start = i + 1;
        }
    }
    return parts;
}
static bool VersionLess(const std::wstring& a, const std::wstring& b) {
    std::vector<int> pa = ParseVersionParts(a), pb = ParseVersionParts(b);
    size_t n = (pa.size() > pb.size()) ? pa.size() : pb.size();
    for (size_t i = 0; i < n; ++i) {
        int va = (i < pa.size()) ? pa[i] : 0;
        int vb = (i < pb.size()) ? pb[i] : 0;
        if (va != vb) return va < vb;
    }
    return false;
}

// Requete HTTPS GET minimale via WinHTTP (natif Windows, aucune dependance).
// Malgre le nom historique, ceci lit des octets bruts, pas forcement de
// l'UTF-8 : reutilise a la fois pour le JSON de l'API et pour telecharger
// les icones PNG des mods (voir FetchThunderstoreAutofill plus bas).
// Timeouts volontairement courts (resolution/connexion 5s, envoi/reception 8s)
// pour ne pas bloquer l'interface trop longtemps en cas de reseau absent.
static bool HttpGetBytes(const std::wstring& host, const std::wstring& path,
                         std::string& outBody, DWORD& outStatus, std::wstring& errOut)
{
    outBody.clear(); outStatus = 0; errOut.clear();
    HINTERNET hSession = WinHttpOpen(L"ValMods (Windows)", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { errOut = L"Impossible d'initialiser WinHTTP."; return false; }
    WinHttpSetTimeouts(hSession, 5000, 5000, 8000, 8000);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        errOut = L"Connexion impossible a " + host + L" (verifie ta connexion internet).";
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        errOut = L"Impossible de preparer la requete.";
        WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }

    BOOL ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(hRequest, NULL);
    if (!ok) {
        errOut = L"Pas de reponse du serveur (delai depasse ou pas de connexion).";
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD statusSize = sizeof(outStatus);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &outStatus, &statusSize, WINHTTP_NO_HEADER_INDEX);

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
        std::vector<char> buf(avail);
        DWORD got = 0;
        if (!WinHttpReadData(hRequest, &buf[0], avail, &got)) break;
        outBody.append(&buf[0], got);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return outStatus != 0;
}
// Decoupe une URL https://host/chemin en host + chemin, pour WinHttpConnect
// (qui veut le host separement). Retourne false si ce n'est pas du https.
static bool SplitHttpsUrl(const std::wstring& url, std::wstring& host, std::wstring& path) {
    const std::wstring prefix = L"https://";
    if (url.compare(0, prefix.size(), prefix) != 0) return false;
    size_t rest = prefix.size();
    size_t slash = url.find(L'/', rest);
    if (slash == std::wstring::npos) { host = url.substr(rest); path = L"/"; return !host.empty(); }
    host = url.substr(rest, slash - rest);
    path = url.substr(slash);
    return !host.empty();
}
// Nom de fichier sur : ne garde que [A-Za-z0-9_.-], le reste devient '_'.
static std::wstring SanitizeFileName(const std::wstring& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        bool ok = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                  (c >= L'0' && c <= L'9') || c == L'_' || c == L'-' || c == L'.';
        out += ok ? c : L'_';
    }
    return out.empty() ? L"icon" : out;
}

// Recupere et parse le JSON du package Thunderstore correspondant a l'URL
// d'un mod. Partage par CheckThunderstoreVersion (bouton "Verif. TS") et
// FetchThunderstoreAutofill (bouton "Auto-remplir") pour ne pas dupliquer
// la logique reseau + gestion d'erreurs entre les deux.
struct TsFetchResult {
    bool ok;
    bool isThunderstore;
    std::wstring error;
    mj::Value root;
    TsFetchResult() : ok(false), isThunderstore(false) {}
};
static TsFetchResult FetchThunderstorePackage(const std::wstring& modUrl) {
    TsFetchResult r;
    std::wstring ns, name;
    if (!ParseThunderstoreUrl(modUrl, ns, name)) return r;   // isThunderstore reste false
    r.isThunderstore = true;

    std::wstring path = L"/api/experimental/package/" + ns + L"/" + name + L"/";
    std::string body; DWORD status = 0; std::wstring err;
    if (!HttpGetBytes(L"thunderstore.io", path, body, status, err)) {
        r.error = err.empty() ? L"Echec de la requete." : err;
        return r;
    }
    if (status == 404) {
        r.error = L"Mod introuvable sur Thunderstore (lien casse, mod retire, ou "
                  L"nom/namespace incorrect dans l'URL).";
        return r;
    }
    if (status != 200) {
        wchar_t b[64]; wsprintfW(b, L"Thunderstore a repondu avec le code %lu.", (unsigned long)status);
        r.error = b;
        return r;
    }
    if (!mj::parse(body, r.root) || r.root.type != mj::OBJ) {
        r.error = L"Reponse Thunderstore illisible (format inattendu).";
        return r;
    }
    r.ok = true;
    return r;
}
// Trouve, dans le tableau "versions" du JSON, l'entree ayant le plus haut
// numero de version (le tri renvoye par l'API n'est pas garanti). Renvoie
// NULL si aucune version exploitable n'a ete trouvee.
static const mj::Value* FindLatestVersionEntry(const mj::Value& root, std::wstring& outVersion) {
    outVersion.clear();
    const mj::Value* versions = root.find("versions");
    if (!versions || versions->type != mj::ARR) return NULL;
    const mj::Value* best = NULL;
    for (size_t i = 0; i < versions->arr.size(); ++i) {
        if (versions->arr[i].type != mj::OBJ) continue;
        std::wstring vn = U2W(versions->arr[i].s("version_number"));
        if (vn.empty()) continue;
        if (outVersion.empty() || VersionLess(outVersion, vn)) {
            outVersion = vn;
            best = &versions->arr[i];
        }
    }
    return best;
}

struct TsCheckResult {
    bool ok;              // requete + parsing reussis, latestVersion exploitable
    bool isThunderstore;  // le lien du mod pointait bien vers thunderstore.io
    bool deprecated;
    std::wstring latestVersion;
    std::wstring error;
    TsCheckResult() : ok(false), isThunderstore(false), deprecated(false) {}
};

static TsCheckResult CheckThunderstoreVersion(const std::wstring& modUrl) {
    TsCheckResult r;
    TsFetchResult f = FetchThunderstorePackage(modUrl);
    r.isThunderstore = f.isThunderstore;
    if (!f.isThunderstore) return r;
    if (!f.ok) { r.error = f.error; return r; }

    std::wstring latest;
    if (!FindLatestVersionEntry(f.root, latest)) {
        r.error = L"Aucune version exploitable listee pour ce mod dans la reponse.";
        return r;
    }
    r.latestVersion = latest;

    const mj::Value* dep = f.root.find("is_deprecated");
    r.deprecated = (dep && dep->type == mj::BOOL && dep->b);
    r.ok = true;
    return r;
}

// Recupere nom / categorie / lien historique / derniere version / icone
// depuis Thunderstore pour pre-remplir l'editeur de mod (bouton
// "Auto-remplir"). L'icone est telechargee et enregistree localement dans
// icons/ a cote de l'exe (GDI+ sait ensuite la charger comme n'importe quel
// autre fichier d'icone choisi a la main).
struct TsAutofillResult {
    bool ok;
    bool isThunderstore;
    std::wstring error;
    std::wstring name, category, changelogUrl, latestVersion, localIconPath;
    TsAutofillResult() : ok(false), isThunderstore(false) {}
};
static TsAutofillResult FetchThunderstoreAutofill(const std::wstring& modUrl) {
    TsAutofillResult r;
    TsFetchResult f = FetchThunderstorePackage(modUrl);
    r.isThunderstore = f.isThunderstore;
    if (!f.isThunderstore) return r;
    if (!f.ok) { r.error = f.error; return r; }

    r.name = Clean(U2W(f.root.s("name")));

    const mj::Value* cats = f.root.find("categories");
    if (cats && cats->type == mj::ARR) {
        for (size_t i = 0; i < cats->arr.size(); ++i) {
            if (cats->arr[i].type != mj::STR) continue;
            if (!r.category.empty()) r.category += L", ";
            r.category += U2W(cats->arr[i].str);
        }
    }

    std::wstring latest;
    const mj::Value* bestEntry = FindLatestVersionEntry(f.root, latest);
    if (!bestEntry) { r.error = L"Aucune version exploitable listee pour ce mod dans la reponse."; return r; }
    r.latestVersion = latest;

    std::wstring changelog = modUrl;
    if (!changelog.empty() && changelog[changelog.size() - 1] != L'/') changelog += L'/';
    changelog += L"changelog/";
    r.changelogUrl = changelog;

    std::wstring iconUrl = U2W(bestEntry->s("icon"));
    if (!iconUrl.empty()) {
        std::wstring host, path;
        if (SplitHttpsUrl(iconUrl, host, path)) {
            std::string bytes; DWORD status = 0; std::wstring err;
            if (HttpGetBytes(host, path, bytes, status, err) && status == 200 && !bytes.empty()) {
                std::wstring ns, nm;
                ParseThunderstoreUrl(modUrl, ns, nm);   // deja valide via f.isThunderstore
                std::wstring dir = ExeDir() + L"\\icons";
                MakeDirs(dir);
                std::wstring dest = dir + L"\\" + SanitizeFileName(ns) + L"-" + SanitizeFileName(nm) + L".png";
                if (WriteAllBytes(dest, bytes)) r.localIconPath = dest;
            }
            // l'icone est un bonus : si le telechargement echoue, on continue
            // quand meme avec le reste des champs plutot que de tout faire echouer.
        }
    }

    r.ok = true;
    return r;
}

// Texte + categorie de couleur pour la colonne "MAJ" : 0 gris (inconnu),
// 1 vert (a jour), 2 rouge (mise a jour disponible).
static std::wstring TsStatusText(const Mod& m, int* colorCategory) {
    if (colorCategory) *colorCategory = 0;
    if (m.tsVersion.empty()) return L"-";
    std::wstring installed = m.dllPath.empty() ? L"" : GetDllVersionString(m.dllPath);
    if (installed.empty()) return m.tsVersion;   // derniere version connue, mais rien a comparer
    if (VersionLess(installed, m.tsVersion)) {
        if (colorCategory) *colorCategory = 2;
        return m.tsVersion + L" (maj disponible)";
    }
    if (colorCategory) *colorCategory = 1;
    return m.tsVersion + L" (a jour)";
}

// ---------------------------------------------------------------- liste mods
static void RefillMods() {
    std::stable_sort(g_mods.begin(), g_mods.end(), ModLess);
    SendMessageW(g_hMods, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_hMods);
    for (size_t i = 0; i < g_mods.size(); ++i) {
        LVITEMW it; memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        it.iItem = (int)i;
        it.iImage = GetOrLoadIcon(g_mods[i].iconPath);
        it.pszText = (LPWSTR)g_mods[i].name.c_str();
        it.lParam = (LPARAM)i;
        ListView_InsertItem(g_hMods, &it);
        ListView_SetItemText(g_hMods, (int)i, COL_CAT, (LPWSTR)g_mods[i].cat.c_str());
        std::wstring last = g_mods[i].last.empty() ? L"jamais" : g_mods[i].last;
        ListView_SetItemText(g_hMods, (int)i, COL_LASTCHECK, (LPWSTR)last.c_str());
        std::wstring dt = DaysText(g_mods[i].last);
        ListView_SetItemText(g_hMods, (int)i, COL_AGE, (LPWSTR)dt.c_str());
        std::wstring dllTxt = DllStatusText(g_mods[i], NULL);
        ListView_SetItemText(g_hMods, (int)i, COL_DLL, (LPWSTR)dllTxt.c_str());
        std::wstring tsTxt = TsStatusText(g_mods[i], NULL);
        ListView_SetItemText(g_hMods, (int)i, COL_TSVER, (LPWSTR)tsTxt.c_str());
        ListView_SetItemText(g_hMods, (int)i, COL_URL, (LPWSTR)g_mods[i].url.c_str());
        ListView_SetItemText(g_hMods, (int)i, COL_NOTE, (LPWSTR)g_mods[i].note.c_str());
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
    HWND hName, hCat, hUrl, hHist, hDll, hIcon, hNote, hIconPreview;
    HICON previewIcon;
    EditCtx() : ok(false), hName(0), hCat(0), hUrl(0), hHist(0), hDll(0), hIcon(0),
                hNote(0), hIconPreview(0), previewIcon(0) {}
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
static HWND MkDlgButton(HWND p, int id, const wchar_t* txt, int x, int y, int w, int h) {
    HWND btn = CreateWindowExW(0, L"BUTTON", txt,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x, y, w, h, p, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageW(btn, WM_SETFONT, (WPARAM)g_font, TRUE);
    return btn;
}

// Boite "Parcourir..." standard ; renvoie 'current' inchange si l'utilisateur
// annule, pour que l'appelant n'ait pas a distinguer "annule" de "vide".
static std::wstring BrowseFile(HWND owner, const wchar_t* filter,
                               const std::wstring& current, const wchar_t* title)
{
    wchar_t buf[MAX_PATH]; buf[0] = 0;
    if (!current.empty()) {
        wcsncpy(buf, current.c_str(), MAX_PATH - 1);
        buf[MAX_PATH - 1] = 0;
    }
    std::wstring initDir;
    if (!current.empty()) {
        size_t p = current.find_last_of(L"\\/");
        if (p != std::wstring::npos) initDir = current.substr(0, p);
    }
    if (initDir.empty() || !DirExists(initDir)) {
        std::wstring pd = PluginsDir();
        initDir = DirExists(pd) ? pd : L"";
    }
    OPENFILENAMEW ofn; memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.lpstrInitialDir = initDir.empty() ? NULL : initDir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn)) return std::wstring(buf);
    return current;
}
static void UpdateIconPreview(EditCtx* c, const std::wstring& path) {
    if (!c || !c->hIconPreview) return;
    HICON hi = path.empty() ? NULL : LoadScaledIconFromFile(path, 32);
    SendMessageW(c->hIconPreview, STM_SETICON, (WPARAM)hi, 0);
    if (c->previewIcon) DestroyIcon(c->previewIcon);
    c->previewIcon = hi;
}

static LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    EditCtx* c = (EditCtx*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        c = (EditCtx*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)c);

        // -- colonne de gauche : champs texte (nom / categorie / liens) -----
        const int LX = 12, LW = 360;
        MkLabel(hwnd, L"Nom du mod *", LX, 10, LW);
        c->hName = MkEdit(hwnd, c->m.name, LX, 28, LW);
        MkLabel(hwnd, L"Categorie / auteur", LX, 58, LW);
        c->hCat = MkEdit(hwnd, c->m.cat, LX, 76, LW);
        MkLabel(hwnd, L"Lien de la page du mod *", LX, 106, LW);
        c->hUrl = MkEdit(hwnd, c->m.url, LX, 124, LW);
        MkDlgButton(hwnd, IDC_E_AUTOFILL, L"Auto-remplir depuis Thunderstore", LX, 150, 220, 24);
        MkLabel(hwnd, L"Lien historique / changelog (optionnel)", LX, 184, LW);
        c->hHist = MkEdit(hwnd, c->m.changelogUrl, LX, 202, LW);

        // -- colonne de droite : icone (apercu + parcourir + effacer) -------
        const int IX = LX + LW + 16;   // 388
        MkLabel(hwnd, L"Icone", IX, 10, 130);
        c->hIconPreview = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
            IX, 28, 40, 40, hwnd, NULL, g_hInst, NULL);
        MkDlgButton(hwnd, IDC_E_BROWSEICON, L"Parcourir...", IX, 72, 130, 24);
        MkDlgButton(hwnd, IDC_E_CLEARICON,  L"Effacer",      IX, 100, 130, 24);

        // -- DLL associe, pleine largeur -------------------------------------
        MkLabel(hwnd, L"DLL installe (pour verifier qu'il est bien present)", LX, 232, LW + 16 + 130);
        c->hDll = MkEdit(hwnd, c->m.dllPath, LX, 250, LW);
        MkDlgButton(hwnd, IDC_E_BROWSEDLL, L"Parcourir...", LX + LW + 24, 250, 124, 24);

        MkLabel(hwnd, L"Note (version installee, remarques...)", LX, 280, LW + 16 + 130);
        c->hNote = MkEdit(hwnd, c->m.note, LX, 298, LW + 16 + 130);

        HWND ok = CreateWindowExW(0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            336, 336, 92, 28, hwnd, (HMENU)IDOK, g_hInst, NULL);
        HWND ca = CreateWindowExW(0, L"BUTTON", L"Annuler",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            436, 336, 92, 28, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
        SendMessageW(ok, WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(ca, WM_SETFONT, (WPARAM)g_font, TRUE);

        if (!c->m.iconPath.empty()) UpdateIconPreview(c, c->m.iconPath);

        SetFocus(c->hName);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_E_AUTOFILL && c) {
            std::wstring url = Clean(GetTextOf(c->hUrl));
            if (url.empty()) {
                MessageBoxW(hwnd, L"Renseigne d'abord le lien de la page du mod.",
                    L"ValMods", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            HCURSOR oldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
            TsAutofillResult r = FetchThunderstoreAutofill(url);
            SetCursor(oldCursor);

            if (!r.isThunderstore) {
                MessageBoxW(hwnd,
                    L"Ce lien ne pointe pas vers thunderstore.io :\n"
                    L"l'auto-remplissage ne fonctionne que pour les mods\n"
                    L"heberges sur Thunderstore.",
                    L"ValMods", MB_OK | MB_ICONWARNING);
                return 0;
            }
            if (!r.ok) {
                std::wstring m = L"Auto-remplissage impossible :\n" + r.error;
                MessageBoxW(hwnd, m.c_str(), L"ValMods", MB_OK | MB_ICONWARNING);
                return 0;
            }

            // remplace toujours les champs concernes : un clic explicite sur
            // "Auto-remplir" veut dire "je veux les donnees fraiches de Thunderstore".
            if (!r.name.empty())         SetWindowTextW(c->hName, r.name.c_str());
            if (!r.category.empty())     SetWindowTextW(c->hCat, r.category.c_str());
            if (!r.changelogUrl.empty()) SetWindowTextW(c->hHist, r.changelogUrl.c_str());
            if (!r.localIconPath.empty()) {
                SetWindowTextW(c->hIcon, r.localIconPath.c_str());
                UpdateIconPreview(c, r.localIconPath);
            }
            c->m.tsVersion = r.latestVersion;   // porte jusqu'a l'enregistrement (pas de champ texte dedie)

            std::wstring summary = L"Champs remplis depuis Thunderstore.\n"
                L"Derniere version publiee : " + r.latestVersion;
            if (r.localIconPath.empty())
                summary += L"\n(icone non recuperee - le mod n'en a peut-etre pas)";
            Info(hwnd, summary.c_str());
            return 0;
        }
        if (LOWORD(wp) == IDC_E_BROWSEDLL && c) {
            std::wstring picked = BrowseFile(hwnd,
                L"Bibliotheques (*.dll)\0*.dll\0Tous les fichiers (*.*)\0*.*\0",
                GetTextOf(c->hDll), L"Choisir le DLL installe");
            SetWindowTextW(c->hDll, picked.c_str());
            return 0;
        }
        if (LOWORD(wp) == IDC_E_BROWSEICON && c) {
            std::wstring picked = BrowseFile(hwnd,
                L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.ico;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.ico;*.gif\0"
                L"Tous les fichiers (*.*)\0*.*\0",
                GetTextOf(c->hIcon), L"Choisir une icone");
            SetWindowTextW(c->hIcon, picked.c_str());
            UpdateIconPreview(c, picked);
            return 0;
        }
        if (LOWORD(wp) == IDC_E_CLEARICON && c) {
            SetWindowTextW(c->hIcon, L"");
            UpdateIconPreview(c, L"");
            return 0;
        }
        if (LOWORD(wp) == IDOK && c) {
            std::wstring n = Clean(GetTextOf(c->hName));
            std::wstring u = Clean(GetTextOf(c->hUrl));
            if (n.empty() || u.empty()) {
                MessageBoxW(hwnd, L"Le nom et le lien de la page du mod sont obligatoires.",
                    L"ValMods", MB_OK | MB_ICONWARNING);
                return 0;
            }
            c->m.name = n;
            c->m.cat  = Clean(GetTextOf(c->hCat));
            c->m.url  = u;
            c->m.changelogUrl = Clean(GetTextOf(c->hHist));
            c->m.dllPath      = Clean(GetTextOf(c->hDll));
            c->m.iconPath     = Clean(GetTextOf(c->hIcon));
            c->m.note = Clean(GetTextOf(c->hNote));
            c->ok = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hwnd); return 0; }
        break;
    case WM_DESTROY:
        // l'apercu d'icone est une copie GDI+ locale a cette fenetre, jamais
        // partagee avec l'image list de la liste principale : a nettoyer ici.
        if (c && c->previewIcon) { DestroyIcon(c->previewIcon); c->previewIcon = NULL; }
        return 0;
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
    RECT r = { 0, 0, 540, 380 };
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
static void ActionOpenHistory(HWND hwnd) {
    int i = SelectedMod();
    if (i < 0) { Info(hwnd, L"Selectionne un mod dans la liste."); return; }
    if (g_mods[i].changelogUrl.empty()) {
        Info(hwnd, L"Ce mod n'a pas de lien vers son historique des versions.\n"
                   L"Ajoute-le en modifiant le mod (bouton Modifier).");
        return;
    }
    ShellExecuteW(NULL, L"open", g_mods[i].changelogUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
}
static void ActionLocateDll(HWND hwnd) {
    int i = SelectedMod();
    if (i < 0) { Info(hwnd, L"Selectionne un mod dans la liste."); return; }
    const std::wstring& p = g_mods[i].dllPath;
    if (p.empty()) {
        Info(hwnd, L"Aucun DLL associe a ce mod.\nAssocie-le en modifiant le mod (bouton Modifier).");
        return;
    }
    if (!FileExists(p)) {
        std::wstring m = L"Le fichier DLL associe est introuvable :\n" + p;
        MessageBoxW(hwnd, m.c_str(), L"ValMods", MB_OK | MB_ICONWARNING);
        return;
    }
    RevealFile(p);
}
static void ActionOpenDllDir(HWND hwnd) {
    int i = SelectedMod();
    if (i < 0) { Info(hwnd, L"Selectionne un mod dans la liste."); return; }
    const std::wstring& p = g_mods[i].dllPath;
    if (p.empty()) {
        Info(hwnd, L"Aucun DLL associe a ce mod.\nAssocie-le en modifiant le mod (bouton Modifier).");
        return;
    }
    size_t s = p.find_last_of(L"\\/");
    std::wstring dir = (s == std::wstring::npos) ? PluginsDir() : p.substr(0, s);
    OpenFolder(hwnd, dir, L"dossier du DLL");
}
static void ActionCheckThunderstore(HWND hwnd) {
    int i = SelectedMod();
    if (i < 0) { Info(hwnd, L"Selectionne un mod dans la liste."); return; }
    std::wstring name = g_mods[i].name;
    std::wstring url = g_mods[i].url;

    HCURSOR oldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    TsCheckResult r = CheckThunderstoreVersion(url);
    SetCursor(oldCursor);

    if (!r.isThunderstore) {
        Info(hwnd, L"Le lien de ce mod ne pointe pas vers thunderstore.io.\n"
                   L"La verification automatique ne fonctionne que pour les mods\n"
                   L"heberges sur Thunderstore (Nexus, GitHub... ne sont pas geres).");
        return;
    }
    if (!r.ok) {
        std::wstring m = L"Verification impossible :\n" + r.error;
        MessageBoxW(hwnd, m.c_str(), L"ValMods", MB_OK | MB_ICONWARNING);
        return;
    }

    // la liste peut avoir ete retriee pendant l'appel reseau : on retrouve
    // le mod par son nom plutot que de garder l'index d'avant l'appel.
    for (size_t k = 0; k < g_mods.size(); ++k) {
        if (g_mods[k].name == name) {
            g_mods[k].tsVersion = r.latestVersion;
            g_mods[k].last = NowStamp();   // une verification en ligne compte comme une verification
            SaveMods();
            RefillMods();
            SelectMod((int)k);
            break;
        }
    }

    std::wstring msg = L"Derniere version publiee sur Thunderstore : " + r.latestVersion;
    if (r.deprecated)
        msg += L"\n\nATTENTION : ce mod est marque comme deprecie (abandonne) "
              L"par son auteur sur Thunderstore.";
    Info(hwnd, msg.c_str());
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

    // --- onglet mods : deux rangees de boutons + liste
    const int bw = 104, bh = 28, gap = 6;
    int row1[4] = { IDC_BADD, IDC_BEDIT, IDC_BDEL, IDC_BCOPY };
    int row2[5] = { IDC_BWATCH, IDC_BHIST, IDC_BCHECK, IDC_BMARK, IDC_BTSCHECK };
    for (int i = 0; i < 4; ++i) {
        HWND b1 = GetDlgItem(hwnd, row1[i]);
        if (b1) MoveWindow(b1, px + i * (bw + gap), py, bw, bh, TRUE);
    }
    for (int i = 0; i < 5; ++i) {
        HWND b2 = GetDlgItem(hwnd, row2[i]);
        if (b2) MoveWindow(b2, px + i * (bw + gap), py + bh + gap, bw, bh, TRUE);
    }
    MoveWindow(g_hMods, px, py + 2 * (bh + gap), pw, ph - 2 * (bh + gap), TRUE);

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
static void AddTip(HWND ctrl, const wchar_t* text) {
    if (!g_hTooltip || !ctrl) return;
    TOOLINFOW ti; memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = GetParent(ctrl);
    ti.uId = (UINT_PTR)ctrl;
    ti.lpszText = (LPWSTR)text;
    SendMessageW(g_hTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

static void BuildMenu(HWND hwnd) {
    HMENU bar = CreateMenu();
    HMENU f = CreatePopupMenu();
    AppendMenuW(f, MF_STRING, IDM_OPENDATA, L"Ouvrir le dossier de donnees\tvalmods.json");
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
        g_pageMods[g_nMods++] = MkButton(hwnd, IDC_BCOPY,  L"Copier lien");
        g_pageMods[g_nMods++] = MkButton(hwnd, IDC_BWATCH, L"Watch");
        g_pageMods[g_nMods++] = MkButton(hwnd, IDC_BHIST,  L"Historique");
        g_pageMods[g_nMods++] = MkButton(hwnd, IDC_BCHECK, L"Check update");
        g_pageMods[g_nMods++] = MkButton(hwnd, IDC_BMARK,  L"Verifie");
        g_pageMods[g_nMods++] = MkButton(hwnd, IDC_BTSCHECK, L"Verif. TS");
        g_hMods = MkList(hwnd, IDC_MODLIST);
        g_pageMods[g_nMods++] = g_hMods;
        AddCol(g_hMods, COL_NAME,      L"Mod", 190);
        AddCol(g_hMods, COL_CAT,       L"Categorie", 110);
        AddCol(g_hMods, COL_LASTCHECK, L"Derniere verif", 115);
        AddCol(g_hMods, COL_AGE,       L"Age", 50);
        AddCol(g_hMods, COL_DLL,       L"DLL", 170);
        AddCol(g_hMods, COL_TSVER,     L"MAJ (Thunderstore)", 150);
        AddCol(g_hMods, COL_URL,       L"Lien", 220);
        AddCol(g_hMods, COL_NOTE,      L"Note", 140);

        // icone par mod : petite image list 16x16 alimentee a la volee
        // (voir GetOrLoadIcon) via GDI+, avec une icone grise par defaut.
        g_imgList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 8, 32);
        if (g_imgList) {
            HICON def = MakeDefaultIcon(16);
            g_defaultIconIdx = def ? ImageList_AddIcon(g_imgList, def) : -1;
            if (def) DestroyIcon(def);
            ListView_SetImageList(g_hMods, g_imgList, LVSIL_SMALL);
        }

        // info-bulles sur les boutons : repond a "a quoi sert ce bouton ?"
        // directement dans l'interface plutot que dans une doc a part.
        g_hTooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd, NULL, g_hInst, NULL);
        if (g_hTooltip) {
            SetWindowPos(g_hTooltip, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            AddTip(GetDlgItem(hwnd, IDC_BADD),   L"Ajoute un nouveau mod a la liste");
            AddTip(GetDlgItem(hwnd, IDC_BEDIT),  L"Modifie le mod selectionne");
            AddTip(GetDlgItem(hwnd, IDC_BDEL),   L"Retire la fiche du mod (ne desinstalle rien)");
            AddTip(GetDlgItem(hwnd, IDC_BCOPY),  L"Copie le lien du mod dans le presse-papier");
            AddTip(GetDlgItem(hwnd, IDC_BWATCH), L"Ouvre la page du mod dans le navigateur");
            AddTip(GetDlgItem(hwnd, IDC_BHIST),  L"Ouvre la page d'historique / changelog du mod");
            AddTip(GetDlgItem(hwnd, IDC_BCHECK), L"Ouvre la page du mod ET note la date de verification du jour");
            AddTip(GetDlgItem(hwnd, IDC_BMARK),
                L"Note la date de verification SANS ouvrir de lien - utile si tu as deja "
                L"verifie ailleurs (Discord du mod, changelog deja ouvert dans un autre onglet...)");
            AddTip(GetDlgItem(hwnd, IDC_BTSCHECK),
                L"Interroge l'API publique de Thunderstore pour connaitre la derniere "
                L"version publiee (uniquement pour les mods heberges sur thunderstore.io)");
        }


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
        case IDC_BHIST:  ActionOpenHistory(hwnd);  return 0;
        case IDC_BCHECK: ActionOpen(hwnd, true);   return 0;
        case IDC_BMARK:  ActionMark(hwnd);         return 0;
        case IDC_BCOPY:  ActionCopy(hwnd);         return 0;
        case IDC_BTSCHECK: ActionCheckThunderstore(hwnd); return 0;

        case IDM_CTX_WATCH:       ActionOpen(hwnd, false); return 0;
        case IDM_CTX_HIST:        ActionOpenHistory(hwnd); return 0;
        case IDM_CTX_CHECK:       ActionOpen(hwnd, true);  return 0;
        case IDM_CTX_MARK:        ActionMark(hwnd);        return 0;
        case IDM_CTX_TSCHECK:     ActionCheckThunderstore(hwnd); return 0;
        case IDM_CTX_LOCATE_DLL:  ActionLocateDll(hwnd);   return 0;
        case IDM_CTX_OPEN_DLLDIR: ActionOpenDllDir(hwnd);  return 0;
        case IDM_CTX_COPY:        ActionCopy(hwnd);        return 0;
        case IDM_CTX_EDIT:        ActionEdit(hwnd);        return 0;
        case IDM_CTX_DELETE:      ActionDelete(hwnd);      return 0;

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
                L"Watch          : ouvre la page du mod\n"
                L"Historique     : ouvre la page des changements / versions\n"
                L"Check update   : ouvre la page du mod ET note la date de verification\n"
                L"Verifie        : note la date de verification SANS ouvrir de lien\n"
                L"                 (utile si tu as deja verifie ailleurs : Discord du\n"
                L"                 mod, changelog deja ouvert dans un autre onglet...)\n\n"
                L"Double-clic sur un mod : ouvre son lien\n"
                L"Clic droit sur un mod  : menu avec toutes les actions, y compris le DLL\n"
                L"Clic sur un en-tete de colonne : tri\n\n"
                L"La colonne DLL indique si le fichier associe au mod est present (vert),\n"
                L"manquant (rouge) ou non renseigne (gris).\n\n"
                L"Verif. TS interroge l'API publique de Thunderstore pour connaitre la\n"
                L"derniere version publiee (uniquement pour les mods heberges sur\n"
                L"thunderstore.io - Nexus/GitHub ne sont pas geres). La colonne MAJ\n"
                L"compare cette version a celle du DLL installe : vert = a jour,\n"
                L"rouge = mise a jour disponible, gris = pas encore verifie / inconnu.\n\n"
                L"Dans l'editeur, Auto-remplir depuis Thunderstore recupere le nom, la\n"
                L"categorie, le lien historique, la derniere version et l'icone du mod\n"
                L"a partir du lien colle (meme limite : Thunderstore uniquement).\n\n"
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
            if (nh->code == NM_RCLICK) {
                LPNMITEMACTIVATE ia = (LPNMITEMACTIVATE)lp;
                if (ia->iItem >= 0) SelectMod(ia->iItem);
                if (SelectedMod() >= 0) {
                    POINT pt; GetCursorPos(&pt);
                    HMENU m = CreatePopupMenu();
                    AppendMenuW(m, MF_STRING, IDM_CTX_WATCH,       L"Ouvrir le lien (Watch)");
                    AppendMenuW(m, MF_STRING, IDM_CTX_HIST,        L"Ouvrir l'historique des versions");
                    AppendMenuW(m, MF_STRING, IDM_CTX_CHECK,       L"Ouvrir le lien + noter la verification");
                    AppendMenuW(m, MF_STRING, IDM_CTX_MARK,        L"Noter la verification (sans ouvrir)");
                    AppendMenuW(m, MF_STRING, IDM_CTX_TSCHECK,     L"Verifier la derniere version (Thunderstore)");
                    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(m, MF_STRING, IDM_CTX_LOCATE_DLL,  L"Localiser le DLL dans l'explorateur");
                    AppendMenuW(m, MF_STRING, IDM_CTX_OPEN_DLLDIR, L"Ouvrir le dossier du DLL");
                    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(m, MF_STRING, IDM_CTX_COPY,        L"Copier le lien");
                    AppendMenuW(m, MF_STRING, IDM_CTX_EDIT,        L"Modifier...");
                    AppendMenuW(m, MF_STRING, IDM_CTX_DELETE,      L"Supprimer");
                    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                    DestroyMenu(m);
                }
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
                    // on redemande un passage par sous-element pour pouvoir
                    // surcharger juste la couleur de la colonne DLL en dessous.
                    return CDRF_NOTIFYSUBITEMDRAW;
                }
                if (cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                    size_t idx = (size_t)cd->nmcd.lItemlParam;
                    if (cd->iSubItem == COL_DLL) {
                        if (idx < g_mods.size()) {
                            bool missing = false;
                            DllStatusText(g_mods[idx], &missing);
                            if (missing)                            cd->clrText = RGB(200, 40, 40);
                            else if (!g_mods[idx].dllPath.empty())  cd->clrText = RGB(40, 130, 60);
                            else                                     cd->clrText = RGB(130, 130, 130);
                        }
                    } else if (cd->iSubItem == COL_TSVER) {
                        if (idx < g_mods.size()) {
                            int cat = 0;
                            TsStatusText(g_mods[idx], &cat);
                            if (cat == 2)      cd->clrText = RGB(200, 40, 40);
                            else if (cat == 1) cd->clrText = RGB(40, 130, 60);
                            else               cd->clrText = RGB(130, 130, 130);
                        }
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

    // ICC_WIN95_CLASSES couvre entre autres les tooltips (TOOLTIPS_CLASSW)
    // utilises pour les info-bulles des boutons, en plus des listes/onglets.
    INITCOMMONCONTROLSEX ic;
    ic.dwSize = sizeof(ic);
    ic.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&ic);

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::Status gdiStatus =
        Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);
    // Si GDI+ echoue a s'initialiser (tres rare), on continue quand meme :
    // GetOrLoadIcon retombe alors sur g_defaultIconIdx == -1, donc simplement
    // aucune icone affichee plutot qu'un crash.

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

    if (g_imgList) { ImageList_Destroy(g_imgList); g_imgList = NULL; }
    if (gdiStatus == Gdiplus::Ok) Gdiplus::GdiplusShutdown(g_gdiplusToken);
    OleUninitialize();
    return (int)msg.wParam;
}
