// ============================================================================
//  ValMods - petit gestionnaire manuel de mods Valheim (Win32 natif, C++)
//  - vue en cartes (pas un tableau) : icone visible, nom, petits details
//    (categorie / derniere verif / etat DLL / etat Thunderstore / note),
//    et les boutons d'action directement sur chaque ligne
//  - bouton "Watch"   : ouvre le lien du mod        "Hist." : historique
//  - bouton "Check+"  : ouvre le lien + horodate     "OK"   : horodate sans ouvrir
//  - bouton "TS"      : verifie la derniere version sur Thunderstore
//  - bouton "DL"      : telecharge le zip (demande ou l'enregistrer)
//  - bouton "Modif."  : modifie le mod               "Config" : ouvre le
//    fichier de config avec le programme associe par Windows
//  - bouton "..."     : copier lien / localiser le DLL ou la config /
//    ouvrir leurs dossiers / supprimer
//  - tri via un menu deroulant + un bouton croissant/decroissant
//  - acces rapide aux dossiers plugins / config / sauvegardes / telechargements
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
#define WINVER 0x0600        // sans ca, des macros comme SS_ENDELLIPSIS
                             // restent gardees derriere #if(WINVER >= ...)
                             // dans winuser.h et ne se declarent pas.
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
#include <initializer_list>   // FieldAny (lecture tolerante de valmods.json, voir LoadData)
#include <cwctype>            // towlower (PickBestDll, comparaison de noms insensible a la casse)

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
#define IDC_BADD       1002
#define IDC_SORTCOMBO  1018
#define IDC_SORTDIR    1019
#define IDC_CHECKALL   1020
#define IDC_DLALL      1021
#define IDC_SHOW10     1022
#define IDC_MODPACK    1023
#define IDC_HIDEUPTODATE 1024
#define IDC_TAGFILTER  1025
#define IDC_SEARCHBOX  1026
#define IDC_SEARCHCLEAR 1027
// cases a cocher "sur quoi chercher" (voir ModMatchesSearch) - une par
// champ inclus dans la recherche texte libre.
#define IDC_SEARCH_NAME     1028
#define IDC_SEARCH_CAT      1029
#define IDC_SEARCH_DESC     1030
#define IDC_SEARCH_NOTE     1031
#define IDC_SEARCH_TAGS     1032
#define IDC_SEARCH_MODPACK  1033
#define IDC_SEARCH_URL      1034
#define IDC_WORLDS     1010
#define IDC_CHARS      1011
#define IDC_LBL1       1012
#define IDC_LBL2       1013
#define IDC_BREFRESH   1014
#define IDC_BOPENSAVE  1015
#define IDC_BBACKUP    1016
#define IDC_BOPENBK    1017

// actions sur une ligne (carte) de mod : bouton dynamique = RA_BASE +
// index_ligne * RA_COUNT + action. LOWORD(wParam) de WM_COMMAND est un
// entier 16 bits (0-65535), donc jusqu'a (65535-RA_BASE)/RA_COUNT lignes
// possibles - tres largement suffisant pour une liste de mods reelle.
#define RA_BASE  5000
enum { RA_WATCH = 0, RA_HIST = 1, RA_CHECK = 2, RA_MARK = 3, RA_TS = 4,
       RA_DL = 5, RA_EDIT = 6, RA_CONFIG = 7, RA_MORE = 8, RA_COUNT = 9 };

// Un bouton de carte declenche son action via un message DIFFERE
// (PostMessage), jamais executee directement dans le gestionnaire de
// WM_COMMAND : l'action (ex: Verifie) peut appeler RefillMods(), qui detruit
// et recree TOUTES les cartes - y compris le bouton en train d'etre clique,
// dont le propre traitement de clic n'a pas fini de se derouler dans la pile
// d'appels (WM_COMMAND est relaye de facon synchrone depuis le panneau de
// cartes). Detruire une fenetre pendant qu'elle traite encore son propre
// evenement est un piege classique en Win32. Poster differe l'execution
// reelle a la prochaine iteration de la boucle de messages, une fois que le
// clic du bouton s'est completement termine et que la pile s'est deroulee.
#define WM_APP_ROWACTION (WM_APP + 1)
// actions du menu "..." (pas encodees dans l'id d'un bouton - la cible est
// g_ctxMenuModIndex, pose au moment d'ouvrir le menu)
enum { RA_MENU_COPY = 100, RA_MENU_LOCATE_DLL = 101, RA_MENU_OPEN_DLLDIR = 102, RA_MENU_DELETE = 103,
       RA_MENU_LOCATE_CONFIG = 104, RA_MENU_OPEN_CONFIG = 105 };

#define IDM_OPENDATA   2001
#define IDM_EXIT       2002
#define IDM_PLUGINS    2003
#define IDM_CONFIG     2004
#define IDM_SAVES      2005
#define IDM_GAMEDIR    2006
#define IDM_SETDIR     2007
#define IDM_ABOUT      2008
#define IDM_BEPINEX    2009
#define IDM_DOWNLOADS  2010

// menu "..." (actions moins frequentes) sur une carte de mod - les actions
// courantes (Watch/Historique/Check/Verifie/TS/Telecharger/Modifier) sont
// deja des boutons directs sur la carte, inutile de les dupliquer ici.
#define IDM_CTX_LOCATE_DLL  2104
#define IDM_CTX_OPEN_DLLDIR 2105
#define IDM_CTX_COPY        2106
#define IDM_CTX_DELETE      2108
#define IDM_CTX_LOCATE_CONFIG 2109
#define IDM_CTX_OPEN_CONFIG   2110

// boutons propres a la fenetre d'edition d'un mod
#define IDC_E_BROWSEDLL   3001
#define IDC_E_BROWSEICON  3002
#define IDC_E_CLEARICON   3003
#define IDC_E_AUTOFILL    3004
#define IDC_E_APISOURCE   3005
#define IDC_E_MODPACK     3006
#define IDC_E_BROWSECONFIG 3007

// parametres (cle API Nexus)
#define IDM_NEXUSKEY   2011
#define IDM_FIXLIST    2012

// controles propres a la petite boite de saisie de texte generique
// (ShowTextInputDialog - utilisee pour la cle API Nexus)
#define IDC_TI_EDIT    3101

// Version de l'application (affichee dans "A propos" et par --version).
// A ne pas confondre avec le numero de schema du fichier valmods.json, qui
// evolue independamment (voir SaveMods). 2.0.0 : refonte en vue "cartes",
// integration Thunderstore (verification/telechargement), modpacks, suivi
// de version installee. 2.1.0 : support Nexus Mods (verification/auto-
// remplissage via cle API personnelle), chemins d'icone relatifs au dossier
// de l'exe (portabilite - valmods.json + valmods.exe + icons\ peuvent
// voyager ensemble). 2.2.0 : tri par source, filtre par tags (en plus du
// modpack), bouton de tri renomme "Tri" (le glyphe suffit a indiquer le sens).
// 2.3.0 : recherche texte libre (barre du haut), lecture retrocompatible
// d'un valmods.json plus ancien/different (racine en tableau, noms de champ
// alternatifs, cle "mods" absente...). 2.4.0 : cases a cocher pour choisir
// sur quels champs porte la recherche (nom/categorie/description/note/
// tags/modpack/lien), preference persistee.
#define VALMODS_VERSION "2.7.1"

// Numero de schema du fichier valmods.json (le champ "version" a la
// racine) - AUCUN rapport avec VALMODS_VERSION ci-dessus (le numero de
// version de l'appli). Source UNIQUE de verite pour ce numero : utilisee
// a la fois par SaveMods() (qui l'ecrit) et par LoadData() (qui compare
// oldSchemaVersion a cette meme constante pour decider si le message "ce
// fichier vient d'un format plus ancien" doit s'afficher). Les deux
// utilisaient auparavant des nombres litteraux distincts (14 d'un cote,
// 12 de l'autre) qui ont fini par diverger silencieusement a un bump de
// schema (ajout de modDir/configPath) sans que le seuil de detection soit
// mis a jour en meme temps - d'ou cette constante commune, a incrementer
// ICI SEULEMENT a chaque ajout/renommage de champ affectant le format.
#define VALMODS_JSON_SCHEMA_VERSION 14

// "colonnes" au sens tri uniquement desormais (il n'y a plus de tableau) :
// meme enum reutilise par ModLess et par le menu deroulant de tri.
enum { COL_NAME = 0, COL_CAT = 1, COL_LASTCHECK = 2, COL_AGE = 3,
       COL_DLL = 4, COL_TSVER = 5, COL_URL = 6, COL_NOTE = 7, COL_SOURCE = 8 };

// Source utilisee pour la verification/l'auto-remplissage d'un mod.
// API_THUNDERSTORE : comportement historique, ne fonctionne que si l'URL
// pointe vers thunderstore.io (voir ParseThunderstoreUrl).
// API_NEXUS : interroge l'API Nexus Mods (necessite une cle API personnelle,
// voir g_nexusApiKey / menu Parametres) - fonctionne pour une URL
// nexusmods.com/valheim/mods/<id>. Pas de telechargement direct (le bouton
// DL reste desactive : l'API de telechargement de Nexus est reservee aux
// comptes Premium), seule la verification de version est disponible.
// API_HEXIUM : interroge l'API publique de Hexium (aucune cle requise,
// voir valheim.hexium.gg/api/docs/) - fonctionne pour une URL
// valheim.hexium.gg/mods/<equipe>/<nom>. Contrairement a Nexus, Hexium
// fournit un lien de telechargement direct par version (download_url),
// donc le bouton DL reste actif, comme pour Thunderstore (voir
// FetchHexiumPackageEntry / DownloadHexiumZip plus bas).
// API_NONE : desactive TS/DL pour ce mod (equivalent a l'ancienne case
// "Non Thunderstore") - utile pour un mod dont aucune des API ne
// convient (GitHub, page perso...).
enum { API_THUNDERSTORE = 0, API_NEXUS = 1, API_HEXIUM = 2, API_NONE = 3 };

// Compteur pour Mod::uid (voir plus bas) - jamais persiste, jamais reinitialise
// en cours d'execution : chaque Mod construit pendant la vie du process,
// meme temporairement (parsing JSON, editeur...), recoit un entier distinct.
static int g_nextModUid = 1;
static int NextModUid() { return g_nextModUid++; }

// ---------------------------------------------------------------- donnees
struct Mod {
    std::wstring name, cat, url, changelogUrl, modDir, iconPath, tsVersion, description, last, note;
    // Chemin vers le fichier de config du mod (typiquement un .cfg BepInEx
    // sous BepInEx\config\, mais peut etre n'importe quel fichier texte
    // selon le mod) - purement informatif, sert juste a le localiser/l'ouvrir
    // rapidement (voir ActionLocateConfig/ActionOpenConfig), n'est jamais
    // utilise pour la verification de version ou de presence.
    std::wstring configPath;
    // Date de publication (ISO8601, telle que renvoyee par Thunderstore) de
    // la derniere version connue - sert a comparer avec la sortie de la 1.0
    // de Valheim (voir VALHEIM_10_DATE). Distincte de "last" (date a
    // laquelle NOUS avons verifie), c'est la date a laquelle LE MOD a ete
    // publie.
    std::wstring tsLatestDate;
    // Version qu'ON SUIT comme etant celle installee - mise a jour
    // automatiquement par Telecharger/Tout DL (on suppose que le zip va
    // etre extrait), et editable a la main dans l'editeur. Distincte de la
    // version EMBARQUEE dans le fichier DLL (voir GetDllVersionString) :
    // beaucoup de DLL de mods Unity/BepInEx n'ont pas de ressource de
    // version fiable, donc "Tout verifier" et le statut affiche sur la
    // carte s'appuient sur CE champ en priorite (voir
    // GetEffectiveInstalledVersion), avec repli sur la version DLL
    // uniquement si ce champ est vide.
    std::wstring installedVersion;
    // Regroupe des mods pour un meme playthrough/serveur ("modpack") ; vide
    // = pas assigne. Sert de filtre d'affichage et pour "Tout verifier" /
    // "Tout DL" (voir g_modpackFilter).
    std::wstring modpack;
    // Tags libres separes par des virgules (ex: "QoL, Building, Serveur")
    // - un mod peut en avoir plusieurs, contrairement au modpack qui est
    // unique. Sert uniquement de filtre d'affichage rapide (voir
    // g_tagFilter/ModHasTag) ; aucune mise en forme particuliere n'est
    // imposee au-dela du decoupage par virgule/point-virgule.
    std::wstring tags;
    // Quelle API utiliser pour la verification/l'auto-remplissage de ce mod
    // (voir enum API_THUNDERSTORE/API_NEXUS/API_NONE ci-dessus). Remplace
    // l'ancienne case a cocher "Non Thunderstore" (toujours lue depuis un
    // valmods.json plus ancien pour compatibilite, voir LoadData).
    int apiSource;
    // Identifiant STABLE en memoire (jamais ecrit dans valmods.json), assigne
    // une seule fois a la construction (voir g_nextModUid). Sert a retrouver
    // LE mod exact apres un appel reseau ou un tri, au lieu de le retrouver
    // par son nom - indispensable des qu'il existe un doublon de nom (par
    // exemple le meme mod ajoute a deux modpacks differents comme deux
    // fiches separees) : chercher "le mod qui s'appelle X" retomberait
    // toujours sur la premiere fiche X trouvee, jamais la bonne en cas de
    // doublon, et une operation groupee ("Tout verifier"/"Tout DL") traiterait
    // deux fois la meme fiche en sautant totalement l'autre.
    int uid;
    Mod() : apiSource(API_THUNDERSTORE), uid(NextModUid()) {}
};

static HINSTANCE g_hInst = NULL;
static HWND  g_hMain = NULL, g_hTab = NULL;
static HWND  g_hCardsHost = NULL;       // panneau scrollable "cartes" (onglet Mods)
static HWND  g_hWorlds = NULL, g_hChars = NULL;
static HWND  g_hTooltip = NULL;
static HFONT g_font = NULL, g_fontBold = NULL;
static std::vector<Mod> g_mods;
static std::wstring g_valheimDir;
// Cle API personnelle Nexus Mods (Compte Nexus > Parametres > API Keys),
// necessaire pour toute requete vers l'API Nexus (contrairement a
// Thunderstore, dont l'API publique ne demande aucune cle). Reglee via le
// menu Parametres > "Definir la cle API Nexus...", persistee dans
// valmods.json - c'est un secret personnel, a ne jamais partager avec le
// fichier/le .exe si tu les envoies a un ami (voir DataFile()).
static std::wstring g_nexusApiKey;
// Date du dernier "Tout verifier", distincte de la date individuelle de
// chaque mod : le bulk check ne doit JAMAIS toucher au champ "last" propre
// a un mod (qui reste reserve a une verification personnelle, manuelle -
// c'est ce qui alimente le code couleur base sur l'anciennete). Persistee a
// part dans le JSON, jamais melangee aux dates par mod.
static std::wstring g_lastGlobalCheck;
// Active/desactive l'affichage de l'indicateur "1.0" sur les cartes. Coche
// par defaut (voir wWinMain), mais desactivable en un clic : juste apres la
// sortie de la 1.0, les mods ne vont pas tous se mettre a jour instantanement
// (il faut le temps que les moddeurs s'y mettent), donc l'indicateur
// passerait au orange presque partout sans que ce soit vraiment un
// probleme - le toggle permet d'eteindre ce bruit le temps que l'ecosysteme
// rattrape son retard, puis de le rallumer plus tard.
static bool g_showValheim10 = true;
// Filtre d'affichage par "modpack" : vide = tous les mods, sinon ne montre
// que ceux dont Mod::modpack correspond exactement. Persiste (voir JSON).
static std::wstring g_modpackFilter;
// Filtre d'affichage par tag : vide = tous les mods, sinon ne montre que
// ceux qui ont CE tag parmi les leurs (voir Mod::tags/ModHasTag). Persiste
// (voir JSON). Independant du filtre modpack : les deux s'appliquent
// ensemble (voir ModMatchesFilters).
static std::wstring g_tagFilter;
// Recherche texte libre (barre du haut) : vide = tous les mods, sinon ne
// montre que ceux dont un champ pertinent (nom, categorie, description,
// note, tags, modpack, lien) contient ce texte, sans tenir compte de la
// casse (voir ModMatchesSearch). Volontairement NON persiste dans
// valmods.json - une recherche ponctuelle n'a pas vocation a rester active
// silencieusement d'un lancement a l'autre (contrairement aux filtres
// modpack/tag, delibere et plus rares a changer).
static std::wstring g_searchQuery;
// Sur quels champs porte la recherche ci-dessus - une case a cocher par
// champ (barre du haut). Contrairement a g_searchQuery, CES reglages SONT
// persistes (voir JSON) : c'est une preference d'usage ("je ne cherche
// jamais dans les liens"), pas une recherche ponctuelle. Tous actives par
// defaut (comportement inchange si on ne touche a rien). Si la personne
// decoche TOUTES les cases, ModMatchesSearch retombe sur "tous les champs"
// plutot que de renvoyer silencieusement zero resultat, ce qui serait
// deroutant et difficile a comprendre sans lire un tooltip.
static bool g_searchInName = true;
static bool g_searchInCat = true;
static bool g_searchInDesc = true;
static bool g_searchInNote = true;
static bool g_searchInTags = true;
static bool g_searchInModpack = true;
static bool g_searchInUrl = true;
// Masque les mods dont le statut Thunderstore est "a jour" (vert) de
// l'affichage ET de "Tout DL" (mais PAS de "Tout verifier", dont le but est
// justement de decouvrir si le statut a change - le masquer la rendrait
// contre-productif). Voir ModPassesFilters/ModMatchesPack.
static bool g_hideUpToDate = false;
// Traduit une position de ligne VISIBLE (0, 1, 2... dans l'ordre des cartes
// effectivement affichees) vers son vrai index dans g_mods. Necessaire des
// qu'un filtre peut faire sauter des mods : sans ca, les boutons de carte
// (qui encodent RA_BASE + position*RA_COUNT) cibleraient le mauvais mod.
// Reconstruit a chaque RefillMods().
static std::vector<int> g_visibleIndices;
static int  g_sortCol = 0;
static bool g_sortAsc = true;
static bool g_lastListIsWorld = true;   // pour le bouton backup
static int  g_ctxMenuModIndex = -1;     // cible du menu "..." (voir ShowRowOverflowMenu)

static ULONG_PTR g_gdiplusToken = 0;
static HICON g_defaultIcon = NULL;
static std::map<std::wstring, HICON> g_iconCache;   // chemin icone -> icone chargee (GDI+)
// Taille des icones affichees sur les cartes (voir RefillMods) - agrandie de
// 40 a 64px : il y a largement la place verticalement dans la carte (voir
// CARD_H plus bas), et une icone plus grande se distingue mieux d'un mod a
// l'autre en un coup d'oeil. Utilisee par GetOrLoadHIcon/MakeDefaultIcon
// ci-dessous ET par RefillMods pour la taille reelle du controle a l'ecran.
static const int CARD_ICON_SIZE = 64;

// controles crees dynamiquement pour chaque carte de mod (detruits et
// recrees a chaque RefillMods). "stretch" = la largeur est recalculee a
// chaque redimensionnement/scroll pour occuper le panneau (nom/details/
// note/separateur) ; sinon la largeur reste fixe (icone, boutons).
struct CardChild { HWND hwnd; int x, y, w, h; bool stretch; };
static std::vector<CardChild> g_cardChildren;
static int g_cardTotalHeight = 0;
static int g_scrollPos = 0;

// la taille doit rester >= au nombre d'elements pousses dans WM_CREATE.
static HWND g_pageMods[32];  static int g_nMods = 0;
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
// true si le chemin est absolu (lettre de lecteur "C:\" ou chemin UNC
// "\\serveur\partage") - un chemin relatif ("icons\foo.png") ne l'est pas.
static bool IsAbsolutePath(const std::wstring& p) {
    if (p.size() >= 2 && p[1] == L':') return true;                 // "C:\..."
    if (p.size() >= 2 && p[0] == L'\\' && p[1] == L'\\') return true; // "\\serveur\..."
    return false;
}
// Transforme un chemin d'icone TEL QUE STOCKE dans valmods.json en chemin
// utilisable pour charger le fichier : un chemin relatif (ex: "icons"
// suivi de "Foo-Bar.png") est resolu par rapport au dossier de l'exe, un chemin
// absolu est utilise tel quel (icone choisie hors du dossier de l'appli -
// ne pourra de toute facon pas voyager avec le .exe si on l'envoie a un
// ami, voir StoreIconPath ci-dessous qui essaie d'eviter ce cas).
static std::wstring ResolveIconPath(const std::wstring& stored) {
    if (stored.empty() || IsAbsolutePath(stored)) return stored;
    return ExeDir() + L"\\" + stored;
}
// Chemin a ENREGISTRER dans valmods.json pour une icone : si le fichier se
// trouve deja quelque part sous le dossier de l'exe (typiquement dans
// icons\, mais pas force), on stocke un chemin RELATIF a l'exe - ainsi
// valmods.json + valmods.exe + le dossier icons\ peuvent etre envoyes tels
// quels a quelqu'un d'autre sans que les icones cassent (un chemin absolu
// du genre "C:\Users\Toi\Pictures\mod.png" ne survivrait pas au transfert,
// vu que ce dossier n'existe pas chez le destinataire). Si le fichier est
// ailleurs, on garde le chemin absolu tel quel (rien d'autre a faire).
static std::wstring StoreIconPath(const std::wstring& fullPath) {
    if (fullPath.empty()) return fullPath;
    std::wstring dir = ExeDir();
    std::wstring prefix = dir + L"\\";
    // Comparaison insensible a la casse sur les N premiers caracteres
    // (lstrcmpiW ne prend pas de longueur, d'ou la sous-chaine prealable) -
    // Windows ne distinguant pas la casse des lettres de lecteur/chemins.
    if (fullPath.size() > prefix.size() &&
        lstrcmpiW(fullPath.substr(0, prefix.size()).c_str(), prefix.c_str()) == 0) {
        return fullPath.substr(prefix.size());
    }
    return fullPath;
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
//
// On NE PASSE PAS par Gdiplus::Bitmap::GetHICON() : cette fonction est connue
// pour produire des icones noires ou totalement invisibles des qu'un canal
// alpha est utilise, sur pas mal de configurations Windows (probleme GDI+
// documente, pas specifique a ce code). A la place, AlphaIconBuilder dessine
// dans un DIB section 32bpp premultiplie qu'on connait a l'avance, puis
// construit l'icone a la main via CreateIconIndirect - la methode fiable
// pour une icone avec vraie transparence.
struct AlphaIconBuilder {
    HBITMAP hbm;
    void* bits;
    Gdiplus::Bitmap* gdiBmp;
    int size;
    explicit AlphaIconBuilder(int sz) : hbm(NULL), bits(NULL), gdiBmp(NULL), size(sz) {
        BITMAPV5HEADER bi; memset(&bi, 0, sizeof(bi));
        bi.bV5Size = sizeof(BITMAPV5HEADER);
        bi.bV5Width = size;
        bi.bV5Height = -size;            // negatif = top-down, evite tout flip manuel
        bi.bV5Planes = 1;
        bi.bV5BitCount = 32;
        bi.bV5Compression = BI_BITFIELDS;
        bi.bV5RedMask   = 0x00FF0000;
        bi.bV5GreenMask = 0x0000FF00;
        bi.bV5BlueMask  = 0x000000FF;
        bi.bV5AlphaMask = 0xFF000000;
        HDC screenDc = GetDC(NULL);
        hbm = CreateDIBSection(screenDc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
        ReleaseDC(NULL, screenDc);
        if (hbm && bits) {
            memset(bits, 0, (size_t)size * (size_t)size * 4);   // transparent au depart
            // PixelFormat32bppPARGB (alpha premultipliee) : le format attendu
            // par un HBITMAP consomme ensuite comme icone a canal alpha.
            gdiBmp = new Gdiplus::Bitmap(size, size, size * 4, PixelFormat32bppPARGB, (BYTE*)bits);
        }
    }
    ~AlphaIconBuilder() {
        delete gdiBmp;
        if (hbm) DeleteObject(hbm);
    }
    bool ok() const { return hbm && bits && gdiBmp && gdiBmp->GetLastStatus() == Gdiplus::Ok; }
    // hbmColor est REUTILISE tel quel par CreateIconIndirect (qui en fait sa
    // propre copie interne) : on peut le detruire nous-memes juste apres,
    // le destructeur s'en charge normalement.
    HICON ToIcon() {
        if (!ok()) return NULL;
        HBITMAP hMask = CreateBitmap(size, size, 1, 1, NULL);
        if (!hMask) return NULL;
        ICONINFO ii; memset(&ii, 0, sizeof(ii));
        ii.fIcon = TRUE;
        ii.hbmColor = hbm;
        ii.hbmMask = hMask;
        HICON hIcon = CreateIconIndirect(&ii);
        DeleteObject(hMask);
        return hIcon;
    }
};

static HICON MakeDefaultIcon(int size) {
    AlphaIconBuilder b(size);
    if (!b.ok()) return NULL;
    Gdiplus::Graphics g(b.gdiBmp);
    // SourceCopy plutot que le SourceOver par defaut : remplace les pixels
    // au lieu de les meler a un fond deja transparent - ecarte tout risque
    // de mauvaise propagation du canal alpha sur une surface PARGB (voir
    // le meme choix, plus critique, dans LoadScaledIconFromFile).
    g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush fill(Gdiplus::Color(255, 100, 100, 112));
    Gdiplus::Pen pen(Gdiplus::Color(255, 60, 60, 70), 1.0f);
    float pad = 1.0f, s = (float)size;
    g.FillRectangle(&fill, pad, pad, s - 2 * pad, s - 2 * pad);
    g.DrawRectangle(&pen, pad, pad, s - 2 * pad, s - 2 * pad);
    return b.ToIcon();
}
static HICON LoadScaledIconFromFile(const std::wstring& path, int size) {
    if (path.empty() || !FileExists(path)) return NULL;
    Gdiplus::Bitmap* src = Gdiplus::Bitmap::FromFile(path.c_str(), FALSE);
    if (!src) return NULL;
    if (src->GetLastStatus() != Gdiplus::Ok || src->GetWidth() == 0 || src->GetHeight() == 0) {
        delete src;
        return NULL;
    }
    AlphaIconBuilder b(size);
    if (!b.ok()) { delete src; return NULL; }
    Gdiplus::Graphics g(b.gdiBmp);
    // Cf. MakeDefaultIcon : SourceCopy est ici encore plus important, car
    // DrawImage (avec redimensionnement) est plus expose que de simples
    // remplissages a une mauvaise gestion de l'alpha en mode "over" par
    // certaines versions de GDI+ - c'est le suspect le plus probable pour
    // une icone chargee qui reste invisible/transparente alors que l'icone
    // par defaut (simples formes pleines) s'affiche correctement.
    g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    float sw = (float)src->GetWidth(), sh = (float)src->GetHeight();
    float scale = (sw > sh) ? (float)size / sw : (float)size / sh;   // conserve le ratio
    float dw = sw * scale, dh = sh * scale;
    float dx = ((float)size - dw) / 2.0f, dy = ((float)size - dh) / 2.0f;
    g.DrawImage(src, dx, dy, dw, dh);
    delete src;
    return b.ToIcon();
}
// Icone (CARD_ICON_SIZE x CARD_ICON_SIZE, taille des cartes) pour ce chemin, chargee et mise en cache
// au premier appel ; un chemin vide ou illisible retombe sur l'icone par
// defaut PARTAGEE (g_defaultIcon) SANS la mettre en cache, pour ne jamais
// risquer de detruire deux fois le meme HICON au nettoyage final (voir
// wWinMain) - seuls les chargements reussis, chacun un HICON distinct,
// entrent dans g_iconCache. Le cache n'est pas invalide si le fichier
// change sur disque a chemin egal - re-parcourir le fichier dans l'editeur
// force un rechargement (nouveau HICON, ancien jamais libere avant la fin
// de l'appli : negligeable pour le nombre d'icones qu'un usage normal genere).
static HICON GetOrLoadHIcon(const std::wstring& path) {
    if (path.empty()) return g_defaultIcon;
    // le cache est cle par le chemin STOCKE (potentiellement relatif) : un
    // meme mod repasse toujours le meme chemin, la resolution (voir
    // ResolveIconPath) n'a besoin d'etre faite qu'au moment du chargement
    // reel du fichier.
    std::map<std::wstring, HICON>::iterator it = g_iconCache.find(path);
    if (it != g_iconCache.end()) return it->second;
    HICON hi = LoadScaledIconFromFile(ResolveIconPath(path), CARD_ICON_SIZE);
    if (!hi) return g_defaultIcon;
    g_iconCache[path] = hi;
    return hi;
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
static std::wstring DownloadsRoot() { return ExeDir() + L"\\downloads"; }

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
//     "version": 14,
//     "valheimDir": "D:\\SteamLibrary\\steamapps\\common\\Valheim",
//     "nexusApiKey": "",
//     "lastGlobalCheck": "2026-08-19 14:30",
//     "showValheim10Check": true,
//     "modpackFilter": "",
//     "tagFilter": "",
//     "hideUpToDate": false,
//     "searchInName": true, "searchInCategory": true, "searchInDescription": true,
//     "searchInNote": true, "searchInTags": true, "searchInModpack": true,
//     "searchInUrl": true,
//     "mods": [
//       { "name": "...", "category": "...", "url": "...",
//         "changelogUrl": "...",
//         "modDir": "D:\\...\\BepInEx\\plugins\\NomDuMod",
//         "configPath": "D:\\...\\BepInEx\\config\\NomDuMod.cfg",
//         "iconPath": "icons\\Mod.png",
//         "tsVersion": "1.3.0", "tsLatestDate": "2026-03-26T21:26:57Z",
//         "installedVersion": "1.3.0",
//         "description": "...", "modpack": "Serveur du vendredi",
//         "tags": "QoL, Building",
//         "apiSource": "thunderstore",   // "thunderstore" / "nexus" / "hexium" / "none"
//         "nonThunderstore": false,
//         "lastCheck": "2026-08-19 14:30", "note": "..." }
//     ]
//   }
// "modDir" remplace l'ancien "dllPath" (v13 et anterieur) : celui-ci
// pointait vers le fichier .dll exact, celui-la pointe vers le DOSSIER
// d'installation du mod (le DLL a l'interieur est retrouve automatiquement,
// voir ResolveModDll). Un valmods.json v13 est lu normalement : si
// "modDir" est absent, "dllPath" (ou ses anciens alias "dll"/"path"/
// "dllFile") est repris et, s'il pointe vers un fichier plutot qu'un
// dossier, son dossier parent est utilise (voir ParseModValue).
static std::wstring DataFile()   { return ExeDir() + L"\\valmods.json"; }
static std::wstring LegacyFile() { return ExeDir() + L"\\valmods.tsv"; }

// "thunderstore" / "nexus" / "hexium" / "none" - lisible a la main dans le
// JSON, contrairement a un simple entier.
static const char* ApiSourceName(int s) {
    switch (s) {
        case API_NEXUS:  return "nexus";
        case API_HEXIUM: return "hexium";
        case API_NONE:   return "none";
        default:         return "thunderstore";
    }
}
static int ApiSourceFromName(const std::string& s) {
    if (s == "nexus")  return API_NEXUS;
    if (s == "hexium") return API_HEXIUM;
    if (s == "none")   return API_NONE;
    return API_THUNDERSTORE;
}
static void SaveMods() {
    std::string out;
    out += "{\n";
    out += "  \"version\": " + std::to_string(VALMODS_JSON_SCHEMA_VERSION) + ",\n";
    out += "  \"valheimDir\": " + mj::quote(W2U(g_valheimDir)) + ",\n";
    // Cle API Nexus : un secret personnel. Rappel volontaire ici pour que
    // quiconope ouvre valmods.json a la main (ou lise ce fichier) tombe
    // dessus : a retirer/vider avant d'envoyer valmods.json a quelqu'un
    // d'autre (voir menu Parametres > "Definir la cle API Nexus...").
    out += "  \"nexusApiKey\": " + mj::quote(W2U(g_nexusApiKey)) + ",\n";
    out += "  \"lastGlobalCheck\": " + mj::quote(W2U(g_lastGlobalCheck)) + ",\n";
    out += std::string("  \"showValheim10Check\": ") + (g_showValheim10 ? "true" : "false") + ",\n";
    out += "  \"modpackFilter\": " + mj::quote(W2U(g_modpackFilter)) + ",\n";
    out += "  \"tagFilter\": " + mj::quote(W2U(g_tagFilter)) + ",\n";
    out += std::string("  \"hideUpToDate\": ") + (g_hideUpToDate ? "true" : "false") + ",\n";
    // Cases "sur quoi chercher" (voir g_searchIn*/ModMatchesSearch) - une
    // preference d'usage persistee, contrairement au texte de recherche
    // lui-meme (g_searchQuery), volontairement non enregistre (voir plus haut).
    out += std::string("  \"searchInName\": ")     + (g_searchInName ? "true" : "false") + ",\n";
    out += std::string("  \"searchInCategory\": ") + (g_searchInCat ? "true" : "false") + ",\n";
    out += std::string("  \"searchInDescription\": ") + (g_searchInDesc ? "true" : "false") + ",\n";
    out += std::string("  \"searchInNote\": ")     + (g_searchInNote ? "true" : "false") + ",\n";
    out += std::string("  \"searchInTags\": ")     + (g_searchInTags ? "true" : "false") + ",\n";
    out += std::string("  \"searchInModpack\": ")  + (g_searchInModpack ? "true" : "false") + ",\n";
    out += std::string("  \"searchInUrl\": ")      + (g_searchInUrl ? "true" : "false") + ",\n";
    out += "  \"mods\": [\n";
    for (size_t i = 0; i < g_mods.size(); ++i) {
        const Mod& m = g_mods[i];
        out += "    {\n";
        out += "      \"name\":             " + mj::quote(W2U(m.name))             + ",\n";
        out += "      \"category\":         " + mj::quote(W2U(m.cat))              + ",\n";
        out += "      \"url\":              " + mj::quote(W2U(m.url))              + ",\n";
        out += "      \"changelogUrl\":     " + mj::quote(W2U(m.changelogUrl))     + ",\n";
        out += "      \"modDir\":           " + mj::quote(W2U(m.modDir))            + ",\n";
        out += "      \"configPath\":       " + mj::quote(W2U(m.configPath))        + ",\n";
        // Chemin d'icone : relatif au dossier de l'exe quand possible (voir
        // StoreIconPath), donc PORTABLE si on envoie valmods.json +
        // valmods.exe + le dossier icons\ ensemble a un ami.
        out += "      \"iconPath\":         " + mj::quote(W2U(m.iconPath))         + ",\n";
        out += "      \"tsVersion\":        " + mj::quote(W2U(m.tsVersion))        + ",\n";
        out += "      \"tsLatestDate\":     " + mj::quote(W2U(m.tsLatestDate))     + ",\n";
        out += "      \"installedVersion\": " + mj::quote(W2U(m.installedVersion)) + ",\n";
        out += "      \"description\":      " + mj::quote(W2U(m.description))      + ",\n";
        out += "      \"modpack\":          " + mj::quote(W2U(m.modpack))          + ",\n";
        out += "      \"tags\":             " + mj::quote(W2U(m.tags))             + ",\n";
        out += std::string("      \"apiSource\":       ") + mj::quote(ApiSourceName(m.apiSource)) + ",\n";
        // "nonThunderstore" garde en ecriture uniquement pour qu'un ancien
        // valmods.json (versions < 2.1, avant l'ajout de Nexus) reste
        // lisible si jamais ce fichier est rouvert par cette meme copie de
        // l'appli apres un retour en arriere ; ignore a la lecture des que
        // "apiSource" est present (voir LoadData).
        out += std::string("      \"nonThunderstore\": ") + (m.apiSource == API_NONE ? "true" : "false") + ",\n";
        out += "      \"lastCheck\":        " + mj::quote(W2U(m.last))             + ",\n";
        out += "      \"note\":             " + mj::quote(W2U(m.note))             + "\n";
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

// Lit une chaine dans v en essayant plusieurs noms de cle possibles, dans
// l'ordre - le premier trouve (non vide) l'emporte. Sert a la RETRO-
// COMPATIBILITE avec un valmods.json cree par une version de l'appli dont
// on ne connait pas exactement le schema (ex: la toute premiere version
// personnelle de quelqu'un, avant que ce fichier ne documente le format
// "officiel" ci-dessus) : plutot que de perdre silencieusement un champ
// dont le nom aurait change, on tente quelques graphies plausibles.
static std::wstring FieldAny(const mj::Value& v, std::initializer_list<const char*> keys) {
    for (std::initializer_list<const char*>::const_iterator it = keys.begin(); it != keys.end(); ++it) {
        const mj::Value* f = v.find(*it);
        if (f && f->type == mj::STR && !f->str.empty()) return Clean(U2W(f->str));
    }
    return L"";
}
// Lit un booleen a la racine, avec une valeur par defaut explicite si le
// champ est absent (ou d'un type inattendu) - utilise pour les reglages
// dont l'absence doit signifier "comportement habituel" plutot que
// "false" (ex: les cases "sur quoi chercher", voir g_searchIn*, absentes
// d'un valmods.json plus ancien mais qui doivent quand meme démarrer TOUTES
// cochees).
static bool BoolField(const mj::Value& root, const char* key, bool def) {
    const mj::Value* f = root.find(key);
    return (f && f->type == mj::BOOL) ? f->b : def;
}

// Construit un Mod a partir d'un element de la liste "mods" - qu'il s'agisse
// d'un objet complet (format habituel) ou d'une simple chaine (URL nue, au
// cas ou un tout premier format de ValMods ait stocke une liste de liens
// plutot que des objets). Renvoie un Mod vide (nom et url vides) si
// l'element n'est exploitable d'aucune de ces deux facons.
static Mod ParseModValue(const mj::Value& v) {
    Mod m;
    if (v.type == mj::STR) {
        // element = juste un lien ; on s'en sert aussi comme nom provisoire,
        // modifiable ensuite dans l'editeur (Modifier).
        m.url = Clean(U2W(v.str));
        m.name = m.url;
        return m;
    }
    if (v.type != mj::OBJ) return m;   // ni objet ni chaine : rien a en tirer

    m.name         = FieldAny(v, {"name", "modName", "title"});
    m.cat          = FieldAny(v, {"category", "cat", "categorie", "author", "auteur"});
    m.url          = FieldAny(v, {"url", "link", "pageUrl", "modUrl", "page"});
    m.changelogUrl = FieldAny(v, {"changelogUrl", "historyUrl", "changelog", "history"});
    // "modDir" (nouveau, v14+) est le dossier d'installation du mod ;
    // l'ancien "dllPath" (v13 et anterieur, ainsi que ses alias "dll"/
    // "path"/"dllFile") pointait vers le fichier .dll exact - on le
    // reprend en repli, et s'il s'agit visiblement d'un chemin de FICHIER
    // (extension .dll) on ne garde que son dossier parent, puisque c'est
    // desormais ca qui est attendu (voir ResolveModDll).
    m.modDir       = FieldAny(v, {"modDir", "modFolder", "installDir"});
    if (m.modDir.empty()) {
        std::wstring legacyDll = FieldAny(v, {"dllPath", "dll", "path", "dllFile"});
        if (!legacyDll.empty()) {
            size_t n = legacyDll.size();
            bool looksLikeDllFile = (n > 4 && lstrcmpiW(legacyDll.c_str() + n - 4, L".dll") == 0);
            if (looksLikeDllFile) {
                size_t p = legacyDll.find_last_of(L"\\/");
                m.modDir = (p == std::wstring::npos) ? L"" : legacyDll.substr(0, p);
            } else {
                m.modDir = legacyDll;   // deja un dossier (ou chemin ambigu) : on le garde tel quel
            }
        }
    }
    m.configPath   = FieldAny(v, {"configPath", "config", "cfgPath", "configFile"});
    m.iconPath     = FieldAny(v, {"iconPath", "icon", "image", "iconFile"});
    m.tsVersion    = FieldAny(v, {"tsVersion", "latestVersion", "lastVersion"});
    m.tsLatestDate = FieldAny(v, {"tsLatestDate", "latestDate", "publishedDate"});
    m.installedVersion = FieldAny(v, {"installedVersion", "installed", "myVersion", "currentVersion"});
    m.description  = FieldAny(v, {"description", "desc", "summary"});
    m.modpack      = FieldAny(v, {"modpack", "pack", "group", "collection"});
    m.tags         = FieldAny(v, {"tags", "tag", "labels"});
    m.last         = FieldAny(v, {"lastCheck", "last", "lastChecked", "checkedAt", "dateChecked"});
    m.note         = FieldAny(v, {"note", "notes", "comment", "remark", "remarque"});

    // "apiSource" (nouveau) prioritaire ; repli sur l'ancienne case
    // "nonThunderstore" pour un valmods.json cree par une version anterieure
    // a l'ajout de Nexus (false -> API Thunderstore comme avant, true ->
    // desactive).
    const mj::Value* apiSrc = v.find("apiSource");
    if (apiSrc && apiSrc->type == mj::STR) {
        m.apiSource = ApiSourceFromName(apiSrc->str);
    } else {
        const mj::Value* nts = v.find("nonThunderstore");
        m.apiSource = (nts && nts->type == mj::BOOL && nts->b) ? API_NONE : API_THUNDERSTORE;
    }

    // Un valmods.json plus ancien peut contenir un chemin d'icone ABSOLU qui
    // se trouve deja sous le dossier de l'exe (ex: telecharge par un ancien
    // Auto-remplir) : on le relativise a la volee pour beneficier de la
    // portabilite (voir StoreIconPath) sans que l'utilisateur ait a rouvrir
    // chaque mod dans l'editeur.
    m.iconPath = StoreIconPath(m.iconPath);
    return m;
}

// Cherche la liste des mods sous plusieurs noms de cle possibles a la
// racine (format habituel : "mods" ; quelques variantes plausibles pour un
// tres ancien fichier). Renvoie NULL si aucune des cles connues ne contient
// un tableau.
static const mj::Value* FindModsArray(const mj::Value& root) {
    static const char* candidates[] = { "mods", "modList", "modlist", "list", "items" };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        const mj::Value* v = root.find(candidates[i]);
        if (v && v->type == mj::ARR) return v;
    }
    return NULL;
}

static void LoadData() {
    g_mods.clear();
    g_valheimDir.clear();
    std::string raw;

    if (ReadAllBytes(DataFile(), raw)) {
        mj::Value root;
        bool parsed = mj::parse(raw, root);
        // Un fichier valide mais de FORME inattendue (racine directement un
        // tableau de mods, schema plus ancien...) n'est PAS un fichier
        // corrompu - seule une erreur de syntaxe JSON authentique merite le
        // traitement "fichier corrompu, mis de cote" ci-dessous. Distinguer
        // les deux evite de renommer en .bad (et donc de faire "disparaitre"
        // aux yeux de l'appli) un fichier par ailleurs parfaitement lisible,
        // juste parce qu'il ne correspond pas exactement au schema attendu.
        if (parsed && (root.type == mj::OBJ || root.type == mj::ARR)) {
            // Schema d'origine du fichier (0 si absent - fichier d'une
            // version qui ne stockait pas encore ce champ, ou racine sous
            // forme de simple tableau) : sert uniquement a decider si le
            // message de mise a jour ci-dessous doit s'afficher, PAS a
            // choisir comment lire les champs (chaque champ est de toute
            // facon lu independamment, avec repli sur une valeur par defaut
            // s'il est absent - voir ParseModValue/FieldAny).
            int oldSchemaVersion = 0;
            const mj::Value* modsNode = NULL;

            if (root.type == mj::OBJ) {
                const mj::Value* verNode = root.find("version");
                if (verNode && verNode->type == mj::NUM) oldSchemaVersion = (int)verNode->num;

                g_valheimDir = U2W(root.s("valheimDir"));
                g_nexusApiKey = Clean(U2W(root.s("nexusApiKey")));
                g_lastGlobalCheck = U2W(root.s("lastGlobalCheck"));
                const mj::Value* show10 = root.find("showValheim10Check");
                g_showValheim10 = (show10 && show10->type == mj::BOOL) ? show10->b : true;
                g_modpackFilter = U2W(root.s("modpackFilter"));
                g_tagFilter = U2W(root.s("tagFilter"));
                const mj::Value* hideUp = root.find("hideUpToDate");
                g_hideUpToDate = (hideUp && hideUp->type == mj::BOOL && hideUp->b);

                // Cases "sur quoi chercher" : absentes (ancien fichier) =
                // TRUE par defaut, pour ne rien exclure d'une recherche tant
                // que la personne n'a pas explicitement decoche quoi que ce
                // soit (voir g_searchIn*).
                g_searchInName    = BoolField(root, "searchInName", true);
                g_searchInCat     = BoolField(root, "searchInCategory", true);
                g_searchInDesc    = BoolField(root, "searchInDescription", true);
                g_searchInNote    = BoolField(root, "searchInNote", true);
                g_searchInTags    = BoolField(root, "searchInTags", true);
                g_searchInModpack = BoolField(root, "searchInModpack", true);
                g_searchInUrl     = BoolField(root, "searchInUrl", true);

                modsNode = FindModsArray(root);
            } else {
                // root.type == ARR : le fichier EST directement la liste de
                // mods (format "plat", sans objet englobant) - aucun reglage
                // racine a lire, tout part des valeurs par defaut.
                modsNode = &root;
            }

            if (modsNode) {
                for (size_t i = 0; i < modsNode->arr.size(); ++i) {
                    Mod m = ParseModValue(modsNode->arr[i]);
                    if (!m.name.empty() || !m.url.empty()) g_mods.push_back(m);
                }
            }

            // Fichier d'un schema plus ancien (ou sans numero de schema du
            // tout) : on le signale une seule fois - la sauvegarde qui suit
            // (SaveMods() en fin de fonction) le fera passer au schema
            // actuel, donc ce message ne reapparaitra plus aux lancements
            // suivants. Une copie de l'original est gardee a cote (.premigration)
            // au cas ou la lecture ci-dessus aurait rate quelque chose.
            // Seuil compare a VALMODS_JSON_SCHEMA_VERSION (PAS un nombre
            // litteral en dur) : sinon ce seuil reste fige a l'ancienne
            // valeur du schema a chaque bump ulterieur (ex: apiSource en
            // v13, modDir/configPath en v14) et cesse de declencher le
            // message/backup pour des fichiers pourtant plus vieux que le
            // format actuel - ce qui s'est deja produit une fois ici.
            if (root.type == mj::ARR || oldSchemaVersion < VALMODS_JSON_SCHEMA_VERSION) {
                std::wstring bak = DataFile() + L".premigration";
                if (!FileExists(bak)) CopyFileW(DataFile().c_str(), bak.c_str(), FALSE);
                wchar_t countMsg[64];
                wsprintfW(countMsg, L"%d mod(s) repris.", (int)g_mods.size());
                std::wstring msg =
                    L"valmods.json vient d'un format plus ancien (ou different) de ValMods.\n\n";
                msg += countMsg;
                msg +=
                    L"\nIl va etre enregistre au format actuel : les champs qui manquaient\n"
                    L"ont recu des valeurs par defaut, rien n'a ete supprime.\n\n"
                    L"Une copie de l'original a ete gardee a cote, au cas ou :\n"
                    L"valmods.json.premigration\n\n"
                    L"Si des mods de ta liste manquent malgre tout ci-dessous, tu peux\n"
                    L"comparer avec cette copie et les rajouter a la main (bouton Ajouter).";
                MessageBoxW(NULL, msg.c_str(), L"ValMods", MB_OK | MB_ICONINFORMATION);
            }
        } else {
            // erreur de syntaxe JSON authentique (ou racine d'un type qui ne
            // peut de toute facon rien contenir, ex: juste un nombre/texte
            // seul) : on met le fichier de cote plutot que de l'ecraser.
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
        case COL_DLL: r = lstrcmpiW(a.modDir.c_str(), b.modDir.c_str()); break;
        case COL_TSVER: r = lstrcmpiW(a.tsVersion.c_str(), b.tsVersion.c_str()); break;
        case COL_URL: r = lstrcmpiW(a.url.c_str(), b.url.c_str()); break;
        case COL_SOURCE: {
            r = (a.apiSource > b.apiSource) ? 1 : (a.apiSource < b.apiSource ? -1 : 0);
            if (r == 0) r = lstrcmpiW(a.name.c_str(), b.name.c_str());
            break;
        }
        default:      r = lstrcmpiW(a.note.c_str(), b.note.c_str()); break;
    }
    return g_sortAsc ? (r < 0) : (r > 0);
}

// ---------------------------------------------------------------- DLL lie
static std::wstring DllFileName(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? path : path.substr(p + 1);
}
// Cherche tous les .dll sous un dossier, jusqu'a maxDepth niveaux de
// sous-dossiers (0 = seulement le dossier lui-meme) - la plupart des mods
// BepInEx extraient leur DLL directement dans le dossier du mod, mais
// certains le nichent un ou deux niveaux plus bas (ex: un sous-dossier
// "plugin" ou une structure de repo copiee telle quelle). On s'arrete a
// depth<=2 et a 64 fichiers trouves pour ne pas partir scanner tout un
// disque si l'utilisateur pointe par erreur sur un dossier bien plus haut.
static void FindDllsInDirRec(const std::wstring& dir, int depth, std::vector<std::wstring>& out) {
    if (depth < 0 || out.size() >= 64) return;
    WIN32_FIND_DATAW fd;
    std::wstring pattern = dir + L"\\*";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring full = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth > 0) FindDllsInDirRec(full, depth - 1, out);
        } else {
            size_t n = wcslen(fd.cFileName);
            if (n > 4 && lstrcmpiW(fd.cFileName + n - 4, L".dll") == 0) out.push_back(full);
        }
        if (out.size() >= 64) break;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static std::vector<std::wstring> FindDllsInDir(const std::wstring& dir) {
    std::vector<std::wstring> out;
    if (!DirExists(dir)) return out;
    FindDllsInDirRec(dir, 2, out);
    return out;
}
// Choisit LE dll a considerer comme "celui du mod" parmi tous ceux trouves
// sous le dossier : priorite a un nom de fichier qui contient le nom du mod
// (ou l'inverse), sinon le premier par ordre alphabetique - un choix
// arbitraire mais stable (evite qu'il change de mod en mod selon l'ordre
// du systeme de fichiers).
static std::wstring PickBestDll(const Mod& m, const std::vector<std::wstring>& dlls) {
    if (dlls.empty()) return L"";
    if (dlls.size() == 1) return dlls[0];
    std::wstring nameLower = m.name;
    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::towlower);
    std::wstring best; 
    for (size_t i = 0; i < dlls.size(); ++i) {
        std::wstring fnLower = DllFileName(dlls[i]);
        std::transform(fnLower.begin(), fnLower.end(), fnLower.begin(), ::towlower);
        if (!nameLower.empty() &&
            (fnLower.find(nameLower) != std::wstring::npos || nameLower.find(fnLower) != std::wstring::npos)) {
            if (best.empty() || lstrcmpiW(dlls[i].c_str(), best.c_str()) < 0) best = dlls[i];
        }
    }
    if (!best.empty()) return best;
    best = dlls[0];
    for (size_t i = 1; i < dlls.size(); ++i)
        if (lstrcmpiW(dlls[i].c_str(), best.c_str()) < 0) best = dlls[i];
    return best;
}
// Resout le(s) DLL du dossier associe a un mod (voir Mod::modDir). countOut,
// si fourni, recoit le nombre total de DLL trouves (utile pour distinguer
// "aucun DLL dans ce dossier" de "plusieurs DLL, on en a choisi un").
static std::wstring ResolveModDll(const Mod& m, size_t* countOut = NULL) {
    if (m.modDir.empty()) { if (countOut) *countOut = 0; return L""; }
    std::vector<std::wstring> dlls = FindDllsInDir(m.modDir);
    if (countOut) *countOut = dlls.size();
    return PickBestDll(m, dlls);
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
// Version consideree comme "installee", pour toutes les comparaisons
// (statut affiche sur la carte, "Tout verifier") : priorite a
// Mod::installedVersion (suivie explicitement, mise a jour par
// Telecharger/Tout DL ou modifiee a la main), repli sur la version
// EMBARQUEE dans le fichier DLL seulement si ce champ est vide - beaucoup
// de DLL de mods Unity/BepInEx n'ont pas de ressource de version fiable,
// donc s'appuyer uniquement dessus donnait des faux "jamais verifie".
static std::wstring GetEffectiveInstalledVersion(const Mod& m) {
    if (!m.installedVersion.empty()) return m.installedVersion;
    std::wstring dll = ResolveModDll(m);
    if (dll.empty()) return L"";
    return GetDllVersionString(dll);
}
// missingOut, si fourni, est mis a true si un probleme concret existe
// (dossier renseigne mais introuvable, ou dossier existant sans aucun DLL
// dedans).
static std::wstring DllStatusText(const Mod& m, bool* missingOut) {
    if (missingOut) *missingOut = false;
    if (m.modDir.empty()) return L"-";
    if (!DirExists(m.modDir)) {
        if (missingOut) *missingOut = true;
        return L"dossier introuvable";
    }
    size_t count = 0;
    std::wstring dll = ResolveModDll(m, &count);
    if (dll.empty()) {
        if (missingOut) *missingOut = true;
        return L"aucun DLL dans le dossier";
    }
    std::wstring fn = DllFileName(dll);
    std::wstring ver = GetEffectiveInstalledVersion(m);
    std::wstring txt = ver.empty() ? fn : (fn + L" (v" + ver + L")");
    if (count > 1) txt += L" [+" + std::to_wstring(count - 1) + L"]";
    return txt;
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
// l'UTF-8 : reutilise a la fois pour le JSON de l'API, les icones PNG et les
// zips de mods (voir DownloadThunderstoreZip plus bas).
// maxBytes (0 = illimite) coupe le telechargement si la reponse depasse
// cette taille - garde-fou pour un zip anormalement gros plutot que de
// remplir la RAM sans limite.
// Timeouts volontairement courts (resolution/connexion 5s, envoi/reception 8s)
// pour ne pas bloquer l'interface trop longtemps en cas de reseau absent.
// extraHeaders (optionnel) : lignes d'en-tete supplementaires separees par
// "\r\n" (ex: L"apikey: xxxx\r\n") - utilise par l'API Nexus, qui exige une
// cle personnelle en en-tete (contrairement a Thunderstore, dont l'API
// publique ne demande aucune authentification).
static bool HttpGetBytes(const std::wstring& host, const std::wstring& path,
                         std::string& outBody, DWORD& outStatus, std::wstring& errOut,
                         size_t maxBytes = 0, const std::wstring& extraHeaders = L"")
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

    BOOL ok = WinHttpSendRequest(hRequest,
        extraHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : extraHeaders.c_str(),
        extraHeaders.empty() ? 0 : (DWORD)-1L,
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

    bool tooBig = false;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
        std::vector<char> buf(avail);
        DWORD got = 0;
        if (!WinHttpReadData(hRequest, &buf[0], avail, &got)) break;
        outBody.append(&buf[0], got);
        if (maxBytes && outBody.size() > maxBytes) { tooBig = true; break; }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (tooBig) {
        errOut = L"Reponse trop volumineuse (limite de securite depassee).";
        outBody.clear();
        return false;
    }
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
// Copie un fichier d'icone choisi par l'utilisateur (n'importe ou sur le
// disque) dans icons\ a cote de l'exe, pour que le chemin puisse ensuite
// etre stocke en RELATIF (voir StoreIconPath) - condition necessaire pour
// que valmods.json + valmods.exe + icons\ puissent voyager ensemble vers
// un autre PC sans que les icones se retrouvent cassees (un chemin absolu
// du style "C:\Users\Toi\Pictures\mod.png" ne pointerait vers rien chez un
// ami). Renvoie le chemin ABSOLU de la copie, ou "" en cas d'echec.
static std::wstring CopyIconIntoIconsDir(const std::wstring& srcPath) {
    if (srcPath.empty() || !FileExists(srcPath)) return L"";
    std::wstring dir = ExeDir() + L"\\icons";
    MakeDirs(dir);
    size_t p = srcPath.find_last_of(L"\\/");
    std::wstring base = SanitizeFileName((p == std::wstring::npos) ? srcPath : srcPath.substr(p + 1));
    std::wstring dest = dir + L"\\" + base;
    // deja la copie visee elle-meme (icone deja importee precedemment) :
    // rien a refaire.
    if (lstrcmpiW(dest.c_str(), srcPath.c_str()) == 0) return dest;
    if (!FileExists(dest))
        return CopyFileW(srcPath.c_str(), dest.c_str(), FALSE) ? dest : L"";
    // un autre fichier porte deja ce nom dans icons\ : on ajoute un
    // suffixe numerique jusqu'a trouver un nom libre.
    std::wstring stem = base, ext;
    size_t dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos) { stem = base.substr(0, dot); ext = base.substr(dot); }
    for (int n = 1; n < 1000; ++n) {
        wchar_t suf[16]; wsprintfW(suf, L"-%d", n);
        std::wstring cand = dir + L"\\" + stem + suf + ext;
        if (!FileExists(cand))
            return CopyFileW(srcPath.c_str(), cand.c_str(), FALSE) ? cand : L"";
    }
    return L"";
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
// L'API renvoie directement la derniere version publiee dans un objet
// "latest" (confirme sur une reponse reelle de l'API) - PAS dans un tableau
// "versions" a parcourir comme on pourrait le supposer. Renvoie NULL si
// l'objet est absent ou n'a pas de version_number exploitable (mod retire,
// reponse degradee...).
static const mj::Value* FindLatestEntry(const mj::Value& root, std::wstring& outVersion) {
    outVersion.clear();
    const mj::Value* latest = root.find("latest");
    if (!latest || latest->type != mj::OBJ) return NULL;
    outVersion = U2W(latest->s("version_number"));
    if (outVersion.empty()) return NULL;
    return latest;
}

struct TsCheckResult {
    bool ok;              // requete + parsing reussis, latestVersion exploitable
    bool isThunderstore;  // le lien du mod pointait bien vers thunderstore.io
    bool deprecated;
    std::wstring latestVersion;
    std::wstring latestDate;   // date_created (ISO8601) de la derniere version
    std::wstring description;
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
    const mj::Value* bestEntry = FindLatestEntry(f.root, latest);
    if (!bestEntry) {
        r.error = L"Numero de version introuvable dans la reponse (mod retire ou reponse degradee).";
        return r;
    }
    r.latestVersion = latest;
    r.description = Clean(U2W(bestEntry->s("description")));
    r.latestDate = Clean(U2W(bestEntry->s("date_created")));

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
    std::wstring name, category, changelogUrl, latestVersion, latestDate, localIconPath, description;
    TsAutofillResult() : ok(false), isThunderstore(false) {}
};
static TsAutofillResult FetchThunderstoreAutofill(const std::wstring& modUrl) {
    TsAutofillResult r;
    TsFetchResult f = FetchThunderstorePackage(modUrl);
    r.isThunderstore = f.isThunderstore;
    if (!f.isThunderstore) return r;
    if (!f.ok) { r.error = f.error; return r; }

    r.name = Clean(U2W(f.root.s("name")));

    // Thunderstore n'expose pas de liste de categories textuelle sur cet
    // endpoint (confirme sur une reponse reelle) - en revanche "owner" (le
    // namespace/l'equipe qui a publie le mod) EST present, c'est ce qu'on
    // utilise pour remplir le champ "Categorie / auteur" de l'editeur.
    // Repli sur le namespace extrait de l'URL si jamais "owner" manquait.
    r.category = Clean(U2W(f.root.s("owner")));
    if (r.category.empty()) {
        std::wstring ns, nm;
        if (ParseThunderstoreUrl(modUrl, ns, nm)) r.category = ns;
    }

    std::wstring latest;
    const mj::Value* bestEntry = FindLatestEntry(f.root, latest);
    if (!bestEntry) {
        r.error = L"Numero de version introuvable dans la reponse (mod retire ou reponse degradee).";
        return r;
    }
    r.latestVersion = latest;
    r.latestDate = Clean(U2W(bestEntry->s("date_created")));
    // Clean() aplatit les retours a la ligne en espaces : la description de
    // Thunderstore est souvent un paragraphe, mais chaque carte n'en montre
    // qu'une ligne tronquee de toute facon (voir RefillMods).
    r.description = Clean(U2W(bestEntry->s("description")));

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
                // stocke en RELATIF (voir StoreIconPath) : le fichier est deja
                // sous ExeDir()\icons, donc portable si on envoie valmods.json
                // + valmods.exe + icons\ a quelqu'un d'autre.
                if (WriteAllBytes(dest, bytes)) r.localIconPath = StoreIconPath(dest);
            }
            // l'icone est un bonus : si le telechargement echoue, on continue
            // quand meme avec le reste des champs plutot que de tout faire echouer.
        }
    }

    r.ok = true;
    return r;
}

// Telecharge le zip de la version demandee via l'URL de telechargement
// officielle de Thunderstore (celle utilisee par leur propre bouton
// "Download" sur la page du mod) :
//   https://thunderstore.io/package/download/{namespace}/{name}/{version}/
// Enregistre le zip dans downloads/ a cote de l'exe. Ne l'extrait JAMAIS
// automatiquement : ValMods reste un outil manuel, l'installation dans
// BepInEx/plugins se fait a la main.
static bool DownloadThunderstoreZip(const std::wstring& ns, const std::wstring& name,
                                    const std::wstring& version, const std::wstring& destPath,
                                    std::wstring& errOut)
{
    std::wstring path = L"/package/download/" + ns + L"/" + name + L"/" + version + L"/";
    std::string bytes; DWORD status = 0;
    const size_t maxBytes = 200u * 1024u * 1024u;   // 200 Mo, garde-fou de securite
    if (!HttpGetBytes(L"thunderstore.io", path, bytes, status, errOut, maxBytes)) {
        if (errOut.empty()) errOut = L"Echec du telechargement.";
        return false;
    }
    if (status == 404) {
        errOut = L"Version introuvable sur Thunderstore (peut-etre retiree entre temps).";
        return false;
    }
    if (status != 200) {
        wchar_t b[64]; wsprintfW(b, L"Thunderstore a repondu avec le code %lu.", (unsigned long)status);
        errOut = b;
        return false;
    }
    if (bytes.empty()) { errOut = L"Fichier telecharge vide."; return false; }
    if (!WriteAllBytes(destPath, bytes)) { errOut = L"Impossible d'ecrire le fichier sur le disque."; return false; }
    return true;
}

// ---------------------------------------------------------------- Hexium
// Verification en ligne via l'API publique de Hexium (aucune cle requise,
// voir valheim.hexium.gg/api/docs/ - c'est une API "compatible
// Thunderstore" au sens du format de package, mais PAS la meme forme
// d'endpoint : contrairement a Thunderstore (un GET par mod, qui renvoie
// directement le mod demande), Hexium n'expose qu'un seul endpoint de
// LISTE COMPLETE :
//   GET https://valheim.hexium.gg/api/v1/package/
// qui renvoie un tableau JSON de TOUS les mods Valheim (nom, equipe/owner,
// package_url, et un tableau "versions" par mod, la plus recente en tete,
// avec version_number/description/icon/download_url/date_created...). Il
// n'y a pas d'endpoint "un seul mod par equipe/nom" comme sur Thunderstore -
// juste "/api/v1/package/{uuid4}/" (par UUID, qu'on n'a pas a partir d'une
// simple URL de page mod) et "/api/v1/package-metrics/{namespace}/{name}/"
// (metriques seules, pas de version). On recupere donc la liste complete et
// on cherche dedans le mod dont owner/name correspondent a l'URL - mise en
// cache quelques minutes (g_hexiumPackageCache) pour eviter de retelecharger
// tout le catalogue a chaque mod verifie pendant un "Tout verifier".
// Fournit un download_url direct par version : contrairement a Nexus, le
// telechargement direct (bouton DL) reste donc disponible pour Hexium.

// Extrait equipe/nom d'une URL de page Hexium, ex:
// https://valheim.hexium.gg/mods/Azumatt/AzuExtendedPlayerInventory -> team=Azumatt, name=AzuExtendedPlayerInventory
static bool ParseHexiumUrl(const std::wstring& url, std::wstring& team, std::wstring& name) {
    if (url.find(L"hexium.gg") == std::wstring::npos) return false;
    size_t marker = url.find(L"/mods/");
    if (marker == std::wstring::npos) return false;
    std::wstring rest = url.substr(marker + 6);
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
    team = parts[0];
    name = parts[1];
    return true;
}

// Cache en memoire (jamais persiste) du catalogue complet Hexium, pour
// eviter un telechargement de plusieurs centaines de mods par appel quand
// "Tout verifier"/"Completer la liste" enchainent des dizaines de mods
// Hexium d'affilee. Duree de vie volontairement courte (5 minutes) : assez
// pour couvrir une seule operation groupee, pas assez pour risquer de
// travailler sur des donnees perimees d'une session a l'autre.
static mj::Value g_hexiumPackageCache;
static bool  g_hexiumCacheValid = false;
static DWORD g_hexiumCacheTick = 0;
static const DWORD HEXIUM_CACHE_TTL_MS = 5 * 60 * 1000;

static bool FetchHexiumPackageList(std::wstring& errOut) {
    DWORD now = GetTickCount();
    if (g_hexiumCacheValid && (now - g_hexiumCacheTick) < HEXIUM_CACHE_TTL_MS) return true;

    std::string body; DWORD status = 0;
    if (!HttpGetBytes(L"valheim.hexium.gg", L"/api/v1/package/", body, status, errOut,
                       64u * 1024u * 1024u)) {   // 64 Mo, garde-fou (catalogue complet)
        if (errOut.empty()) errOut = L"Echec de la requete.";
        return false;
    }
    if (status != 200) {
        wchar_t b[64]; wsprintfW(b, L"Hexium a repondu avec le code %lu.", (unsigned long)status);
        errOut = b;
        return false;
    }
    mj::Value root;
    if (!mj::parse(body, root) || root.type != mj::ARR) {
        errOut = L"Reponse Hexium illisible (format inattendu).";
        return false;
    }
    g_hexiumPackageCache = root;
    g_hexiumCacheValid = true;
    g_hexiumCacheTick = now;
    return true;
}

// Recupere (via le cache ci-dessus) l'entree du catalogue Hexium correspondant
// a l'URL d'un mod. Partagee par CheckHexiumVersion et FetchHexiumAutofill,
// comme FetchThunderstorePackage pour Thunderstore.
struct HexiumFetchResult {
    bool ok;
    bool isHexium;
    std::wstring error;
    mj::Value entry;
    HexiumFetchResult() : ok(false), isHexium(false) {}
};
static HexiumFetchResult FetchHexiumPackageEntry(const std::wstring& modUrl) {
    HexiumFetchResult r;
    std::wstring team, name;
    if (!ParseHexiumUrl(modUrl, team, name)) return r;   // isHexium reste false
    r.isHexium = true;

    std::wstring err;
    if (!FetchHexiumPackageList(err)) {
        r.error = err.empty() ? L"Echec de la requete." : err;
        return r;
    }
    for (size_t i = 0; i < g_hexiumPackageCache.arr.size(); ++i) {
        const mj::Value& e = g_hexiumPackageCache.arr[i];
        std::wstring eOwner = U2W(e.s("owner"));
        std::wstring eName  = U2W(e.s("name"));
        if (lstrcmpiW(eOwner.c_str(), team.c_str()) == 0 && lstrcmpiW(eName.c_str(), name.c_str()) == 0) {
            r.entry = e;
            r.ok = true;
            return r;
        }
    }
    r.error = L"Mod introuvable dans le catalogue Hexium (lien casse, mod retire, "
              L"ou equipe/nom incorrect dans l'URL).";
    return r;
}
// Comme FindLatestEntry pour Thunderstore, mais lit "versions[0]" (tableau,
// la plus recente en tete - confirme par le schema de l'API) plutot qu'un
// objet "latest" separe, forme propre a l'API Hexium.
static const mj::Value* FindHexiumLatestVersion(const mj::Value& entry, std::wstring& outVersion) {
    outVersion.clear();
    const mj::Value* versions = entry.find("versions");
    if (!versions || versions->type != mj::ARR || versions->arr.empty()) return NULL;
    const mj::Value& v0 = versions->arr[0];
    outVersion = U2W(v0.s("version_number"));
    if (outVersion.empty()) return NULL;
    return &v0;
}

struct HexiumCheckResult {
    bool ok;
    bool isHexium;
    bool deprecated;
    std::wstring latestVersion;
    std::wstring latestDate;
    std::wstring description;
    std::wstring downloadUrl;   // lien direct vers le zip de cette version
    std::wstring error;
    HexiumCheckResult() : ok(false), isHexium(false), deprecated(false) {}
};
static HexiumCheckResult CheckHexiumVersion(const std::wstring& modUrl) {
    HexiumCheckResult r;
    HexiumFetchResult f = FetchHexiumPackageEntry(modUrl);
    r.isHexium = f.isHexium;
    if (!f.isHexium) return r;
    if (!f.ok) { r.error = f.error; return r; }

    std::wstring latest;
    const mj::Value* v0 = FindHexiumLatestVersion(f.entry, latest);
    if (!v0) {
        r.error = L"Numero de version introuvable dans la reponse (mod retire ou reponse degradee).";
        return r;
    }
    r.latestVersion = latest;
    r.description  = Clean(U2W(v0->s("description")));
    r.latestDate   = Clean(U2W(v0->s("date_created")));
    r.downloadUrl  = U2W(v0->s("download_url"));

    const mj::Value* dep = f.entry.find("is_deprecated");
    r.deprecated = (dep && dep->type == mj::BOOL && dep->b);
    r.ok = true;
    return r;
}

// Recupere nom / categorie (owner) / derniere version / icone depuis Hexium
// pour pre-remplir l'editeur de mod (bouton "Auto-remplir"). Pas de
// changelogUrl : contrairement a Thunderstore, l'API Hexium n'expose pas de
// page de changelog separee dans ce schema (le champ reste simplement vide,
// comme pour Nexus - voir ModMissingAutofillableField).
struct HexiumAutofillResult {
    bool ok;
    bool isHexium;
    std::wstring error;
    std::wstring name, category, latestVersion, latestDate, localIconPath, description;
    HexiumAutofillResult() : ok(false), isHexium(false) {}
};
static HexiumAutofillResult FetchHexiumAutofill(const std::wstring& modUrl) {
    HexiumAutofillResult r;
    HexiumFetchResult f = FetchHexiumPackageEntry(modUrl);
    r.isHexium = f.isHexium;
    if (!f.isHexium) return r;
    if (!f.ok) { r.error = f.error; return r; }

    r.name = Clean(U2W(f.entry.s("name")));
    r.category = Clean(U2W(f.entry.s("owner")));
    if (r.category.empty()) {
        std::wstring team, nm;
        if (ParseHexiumUrl(modUrl, team, nm)) r.category = team;
    }

    std::wstring latest;
    const mj::Value* v0 = FindHexiumLatestVersion(f.entry, latest);
    if (!v0) {
        r.error = L"Numero de version introuvable dans la reponse (mod retire ou reponse degradee).";
        return r;
    }
    r.latestVersion = latest;
    r.latestDate = Clean(U2W(v0->s("date_created")));
    r.description = Clean(U2W(v0->s("description")));

    std::wstring iconUrl = U2W(v0->s("icon"));
    if (!iconUrl.empty()) {
        std::wstring host, path;
        if (SplitHttpsUrl(iconUrl, host, path)) {
            std::string bytes; DWORD status = 0; std::wstring err;
            if (HttpGetBytes(host, path, bytes, status, err) && status == 200 && !bytes.empty()) {
                std::wstring team, nm;
                ParseHexiumUrl(modUrl, team, nm);   // deja valide via f.isHexium
                std::wstring dir = ExeDir() + L"\\icons";
                MakeDirs(dir);
                // suffixe "-hexium" pour ne jamais ecraser une icone
                // Thunderstore/Nexus deja telechargee sous le meme
                // owner/nom (peu probable, mais gratuit a eviter).
                std::wstring dest = dir + L"\\" + SanitizeFileName(team) + L"-" + SanitizeFileName(nm) + L"-hexium.png";
                if (WriteAllBytes(dest, bytes)) r.localIconPath = StoreIconPath(dest);
            }
            // bonus non bloquant, comme pour Thunderstore : un echec de
            // telechargement d'icone ne fait pas echouer tout le reste.
        }
    }

    r.ok = true;
    return r;
}

// Telecharge le zip via le download_url renvoye directement par l'API Hexium
// pour cette version (pas de pattern d'URL a reconstruire a la main, comme
// pour Thunderstore - Hexium le fournit deja tout fait dans la reponse).
static bool DownloadHexiumZip(const std::wstring& downloadUrl, const std::wstring& destPath, std::wstring& errOut) {
    std::wstring host, path;
    if (!SplitHttpsUrl(downloadUrl, host, path)) {
        errOut = L"Lien de telechargement Hexium invalide ou manquant.";
        return false;
    }
    std::string bytes; DWORD status = 0;
    const size_t maxBytes = 200u * 1024u * 1024u;   // 200 Mo, garde-fou de securite
    if (!HttpGetBytes(host, path, bytes, status, errOut, maxBytes)) {
        if (errOut.empty()) errOut = L"Echec du telechargement.";
        return false;
    }
    if (status == 404) {
        errOut = L"Fichier introuvable sur Hexium (peut-etre retire entre temps).";
        return false;
    }
    if (status != 200) {
        wchar_t b[64]; wsprintfW(b, L"Hexium a repondu avec le code %lu.", (unsigned long)status);
        errOut = b;
        return false;
    }
    if (bytes.empty()) { errOut = L"Fichier telecharge vide."; return false; }
    if (!WriteAllBytes(destPath, bytes)) { errOut = L"Impossible d'ecrire le fichier sur le disque."; return false; }
    return true;
}

// ---------------------------------------------------------------- Nexus Mods
// Verification en ligne via l'API officielle de Nexus Mods :
//   GET https://api.nexusmods.com/v1/games/{domaine}/mods/{id}.json
//   en-tete requis : apikey: <cle personnelle>
// Contrairement a Thunderstore, l'API Nexus exige une cle API personnelle
// (gratuite - Compte Nexus > Parametres > API Keys), reglee dans l'appli via
// le menu Parametres > "Definir la cle API Nexus..." (voir g_nexusApiKey).
// Le domaine de jeu Nexus pour Valheim est "valheim" (voir NEXUS_GAME_DOMAIN
// ci-dessous, au cas ou Nexus le changerait un jour).
// Pas de telechargement direct : l'API "generate_download_link" de Nexus est
// reservee aux comptes Premium (ou a un flux NXM complique a reproduire
// fidelement ici) - le bouton DL reste desactive pour les mods Nexus (voir
// RefillMods), seule la verification de version est proposee.
static const wchar_t* NEXUS_GAME_DOMAIN = L"valheim";

// Extrait l'identifiant numerique d'une URL de page Nexus, ex:
// https://www.nexusmods.com/valheim/mods/1234 -> modId=1234
// https://www.nexusmods.com/valheim/mods/1234?tab=files -> idem
static bool ParseNexusUrl(const std::wstring& url, std::wstring& modId) {
    if (url.find(L"nexusmods.com") == std::wstring::npos) return false;
    size_t marker = url.find(L"/mods/");
    if (marker == std::wstring::npos) return false;
    std::wstring rest = url.substr(marker + 6);
    size_t end = 0;
    while (end < rest.size() && rest[end] >= L'0' && rest[end] <= L'9') ++end;
    if (end == 0) return false;
    modId = rest.substr(0, end);
    return true;
}

struct NexusFetchResult {
    bool ok;
    bool isNexus;
    std::wstring error;
    mj::Value root;
    NexusFetchResult() : ok(false), isNexus(false) {}
};
// Partagee par CheckNexusVersion et FetchNexusAutofill, comme
// FetchThunderstorePackage pour Thunderstore.
static NexusFetchResult FetchNexusMod(const std::wstring& modUrl) {
    NexusFetchResult r;
    std::wstring modId;
    if (!ParseNexusUrl(modUrl, modId)) return r;   // isNexus reste false
    r.isNexus = true;

    if (g_nexusApiKey.empty()) {
        r.error = L"Aucune cle API Nexus renseignee (menu Parametres > "
                  L"\"Definir la cle API Nexus...\"). Cle personnelle et gratuite, "
                  L"disponible sur ton compte Nexus (Parametres > API Keys).";
        return r;
    }

    std::wstring path = L"/v1/games/" + std::wstring(NEXUS_GAME_DOMAIN) + L"/mods/" + modId + L".json";
    std::wstring headers = L"apikey: " + g_nexusApiKey + L"\r\nAccept: application/json\r\n";
    std::string body; DWORD status = 0; std::wstring err;
    if (!HttpGetBytes(L"api.nexusmods.com", path, body, status, err, 0, headers)) {
        r.error = err.empty() ? L"Echec de la requete." : err;
        return r;
    }
    if (status == 401) {
        r.error = L"Cle API Nexus refusee (401) - verifie qu'elle est correcte "
                  L"dans Parametres > \"Definir la cle API Nexus...\".";
        return r;
    }
    if (status == 404) {
        r.error = L"Mod introuvable sur Nexus (lien casse, mod retire, ou "
                  L"identifiant incorrect dans l'URL).";
        return r;
    }
    if (status != 200) {
        wchar_t b[64]; wsprintfW(b, L"Nexus a repondu avec le code %lu.", (unsigned long)status);
        r.error = b;
        return r;
    }
    if (!mj::parse(body, r.root) || r.root.type != mj::OBJ) {
        r.error = L"Reponse Nexus illisible (format inattendu).";
        return r;
    }
    r.ok = true;
    return r;
}

struct NexusCheckResult {
    bool ok;
    bool isNexus;
    bool unavailable;   // mod retire/cache par son auteur ("available": false)
    std::wstring latestVersion;
    std::wstring latestDate;
    std::wstring description;
    std::wstring error;
    NexusCheckResult() : ok(false), isNexus(false), unavailable(false) {}
};
static NexusCheckResult CheckNexusVersion(const std::wstring& modUrl) {
    NexusCheckResult r;
    NexusFetchResult f = FetchNexusMod(modUrl);
    r.isNexus = f.isNexus;
    if (!f.isNexus) return r;
    if (!f.ok) { r.error = f.error; return r; }

    // Le champ "version" est la version declaree par l'auteur du mod (pas
    // un numero de fichier individuel) - equivalent le plus proche de
    // "latest.version_number" cote Thunderstore pour une simple comparaison.
    r.latestVersion = Clean(U2W(f.root.s("version")));
    if (r.latestVersion.empty()) {
        r.error = L"Numero de version introuvable dans la reponse Nexus.";
        return r;
    }
    r.latestDate = Clean(U2W(f.root.s("updated_time")));
    r.description = Clean(U2W(f.root.s("summary")));

    const mj::Value* avail = f.root.find("available");
    r.unavailable = (avail && avail->type == mj::BOOL && !avail->b);
    r.ok = true;
    return r;
}

// Recupere nom / description / derniere version / icone depuis Nexus pour
// pre-remplir l'editeur (bouton "Auto-remplir"). Meme principe que
// FetchThunderstoreAutofill : l'icone est telechargee dans icons\ a cote de
// l'exe et le chemin stocke est RELATIF (voir StoreIconPath).
struct NexusAutofillResult {
    bool ok;
    bool isNexus;
    std::wstring error;
    std::wstring name, latestVersion, latestDate, localIconPath, description;
    NexusAutofillResult() : ok(false), isNexus(false) {}
};
static NexusAutofillResult FetchNexusAutofill(const std::wstring& modUrl) {
    NexusAutofillResult r;
    NexusFetchResult f = FetchNexusMod(modUrl);
    r.isNexus = f.isNexus;
    if (!f.isNexus) return r;
    if (!f.ok) { r.error = f.error; return r; }

    r.name = Clean(U2W(f.root.s("name")));
    r.latestVersion = Clean(U2W(f.root.s("version")));
    if (r.latestVersion.empty()) {
        r.error = L"Numero de version introuvable dans la reponse Nexus.";
        return r;
    }
    r.latestDate = Clean(U2W(f.root.s("updated_time")));
    r.description = Clean(U2W(f.root.s("summary")));

    std::wstring iconUrl = U2W(f.root.s("picture_url"));
    if (!iconUrl.empty()) {
        std::wstring host, path;
        if (SplitHttpsUrl(iconUrl, host, path)) {
            std::string bytes; DWORD status = 0; std::wstring err;
            if (HttpGetBytes(host, path, bytes, status, err) && status == 200 && !bytes.empty()) {
                std::wstring modId; ParseNexusUrl(modUrl, modId);   // deja valide via f.isNexus
                std::wstring dir = ExeDir() + L"\\icons";
                MakeDirs(dir);
                // extension d'apres picture_url (jpg le plus souvent chez Nexus)
                std::wstring ext = L".jpg";
                size_t dot = path.find_last_of(L'.');
                if (dot != std::wstring::npos && path.size() - dot <= 5) ext = path.substr(dot);
                std::wstring dest = dir + L"\\nexus-" + modId + ext;
                if (WriteAllBytes(dest, bytes)) r.localIconPath = StoreIconPath(dest);
            }
            // l'icone est un bonus, comme pour Thunderstore : un echec ne
            // doit pas faire echouer tout l'auto-remplissage.
        }
    }

    r.ok = true;
    return r;
}

// Texte + categorie de couleur pour l'etat Thunderstore (integre a la ligne
// de details de chaque carte) : 0 gris (inconnu),
// 1 vert (a jour), 2 rouge (mise a jour disponible).
static std::wstring TsStatusText(const Mod& m, int* colorCategory) {
    if (colorCategory) *colorCategory = 0;
    if (m.tsVersion.empty()) return L"-";
    std::wstring installed = GetEffectiveInstalledVersion(m);
    if (installed.empty()) return m.tsVersion;   // derniere version connue, mais rien a comparer
    if (VersionLess(installed, m.tsVersion)) {
        if (colorCategory) *colorCategory = 2;
        return m.tsVersion + L" (maj disponible)";
    }
    if (colorCategory) *colorCategory = 1;
    return m.tsVersion + L" (a jour)";
}

// Sortie de la version 1.0 de Valheim (mise a jour "Deep North"), annoncee
// le 7 juin 2026 pour une sortie le 9 septembre 2026 - a verifier/ajuster
// si la date venait a changer. Format AAAA-MM-JJ pour rester directement
// comparable (lexicographiquement) aux horodatages ISO8601/"AAAA-MM-JJ ..."
// utilises partout ailleurs dans ce fichier.
static const wchar_t* VALHEIM_10_DATE = L"2026-09-09";

// Ne garde que la partie AAAA-MM-JJ d'un horodatage ISO8601
// ("2026-03-26T21:26:57Z") ou de notre propre format ("2026-03-26 14:30").
static std::wstring DatePrefix(const std::wstring& stamp) {
    return stamp.size() >= 10 ? stamp.substr(0, 10) : stamp;
}
// Categorie de couleur : 0 gris (inconnu - jamais verifie), 1 vert (mis a
// jour depuis la sortie de la 1.0), 2 gris neutre (pas encore mis a jour,
// mais la 1.0 n'est elle-meme pas encore sortie - rien d'anormal), 3 orange
// (la 1.0 est sortie et ce mod n'a pas ete mis a jour depuis - a surveiller).
static std::wstring Valheim10StatusText(const Mod& m, int* colorCategory) {
    if (colorCategory) *colorCategory = 0;
    if (m.tsLatestDate.empty()) return L"1.0 : inconnu";
    std::wstring modDate = DatePrefix(m.tsLatestDate);
    if (modDate >= VALHEIM_10_DATE) {
        if (colorCategory) *colorCategory = 1;
        return L"1.0 : a jour";
    }
    std::wstring today = DatePrefix(NowStamp());
    if (today < VALHEIM_10_DATE) {
        if (colorCategory) *colorCategory = 2;
        return L"1.0 : pas encore sortie";
    }
    if (colorCategory) *colorCategory = 3;
    return L"1.0 : a verifier";
}

// Une seule couleur "dominante" par carte plutot que par segment : plus
// simple et plus lisible qu'une phrase multicolore. Priorite : probleme
// concret (DLL manquant / mise a jour disponible) avant l'anciennete de
// la derniere verification.
static COLORREF RowStatusColor(const Mod& m) {
    bool dllMissing = false;
    DllStatusText(m, &dllMissing);
    if (dllMissing) return RGB(200, 40, 40);
    int tsCat = 0;
    TsStatusText(m, &tsCat);
    if (tsCat == 2) return RGB(200, 40, 40);
    int d = DaysSince(m.last);
    if (d < 0) return RGB(130, 130, 130);
    if (d >= 30) return RGB(200, 40, 40);
    if (d >= 14) return RGB(190, 120, 0);
    return RGB(30, 120, 50);
}
// Ligne de "petits details" affichee sous le nom du mod dans sa carte.
static std::wstring BuildDetailsLine(const Mod& m) {
    std::wstring s;
    if (!m.cat.empty()) s += m.cat + L"  |  ";
    s += L"Verif : " + (m.last.empty() ? std::wstring(L"jamais") : DaysText(m.last));
    s += L"  |  DLL : " + DllStatusText(m, NULL);
    const wchar_t* srcLabel = (m.apiSource == API_NEXUS) ? L"Nexus" :
        (m.apiSource == API_HEXIUM) ? L"Hexium" : (m.apiSource == API_NONE) ? L"-" : L"TS";
    s += L"  |  " + std::wstring(srcLabel) + L" : " + TsStatusText(m, NULL);
    if (g_showValheim10) s += L"  |  " + Valheim10StatusText(m, NULL);
    return s;
}

// definie plus bas (pres de MkButton/AddCol), mais RefillMods l'utilise des
// ici pour les info-bulles des boutons de chaque carte : declaration
// anticipee pour eviter d'imposer un ordre de sections dans le fichier.
static void AddTip(HWND ctrl, const wchar_t* text);

// ---------------------------------------------------------------- liste mods
// Vue en "cartes" (pas un tableau) : chaque mod est une ligne haute avec une
// icone visible, le nom, une ligne de petits details, une ligne de note, et
// ses propres boutons d'action - pas de toolbar partagee, pas de selection
// au sens ListView. Le panneau (g_hCardsHost, classe "ValModsCards") gere
// son propre defilement vertical (voir CardsHostProc plus bas).
static const int CARD_H = 128;

static void ClearCards() {
    for (size_t i = 0; i < g_cardChildren.size(); ++i)
        if (g_cardChildren[i].hwnd) DestroyWindow(g_cardChildren[i].hwnd);
    g_cardChildren.clear();
}
// STATIC de carte : SS_NOPREFIX est important ici (pas dans MkLabel, qui
// n'affiche que du texte fixe ecrit par nous) car le nom/la note d'un mod
// viennent de l'utilisateur ou de Thunderstore et peuvent contenir '&', que
// Windows interpreterait sinon comme un prefixe d'acceleration clavier.
static HWND MkCardStatic(const std::wstring& txt, int x, int y, int w, int h,
                         HFONT font, COLORREF color, DWORD extraStyle, bool stretch)
{
    // WS_CLIPSIBLINGS : indispensable avec autant de controles freres dans
    // un meme parent - sans lui, un frere peut se retrouver mal exclu de la
    // zone de rafraichissement d'un autre au moment de peindre.
    HWND s = CreateWindowExW(0, L"STATIC", txt.c_str(),
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_NOPREFIX | extraStyle,
        x, y, w, h, g_hCardsHost, NULL, g_hInst, NULL);
    SendMessageW(s, WM_SETFONT, (WPARAM)font, TRUE);
    if (color) SetWindowLongPtrW(s, GWLP_USERDATA, (LONG_PTR)color);
    CardChild cc; cc.hwnd = s; cc.x = x; cc.y = y; cc.w = w; cc.h = h; cc.stretch = stretch;
    g_cardChildren.push_back(cc);
    return s;
}
static HWND MkCardButton(int id, const wchar_t* txt, int x, int y, int w, int h) {
    HWND b = CreateWindowExW(0, L"BUTTON", txt,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_PUSHBUTTON,
        x, y, w, h, g_hCardsHost, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageW(b, WM_SETFONT, (WPARAM)g_font, TRUE);
    CardChild cc; cc.hwnd = b; cc.x = x; cc.y = y; cc.w = w; cc.h = h; cc.stretch = false;
    g_cardChildren.push_back(cc);
    return b;
}
// Recalcule la largeur des controles "stretch" selon la largeur actuelle du
// panneau, et repositionne tout selon g_scrollPos. Appelee au defilement ET
// au redimensionnement (bien moins couteux que reconstruire les cartes).
static void RepositionCards() {
    if (!g_hCardsHost) return;
    RECT rc; GetClientRect(g_hCardsHost, &rc);
    int hostW = rc.right;
    for (size_t i = 0; i < g_cardChildren.size(); ++i) {
        CardChild& cc = g_cardChildren[i];
        int w = cc.w;
        if (cc.stretch) {
            w = hostW - cc.x - 10;
            if (w < 40) w = 40;
        }
        MoveWindow(cc.hwnd, cc.x, cc.y - g_scrollPos, w, cc.h, TRUE);
    }
}
// InvalidateRect+UpdateWindow ne force le repaint QUE de g_hCardsHost
// lui-meme, jamais de ses enfants - or le contenu reel des cartes (icones,
// textes, boutons) est entierement porte par ces enfants, le panneau n'a
// aucun contenu propre a peindre. RDW_ALLCHILDREN est indispensable pour
// que la mise a jour se propage vraiment jusqu'a eux ; sans ca, seul un
// evenement systeme qui invalide tout de lui-meme (comme un redimensionnement,
// via CS_HREDRAW|CS_VREDRAW) declenche un affichage correct.
static void RedrawCardsHost() {
    if (!g_hCardsHost) return;
    RedrawWindow(g_hCardsHost, NULL, NULL,
        RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_ERASE);
}
// Meme constat, mais pour toute la fenetre : certains controles enfants
// directs de la fenetre principale (le menu de tri, les boutons Ajouter/
// Tri...) peuvent eux aussi ne pas se redessiner correctement apres
// une action, exactement pour la meme raison que le panneau de cartes.
// Plutot que de traquer un par un chaque endroit oublie, on force tout
// depuis la racine - legerement plus couteux, mais fiable.
static void RedrawEverything() {
    if (!g_hMain) return;
    RedrawWindow(g_hMain, NULL, NULL,
        RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_ERASE);
}

static void UpdateCardsScrollInfo() {
    if (!g_hCardsHost) return;
    RECT rc; GetClientRect(g_hCardsHost, &rc);
    int pageH = rc.bottom - rc.top;
    SCROLLINFO si; memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = (g_cardTotalHeight > 0) ? g_cardTotalHeight - 1 : 0;
    si.nPage = (UINT)((pageH > 1) ? pageH : 1);
    si.nPos = g_scrollPos;
    SetScrollInfo(g_hCardsHost, SB_VERT, &si, TRUE);
    // relit la position potentiellement replafonnee par Windows (page > range)
    GetScrollInfo(g_hCardsHost, SB_VERT, &si);
    g_scrollPos = si.nPos;
}
// Un mod appartient au modpack actuellement filtre (ou aucun filtre actif).
static bool ModMatchesPack(const Mod& m) {
    return g_modpackFilter.empty() || m.modpack == g_modpackFilter;
}
// Decoupe Mod::tags (texte libre separe par virgules ou points-virgules) en
// tags individuels, nettoyes (Trim) et non vides. Partage par ModHasTag et
// par la reconstruction du menu deroulant de filtre (RefillTagCombo).
static std::vector<std::wstring> SplitTags(const std::wstring& raw) {
    std::vector<std::wstring> out;
    std::wstring cur;
    for (size_t i = 0; i <= raw.size(); ++i) {
        if (i == raw.size() || raw[i] == L',' || raw[i] == L';') {
            std::wstring t = Trim(cur);
            if (!t.empty()) out.push_back(t);
            cur.clear();
        } else {
            cur += raw[i];
        }
    }
    return out;
}
// true si ce mod porte CE tag precis (comparaison insensible a la casse,
// comme le reste des comparaisons textuelles de l'appli - voir lstrcmpiW).
static bool ModHasTag(const Mod& m, const std::wstring& tag) {
    std::vector<std::wstring> ts = SplitTags(m.tags);
    for (size_t i = 0; i < ts.size(); ++i)
        if (lstrcmpiW(ts[i].c_str(), tag.c_str()) == 0) return true;
    return false;
}
// Un mod correspond au tag actuellement filtre (ou aucun filtre actif).
static bool ModMatchesTag(const Mod& m) {
    return g_tagFilter.empty() || ModHasTag(m, g_tagFilter);
}
// true si "needle" apparait dans "haystack", sans tenir compte de la casse.
// Conversion en minuscules manuelle (juste A-Z, largement suffisant ici -
// noms/descriptions de mods restent tres majoritairement en ASCII) plutot
// que de dependre de locales.
static std::wstring ToLowerAscii(const std::wstring& s) {
    std::wstring out = s;
    for (size_t i = 0; i < out.size(); ++i)
        if (out[i] >= L'A' && out[i] <= L'Z') out[i] = (wchar_t)(out[i] - L'A' + L'a');
    return out;
}
static bool ContainsCI(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) return true;
    return ToLowerAscii(haystack).find(ToLowerAscii(needle)) != std::wstring::npos;
}
// Un mod correspond a la recherche en cours (ou aucune recherche active) si
// le texte tape se retrouve dans l'un des champs COCHES (voir g_searchIn*,
// cases a cocher de la barre du haut). Si aucune case n'est cochee, on
// cherche quand meme dans tous les champs (voir le commentaire pres de
// g_searchInName) plutot que de bloquer silencieusement tout resultat.
static bool AnySearchFieldEnabled() {
    return g_searchInName || g_searchInCat || g_searchInDesc || g_searchInNote ||
           g_searchInTags || g_searchInModpack || g_searchInUrl;
}
static bool ModMatchesSearch(const Mod& m) {
    if (g_searchQuery.empty()) return true;
    bool restrict_ = AnySearchFieldEnabled();
    if ((!restrict_ || g_searchInName)    && ContainsCI(m.name, g_searchQuery))        return true;
    if ((!restrict_ || g_searchInCat)     && ContainsCI(m.cat, g_searchQuery))          return true;
    if ((!restrict_ || g_searchInDesc)    && ContainsCI(m.description, g_searchQuery))  return true;
    if ((!restrict_ || g_searchInNote)    && ContainsCI(m.note, g_searchQuery))         return true;
    if ((!restrict_ || g_searchInTags)    && ContainsCI(m.tags, g_searchQuery))         return true;
    if ((!restrict_ || g_searchInModpack) && ContainsCI(m.modpack, g_searchQuery))      return true;
    if ((!restrict_ || g_searchInUrl)     && ContainsCI(m.url, g_searchQuery))          return true;
    return false;
}
// Modpack + tag + recherche : les filtres que les operations groupees
// ("Tout verifier"/"Tout DL") doivent respecter, contrairement a "Masquer a
// jour" qui, lui, ne doit pas empecher "Tout verifier" de decouvrir un
// changement de statut (voir ModPassesFilters plus bas et le commentaire
// pres de g_hideUpToDate).
static bool ModMatchesFilters(const Mod& m) {
    return ModMatchesPack(m) && ModMatchesTag(m) && ModMatchesSearch(m);
}
// Filtre complet utilise pour l'affichage des cartes ET pour "Tout DL" :
// modpack + tag + masquage des mods a jour. PAS utilise par "Tout verifier",
// qui doit justement pouvoir decouvrir un changement de statut (voir le
// commentaire pres de g_hideUpToDate).
static bool ModPassesFilters(const Mod& m) {
    if (!ModMatchesFilters(m)) return false;
    if (g_hideUpToDate) {
        int tsCat = 0;
        TsStatusText(m, &tsCat);
        if (tsCat == 1) return false;   // "a jour" masque si le toggle est actif
    }
    return true;
}

// Fait defiler le panneau pour amener ce mod (retrouve par son identifiant
// stable, voir Mod::uid - PAS par son nom, qui peut etre duplique si le meme
// mod a ete ajoute a plusieurs modpacks comme deux fiches separees) apres
// tri ET filtrage, dans la zone visible. Sans ca, ajouter/modifier un mod
// dont le nom le fait trier loin de la position actuelle de defilement
// donne l'impression que "rien ne s'est passe" - la carte existe bien,
// mais hors champ, sans aucune indication a l'ecran.
static void ScrollToMod(int uid) {
    if (!g_hCardsHost || uid <= 0) return;
    int gIdx = -1;
    for (size_t k = 0; k < g_mods.size(); ++k)
        if (g_mods[k].uid == uid) { gIdx = (int)k; break; }
    if (gIdx < 0) return;
    // position parmi les cartes VISIBLES, pas l'index brut dans g_mods : un
    // filtre actif peut avoir saute des mods, decalant les positions ecran.
    int visRow = -1;
    for (size_t v = 0; v < g_visibleIndices.size(); ++v)
        if (g_visibleIndices[v] == gIdx) { visRow = (int)v; break; }
    if (visRow < 0) return;   // le mod est filtre, pas de carte a montrer

    int cardTop = visRow * CARD_H;
    int cardBottom = cardTop + CARD_H;
    RECT rc; GetClientRect(g_hCardsHost, &rc);
    int pageH = rc.bottom - rc.top;
    if (cardTop < g_scrollPos) g_scrollPos = cardTop;
    else if (cardBottom > g_scrollPos + pageH) g_scrollPos = cardBottom - pageH;
    if (g_scrollPos < 0) g_scrollPos = 0;
    UpdateCardsScrollInfo();
    RepositionCards();
    RedrawCardsHost();
}

// Reconstruit le menu deroulant de filtre "Modpack" a partir des noms de
// modpack distincts presents dans g_mods, en tete "Tous les mods". Conserve
// la selection courante si ce modpack existe encore, sinon repli sur "tous".
static void RefillModpackCombo() {
    HWND combo = g_hMain ? GetDlgItem(g_hMain, IDC_MODPACK) : NULL;
    if (!combo) return;
    std::vector<std::wstring> packs;
    for (size_t i = 0; i < g_mods.size(); ++i) {
        if (g_mods[i].modpack.empty()) continue;
        bool found = false;
        for (size_t k = 0; k < packs.size(); ++k)
            if (packs[k] == g_mods[i].modpack) { found = true; break; }
        if (!found) packs.push_back(g_mods[i].modpack);
    }
    std::sort(packs.begin(), packs.end());

    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Tous les mods");
    for (size_t i = 0; i < packs.size(); ++i)
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)packs[i].c_str());

    int sel = 0;
    if (!g_modpackFilter.empty()) {
        for (size_t i = 0; i < packs.size(); ++i)
            if (packs[i] == g_modpackFilter) { sel = (int)(i + 1); break; }
        if (sel == 0) g_modpackFilter.clear();   // le modpack filtre a disparu
    }
    SendMessageW(combo, CB_SETCURSEL, sel, 0);
}

// Reconstruit le menu deroulant de filtre "Tag" a partir de tous les tags
// distincts presents dans g_mods (chaque Mod::tags decoupe via SplitTags),
// en tete "Tous les tags". Meme logique que RefillModpackCombo.
static void RefillTagCombo() {
    HWND combo = g_hMain ? GetDlgItem(g_hMain, IDC_TAGFILTER) : NULL;
    if (!combo) return;
    std::vector<std::wstring> tags;
    for (size_t i = 0; i < g_mods.size(); ++i) {
        std::vector<std::wstring> ts = SplitTags(g_mods[i].tags);
        for (size_t j = 0; j < ts.size(); ++j) {
            bool found = false;
            for (size_t k = 0; k < tags.size(); ++k)
                if (lstrcmpiW(tags[k].c_str(), ts[j].c_str()) == 0) { found = true; break; }
            if (!found) tags.push_back(ts[j]);
        }
    }
    std::sort(tags.begin(), tags.end());

    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Tous les tags");
    for (size_t i = 0; i < tags.size(); ++i)
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)tags[i].c_str());

    int sel = 0;
    if (!g_tagFilter.empty()) {
        for (size_t i = 0; i < tags.size(); ++i)
            if (lstrcmpiW(tags[i].c_str(), g_tagFilter.c_str()) == 0) { sel = (int)(i + 1); break; }
        if (sel == 0) g_tagFilter.clear();   // le tag filtre a disparu
    }
    SendMessageW(combo, CB_SETCURSEL, sel, 0);
}

static void RefillMods() {
    std::stable_sort(g_mods.begin(), g_mods.end(), ModLess);
    RefillModpackCombo();   // peut reinitialiser g_modpackFilter si obsolete
    RefillTagCombo();       // peut reinitialiser g_tagFilter si obsolete
    ClearCards();
    g_visibleIndices.clear();

    // iconX/textX : l'icone (CARD_ICON_SIZE) est centree verticalement dans
    // la carte, le texte commence juste apres avec une marge confortable.
    const int iconX = 10, textX = iconX + CARD_ICON_SIZE + 16;   // 90 a 64px
    int row = 0;   // position parmi les cartes VISIBLES (pas l'index dans g_mods)
    for (size_t i = 0; i < g_mods.size(); ++i) {
        const Mod& m = g_mods[i];
        if (!ModPassesFilters(m)) continue;
        g_visibleIndices.push_back((int)i);
        int y = row * CARD_H;
        int iconY = y + (CARD_H - CARD_ICON_SIZE) / 2;

        HWND icon = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_ICON | SS_CENTERIMAGE,
            iconX, iconY, CARD_ICON_SIZE, CARD_ICON_SIZE, g_hCardsHost, NULL, g_hInst, NULL);
        SendMessageW(icon, STM_SETICON, (WPARAM)GetOrLoadHIcon(m.iconPath), 0);
        { CardChild cc; cc.hwnd = icon; cc.x = iconX; cc.y = iconY; cc.w = CARD_ICON_SIZE; cc.h = CARD_ICON_SIZE;
          cc.stretch = false; g_cardChildren.push_back(cc); }

        MkCardStatic(m.name, textX, y + 8, 400, 20,
            g_fontBold, 0, SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, true);

        // description courte (auto-remplie depuis Thunderstore, ou tapee a
        // la main dans l'editeur) : ce que fait le mod, en un coup d'oeil.
        MkCardStatic(m.description, textX, y + 28, 400, 18,
            g_font, RGB(40, 40, 40), SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, true);

        MkCardStatic(BuildDetailsLine(m), textX, y + 46, 400, 18,
            g_font, RowStatusColor(m), SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, true);

        std::wstring noteTxt = m.note.empty() ? L"" : (L"Note : " + m.note);
        MkCardStatic(noteTxt, textX, y + 64, 400, 18,
            g_font, RGB(120, 120, 120), SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, true);

        // Boutons a glyphe plutot qu'a texte : des caracteres Unicode
        // (echappes en \u pour ne pas dependre de l'encodage du fichier
        // source), pas de vraie image - on evite ainsi tout le pipeline de
        // chargement d'icone (et ses pieges) pour un simple bouton. Le sens
        // de chaque glyphe est repris dans l'info-bulle au survol.
        int bx = textX, by = y + 86, bh = 24, bw = 32, gap = 4;
        int id = RA_BASE + row * RA_COUNT;
        HWND bWatch = MkCardButton(id + RA_WATCH, L"\u2197",    bx, by, bw, bh); bx += bw + gap;  // ->  ouvrir
        HWND bHist  = MkCardButton(id + RA_HIST,  L"\u2261",    bx, by, bw, bh); bx += bw + gap;  // =   historique
        HWND bCheck = MkCardButton(id + RA_CHECK, L"\u21BB",    bx, by, bw, bh); bx += bw + gap;  // (r) check+ouvrir
        HWND bMark  = MkCardButton(id + RA_MARK,  L"\u2713",    bx, by, bw, bh); bx += bw + gap;  // v   verifie
        HWND bTs    = MkCardButton(id + RA_TS,    L"\u26A1",    bx, by, bw, bh); bx += bw + gap;  // eclair  Thunderstore
        HWND bDl    = MkCardButton(id + RA_DL,    L"\u2193",    bx, by, bw, bh); bx += bw + gap;  // v(bas)  telecharger
        HWND bEdit  = MkCardButton(id + RA_EDIT,  L"\u270E",    bx, by, bw, bh); bx += bw + gap;  // crayon  modifier
        HWND bConfig= MkCardButton(id + RA_CONFIG,L"\u2699",    bx, by, bw, bh); bx += bw + gap;  // roue dentee  config
        HWND bMore  = MkCardButton(id + RA_MORE,  L"...",       bx, by, bw, bh); bx += bw + gap;  // ...  plus d'actions

        AddTip(bWatch, L"Watch : ouvre la page du mod dans le navigateur");
        AddTip(bHist,  L"Historique : ouvre la page des changements / versions du mod");
        AddTip(bCheck, L"Check+ : ouvre la page du mod ET note la date de verification du jour");
        AddTip(bMark,  L"Verifie : note la date de verification SANS ouvrir de lien - deja verifie ailleurs ?");
        if (m.apiSource == API_NONE) {
            EnableWindow(bTs, FALSE);
            EnableWindow(bDl, FALSE);
            AddTip(bTs, L"Verification : desactivee - source \"Aucune\" (voir Modifier)");
            AddTip(bDl, L"Telecharger : desactive - source \"Aucune\" (voir Modifier)");
        } else if (m.apiSource == API_NEXUS) {
            EnableWindow(bDl, FALSE);   // pas de telechargement direct sans compte Nexus Premium
            AddTip(bTs, L"Nexus : interroge l'API Nexus Mods pour connaitre la derniere version publiee");
            AddTip(bDl, L"Telecharger : non disponible pour Nexus (API de telechargement reservee "
                        L"aux comptes Premium) - utilise Watch pour aller chercher le fichier a la main");
        } else if (m.apiSource == API_HEXIUM) {
            AddTip(bTs, L"Hexium : interroge Hexium pour connaitre la derniere version publiee");
            AddTip(bDl, L"Telecharger : recupere le zip de la derniere version depuis Hexium");
        } else {
            AddTip(bTs, L"Thunderstore : interroge Thunderstore pour connaitre la derniere version publiee");
            AddTip(bDl, L"Telecharger : recupere le zip de la derniere version (demande ou l'enregistrer)");
        }
        AddTip(bEdit,  L"Modifier ce mod");
        // Config : ouvre le fichier avec le programme associe par Windows
        // (Bloc-notes pour un .cfg par defaut, ou tout autre editeur associe
        // par l'utilisateur) - desactive si aucun fichier de config n'est
        // renseigne pour ce mod (voir Modifier).
        if (m.configPath.empty()) {
            EnableWindow(bConfig, FALSE);
            AddTip(bConfig, L"Config : aucun fichier associe - a definir dans Modifier");
        } else {
            AddTip(bConfig, L"Config : ouvre le fichier de config avec le programme associe par Windows");
        }
        AddTip(bMore,  L"Plus d'actions : copier le lien, localiser le DLL/la config, supprimer...");

        MkCardStatic(L"", 8, y + CARD_H - 6, 400, 2, g_font, 0, SS_ETCHEDHORZ, true);
        ++row;
    }

    g_cardTotalHeight = row * CARD_H;
    UpdateCardsScrollInfo();
    RepositionCards();
    RedrawEverything();
}

// ---------------------------------------------------------------- editeur mod
struct EditCtx {
    Mod m;
    bool ok;
    HWND hName, hCat, hUrl, hHist, hDesc, hDll, hConfig, hIcon, hNote, hIconPreview, hApiSource, hModpack, hInstalledVer, hTags;
    HICON previewIcon;
    EditCtx() : ok(false), hName(0), hCat(0), hUrl(0), hHist(0), hDesc(0), hDll(0), hConfig(0), hIcon(0),
                hNote(0), hIconPreview(0), hApiSource(0), hModpack(0), hInstalledVer(0), hTags(0), previewIcon(0) {}
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
                               const std::wstring& current, const wchar_t* title,
                               const std::wstring& fallbackDir = L"")
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
        std::wstring pd = fallbackDir.empty() ? PluginsDir() : fallbackDir;
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
// Boite "Enregistrer sous..." (contrairement a BrowseFile, annuler renvoie
// une chaine vide plutot qu'une valeur "actuelle" - il n'y en a pas ici).
static std::wstring BrowseSaveFile(HWND owner, const wchar_t* filter,
                                   const std::wstring& initialDir,
                                   const std::wstring& suggestedName, const wchar_t* title)
{
    wchar_t buf[MAX_PATH]; buf[0] = 0;
    if (!suggestedName.empty()) {
        wcsncpy(buf, suggestedName.c_str(), MAX_PATH - 1);
        buf[MAX_PATH - 1] = 0;
    }
    OPENFILENAMEW ofn; memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.lpstrInitialDir = (!initialDir.empty() && DirExists(initialDir)) ? initialDir.c_str() : NULL;
    ofn.lpstrDefExt = L"zip";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetSaveFileNameW(&ofn)) return std::wstring(buf);
    return L"";
}
static int CALLBACK BrowseFolderCB(HWND hwnd, UINT msg, LPARAM lp, LPARAM data) {
    if (msg == BFFM_INITIALIZED && data)
        SendMessageW(hwnd, BFFM_SETSELECTION, TRUE, data);
    return 0;
}
// Boite systeme "Parcourir les dossiers..." ; renvoie une chaine vide si
// l'utilisateur annule.
static std::wstring BrowseFolder(HWND owner, const wchar_t* title, const std::wstring& initial) {
    BROWSEINFOW bi; memset(&bi, 0, sizeof(bi));
    wchar_t path[MAX_PATH] = L"";
    bi.hwndOwner = owner;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = BrowseFolderCB;
    bi.lParam = initial.empty() ? 0 : (LPARAM)initial.c_str();
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return L"";
    std::wstring result;
    if (SHGetPathFromIDListW(pidl, path)) result = path;
    CoTaskMemFree(pidl);
    return result;
}
// Renvoie true si un chemin non vide a effectivement produit une icone
// (permet a l'appelant de signaler un echec de chargement au lieu de
// laisser l'aperçu redevenir silencieusement vide sans explication).
static bool UpdateIconPreview(EditCtx* c, const std::wstring& path) {
    if (!c || !c->hIconPreview) return false;
    HICON hi = path.empty() ? NULL : LoadScaledIconFromFile(ResolveIconPath(path), 32);
    SendMessageW(c->hIconPreview, STM_SETICON, (WPARAM)hi, 0);
    if (c->previewIcon) DestroyIcon(c->previewIcon);
    c->previewIcon = hi;
    return !path.empty() && hi != NULL;
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
        MkDlgButton(hwnd, IDC_E_AUTOFILL, L"Auto-remplir (Thunderstore/Nexus)", LX, 150, 220, 24);
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
        // Champ texte reellement porteur du chemin (c->m.iconPath en depend
        // a la sauvegarde) - BUG CORRIGE : ce controle avait disparu d'une
        // reecriture precedente, laissant c->hIcon a NULL en permanence.
        // SetWindowTextW/GetTextOf sur un HWND NULL echouent silencieusement,
        // donc iconPath restait toujours vide malgre un apercu fonctionnel
        // (l'apercu, lui, utilise directement le chemin choisi, jamais ce
        // controle - c'est pour ca qu'il marchait sans que le bug se voie).
        MkLabel(hwnd, L"Chemin", IX, 130, 130);
        c->hIcon = MkEdit(hwnd, c->m.iconPath, IX, 148, 130);

        // Quelle API utiliser pour la verification/l'auto-remplissage -
        // remplace l'ancienne case "Non Thunderstore". "Aucune" a le meme
        // effet qu'avant (masque TS/DL sur la carte, exclut du "Tout
        // verifier") ; "Nexus" active la verification via l'API Nexus Mods
        // (necessite une cle API, voir menu Parametres).
        MkLabel(hwnd, L"Source de verification", IX, 182, 130);
        c->hApiSource = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            IX, 200, 130, 200, hwnd, (HMENU)IDC_E_APISOURCE, g_hInst, NULL);
        SendMessageW(c->hApiSource, WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(c->hApiSource, CB_ADDSTRING, 0, (LPARAM)L"Thunderstore");
        SendMessageW(c->hApiSource, CB_ADDSTRING, 0, (LPARAM)L"Nexus Mods");
        SendMessageW(c->hApiSource, CB_ADDSTRING, 0, (LPARAM)L"Hexium");
        SendMessageW(c->hApiSource, CB_ADDSTRING, 0, (LPARAM)L"Aucune (desactive)");
        SendMessageW(c->hApiSource, CB_SETCURSEL, (WPARAM)c->m.apiSource, 0);

        // Regroupe des mods pour un meme playthrough/serveur. CBS_DROPDOWN
        // (pas DROPDOWNLIST) : on peut choisir un modpack existant dans la
        // liste OU taper librement un nouveau nom.
        MkLabel(hwnd, L"Modpack (optionnel - regroupe des mods pour un meme playthrough)",
            LX, 232, LW + 16 + 130);
        c->hModpack = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN | WS_VSCROLL | CBS_AUTOHSCROLL,
            LX, 250, LW + 16 + 130, 200, hwnd, (HMENU)IDC_E_MODPACK, g_hInst, NULL);
        SendMessageW(c->hModpack, WM_SETFONT, (WPARAM)g_font, TRUE);
        {
            // modpacks distincts deja utilises ailleurs, pour les proposer
            // dans la liste deroulante sans empecher d'en taper un nouveau.
            std::vector<std::wstring> packs;
            for (size_t pi = 0; pi < g_mods.size(); ++pi) {
                if (g_mods[pi].modpack.empty()) continue;
                bool found = false;
                for (size_t pk = 0; pk < packs.size(); ++pk)
                    if (packs[pk] == g_mods[pi].modpack) { found = true; break; }
                if (!found) packs.push_back(g_mods[pi].modpack);
            }
            std::sort(packs.begin(), packs.end());
            for (size_t pi = 0; pi < packs.size(); ++pi)
                SendMessageW(c->hModpack, CB_ADDSTRING, 0, (LPARAM)packs[pi].c_str());
        }
        SetWindowTextW(c->hModpack, c->m.modpack.c_str());

        // -- description (courte, affichee sur la carte) - pleine largeur ---
        MkLabel(hwnd, L"Description courte (affichee sur la carte)", LX, 282, LW + 16 + 130);
        c->hDesc = MkEdit(hwnd, c->m.description, LX, 300, LW + 16 + 130);

        // -- dossier du mod installe, pleine largeur -------------------------
        // On demande le DOSSIER dans lequel le mod est installe (typiquement
        // BepInEx\plugins\NomDuMod\) plutot que le chemin exact d'un .dll :
        // plus simple a retrouver dans l'explorateur, et resistant aux mods
        // qui embarquent plusieurs DLL ou renomment le leur d'une version a
        // l'autre. Le DLL a l'interieur est retrouve automatiquement (voir
        // ResolveModDll) pour la verification de presence/version.
        MkLabel(hwnd, L"Dossier du mod installe (pour verifier qu'il est bien present)", LX, 332, LW + 16 + 130);
        c->hDll = MkEdit(hwnd, c->m.modDir, LX, 350, LW);
        MkDlgButton(hwnd, IDC_E_BROWSEDLL, L"Parcourir...", LX + LW + 24, 350, 124, 24);

        // -- fichier de config associe (optionnel), pleine largeur -----------
        MkLabel(hwnd, L"Fichier de config (optionnel)", LX, 382, LW + 16 + 130);
        c->hConfig = MkEdit(hwnd, c->m.configPath, LX, 400, LW);
        MkDlgButton(hwnd, IDC_E_BROWSECONFIG, L"Parcourir...", LX + LW + 24, 400, 124, 24);

        // Version qu'on SUIT comme installee - mise a jour automatiquement
        // par Telecharger/Tout DL (voir ActionDownloadLatest/ActionDownloadAll),
        // ou modifiable ici a la main si l'installation se fait autrement
        // (ou si le zip telecharge n'a pas encore ete extrait).
        MkLabel(hwnd, L"Version installee (suivie par Telecharger, ou a la main)", LX, 432, 260);
        c->hInstalledVer = MkEdit(hwnd, c->m.installedVersion, LX, 450, 200);

        // Tags libres (separes par des virgules) - filtre rapide independant
        // du modpack (voir menu deroulant "Tag" de la barre du haut).
        MkLabel(hwnd, L"Tags (separes par des virgules)", LX + 220, 432, LW + 16 + 130 - 220);
        c->hTags = MkEdit(hwnd, c->m.tags, LX + 220, 450, LW + 16 + 130 - 220);

        MkLabel(hwnd, L"Note (remarques diverses)", LX, 482, LW + 16 + 130);
        c->hNote = MkEdit(hwnd, c->m.note, LX, 500, LW + 16 + 130);

        HWND ok = CreateWindowExW(0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            336, 538, 92, 28, hwnd, (HMENU)IDOK, g_hInst, NULL);
        HWND ca = CreateWindowExW(0, L"BUTTON", L"Annuler",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            436, 538, 92, 28, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
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
            // Detecte automatiquement Thunderstore, Nexus ou Hexium d'apres
            // l'URL - inutile de faire dependre ca de la selection dans le
            // menu deroulant "Source de verification" (qui, elle, sert
            // surtout a desactiver completement TS/DL si besoin).
            bool looksNexus  = url.find(L"nexusmods.com") != std::wstring::npos;
            bool looksHexium = !looksNexus && url.find(L"hexium.gg") != std::wstring::npos;

            std::wstring name, category, changelogUrl, description, latestVersion, latestDate, localIconPath;
            bool matched = false, ok = false; std::wstring error;

            if (looksNexus) {
                HCURSOR oldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
                NexusAutofillResult r = FetchNexusAutofill(url);
                SetCursor(oldCursor);
                matched = r.isNexus; ok = r.ok; error = r.error;
                name = r.name; description = r.description;
                latestVersion = r.latestVersion; latestDate = r.latestDate;
                localIconPath = r.localIconPath;
            } else if (looksHexium) {
                HCURSOR oldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
                HexiumAutofillResult r = FetchHexiumAutofill(url);
                SetCursor(oldCursor);
                matched = r.isHexium; ok = r.ok; error = r.error;
                name = r.name; category = r.category; description = r.description;
                latestVersion = r.latestVersion; latestDate = r.latestDate;
                localIconPath = r.localIconPath;
            } else {
                HCURSOR oldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
                TsAutofillResult r = FetchThunderstoreAutofill(url);
                SetCursor(oldCursor);
                matched = r.isThunderstore; ok = r.ok; error = r.error;
                name = r.name; category = r.category; changelogUrl = r.changelogUrl; description = r.description;
                latestVersion = r.latestVersion; latestDate = r.latestDate;
                localIconPath = r.localIconPath;
                if (matched && ok) SendMessageW(c->hApiSource, CB_SETCURSEL, API_THUNDERSTORE, 0);
            }
            if (looksNexus  && matched && ok) SendMessageW(c->hApiSource, CB_SETCURSEL, API_NEXUS, 0);
            if (looksHexium && matched && ok) SendMessageW(c->hApiSource, CB_SETCURSEL, API_HEXIUM, 0);

            if (!matched) {
                MessageBoxW(hwnd,
                    L"Ce lien ne pointe ni vers thunderstore.io, ni vers nexusmods.com,\n"
                    L"ni vers hexium.gg : l'auto-remplissage ne fonctionne que pour les\n"
                    L"mods heberges sur l'un de ces sites.",
                    L"ValMods", MB_OK | MB_ICONWARNING);
                return 0;
            }
            if (!ok) {
                std::wstring m = L"Auto-remplissage impossible :\n" + error;
                MessageBoxW(hwnd, m.c_str(), L"ValMods", MB_OK | MB_ICONWARNING);
                return 0;
            }

            // remplace toujours les champs concernes : un clic explicite sur
            // "Auto-remplir" veut dire "je veux les donnees fraiches".
            if (!name.empty())         SetWindowTextW(c->hName, name.c_str());
            if (!category.empty())    SetWindowTextW(c->hCat, category.c_str());
            if (!changelogUrl.empty()) SetWindowTextW(c->hHist, changelogUrl.c_str());
            if (!description.empty())  SetWindowTextW(c->hDesc, description.c_str());
            bool iconLoadedOk = true;
            if (!localIconPath.empty()) {
                SetWindowTextW(c->hIcon, localIconPath.c_str());
                iconLoadedOk = UpdateIconPreview(c, localIconPath);
            }
            c->m.tsVersion = latestVersion;   // porte jusqu'a l'enregistrement (pas de champ texte dedie)
            c->m.tsLatestDate = latestDate;

            std::wstring sourceLabel = looksNexus ? L"Nexus" : (looksHexium ? L"Hexium" : L"Thunderstore");
            std::wstring summary = std::wstring(L"Champs remplis depuis ") + sourceLabel +
                L".\nDerniere version publiee : " + latestVersion;
            if (localIconPath.empty())
                summary += L"\n(icone non recuperee - le mod n'en a peut-etre pas)";
            else if (!iconLoadedOk)
                summary += L"\n(icone telechargee mais illisible - fichier corrompu ou\n"
                          L"format non gere par GDI+ ; le chemin est garde dans le champ\n"
                          L"Icone au cas ou, mais l'apercu restera vide)";
            Info(hwnd, summary.c_str());
            return 0;
        }
        if (LOWORD(wp) == IDC_E_BROWSEDLL && c) {
            // On demande le DOSSIER du mod, pas un fichier .dll precis (voir
            // Mod::modDir) - part du dossier deja renseigne si non vide et
            // valide, sinon de BepInEx\plugins.
            std::wstring cur = GetTextOf(c->hDll);
            std::wstring initial = (!cur.empty() && DirExists(cur)) ? cur : PluginsDir();
            std::wstring picked = BrowseFolder(hwnd, L"Choisir le dossier du mod installe", initial);
            if (!picked.empty()) SetWindowTextW(c->hDll, picked.c_str());
            return 0;
        }
        if (LOWORD(wp) == IDC_E_BROWSECONFIG && c) {
            std::wstring picked = BrowseFile(hwnd,
                L"Config (*.cfg;*.json;*.yaml;*.yml;*.txt)\0*.cfg;*.json;*.yaml;*.yml;*.txt\0"
                L"Tous les fichiers (*.*)\0*.*\0",
                GetTextOf(c->hConfig), L"Choisir le fichier de config du mod", ConfigDir());
            SetWindowTextW(c->hConfig, picked.c_str());
            return 0;
        }
        if (LOWORD(wp) == IDC_E_BROWSEICON && c) {
            std::wstring picked = BrowseFile(hwnd,
                L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.ico;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.ico;*.gif\0"
                L"Tous les fichiers (*.*)\0*.*\0",
                GetTextOf(c->hIcon), L"Choisir une icone");
            std::wstring stored = picked;
            if (!picked.empty()) {
                // Copie le fichier choisi dans icons\ a cote de l'exe (sauf
                // s'il s'y trouve deja) pour pouvoir stocker un chemin
                // RELATIF (voir StoreIconPath) - indispensable pour que
                // valmods.json + valmods.exe + icons\ restent portables si on
                // les envoie a un ami. En cas d'echec de la copie (disque
                // plein, permissions...), on garde simplement le chemin
                // choisi tel quel (fonctionnera toujours en local).
                std::wstring copied = CopyIconIntoIconsDir(ResolveIconPath(picked));
                stored = copied.empty() ? picked : StoreIconPath(copied);
            }
            SetWindowTextW(c->hIcon, stored.c_str());
            if (!stored.empty() && !UpdateIconPreview(c, stored)) {
                MessageBoxW(hwnd,
                    L"Ce fichier n'a pas pu etre charge comme icone.\n\n"
                    L"Causes possibles :\n"
                    L"- format non gere par GDI+ (WEBP et AVIF ne sont PAS geres ;\n"
                    L"  utilise plutot un PNG ou JPG)\n"
                    L"- fichier corrompu ou incomplet\n\n"
                    L"Le champ garde quand meme le chemin choisi, au cas ou le\n"
                    L"probleme vienne d'ailleurs - mais l'apercu restera vide.",
                    L"ValMods", MB_OK | MB_ICONWARNING);
            }
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
            c->m.description  = Clean(GetTextOf(c->hDesc));
            c->m.modDir       = Clean(GetTextOf(c->hDll));
            c->m.configPath   = Clean(GetTextOf(c->hConfig));
            c->m.installedVersion = Clean(GetTextOf(c->hInstalledVer));
            c->m.tags = Clean(GetTextOf(c->hTags));
            // StoreIconPath en filet de securite : si le champ a ete tape a
            // la main avec un chemin absolu qui se trouve deja sous le
            // dossier de l'exe, on le relativise quand meme.
            c->m.iconPath     = StoreIconPath(Clean(GetTextOf(c->hIcon)));
            c->m.note = Clean(GetTextOf(c->hNote));
            {
                int sel = (int)SendMessageW(c->hApiSource, CB_GETCURSEL, 0, 0);
                c->m.apiSource = (sel < 0) ? API_THUNDERSTORE : sel;
            }
            c->m.modpack = Clean(GetTextOf(c->hModpack));
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
    RECT r = { 0, 0, 540, 580 };
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

// ---------------------------------------------------------------- saisie generique
// Petite boite modale generique "un champ texte + OK/Annuler" - utilisee pour
// la cle API Nexus (menu Parametres), sur le meme principe que ShowModEditor
// mais bien plus simple (un seul controle).
struct TextInputCtx {
    std::wstring prompt, value;
    bool ok;
    HWND hEdit;
    TextInputCtx() : ok(false), hEdit(0) {}
};
static LRESULT CALLBACK TextInputProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    TextInputCtx* c = (TextInputCtx*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        c = (TextInputCtx*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)c);
        MkLabel(hwnd, c->prompt.c_str(), 12, 12, 380);
        c->hEdit = MkEdit(hwnd, c->value, 12, 34, 380);
        HWND ok = CreateWindowExW(0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            216, 68, 84, 26, hwnd, (HMENU)IDOK, g_hInst, NULL);
        HWND ca = CreateWindowExW(0, L"BUTTON", L"Annuler",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            308, 68, 84, 26, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
        SendMessageW(ok, WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(ca, WM_SETFONT, (WPARAM)g_font, TRUE);
        SetFocus(c->hEdit);
        SendMessageW(c->hEdit, EM_SETSEL, 0, -1);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK && c) {
            c->value = GetTextOf(c->hEdit);
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
// Renvoie true si l'utilisateur a valide (io est alors mis a jour) ; false
// si annule (io reste inchange). Le texte n'est PAS force en majuscules/
// nettoye (Clean()) par l'appelant, contrairement aux autres champs de
// l'appli - une cle API peut contenir des caracteres que Clean() laisse de
// toute facon intacts (ni tabulation ni retour a la ligne attendus ici).
static bool ShowTextInputDialog(HWND parent, const wchar_t* title, const wchar_t* prompt, std::wstring& io) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc; memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = TextInputProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"ValModsTextInput";
        RegisterClassExW(&wc);
        reg = true;
    }
    TextInputCtx ctx; ctx.prompt = prompt; ctx.value = io;
    RECT r = { 0, 0, 410, 110 };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&r, style, FALSE);
    RECT pr; GetWindowRect(parent, &pr);
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = pr.left + ((pr.right - pr.left) - w) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - h) / 2;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        L"ValModsTextInput", title, style, x, y, w, h, parent, NULL, g_hInst, &ctx);
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
    if (ctx.ok) io = ctx.value;
    return ctx.ok;
}
// Ouvre la boite de saisie de la cle API Nexus (menu Parametres) et
// enregistre immediatement si validee.
static void ChooseNexusApiKey(HWND hwnd) {
    std::wstring key = g_nexusApiKey;
    if (!ShowTextInputDialog(hwnd, L"Cle API Nexus Mods",
            L"Cle API personnelle (Compte Nexus > Parametres > API Keys) :", key))
        return;   // annule
    g_nexusApiKey = Clean(key);
    SaveIni();
    Info(hwnd, g_nexusApiKey.empty()
        ? L"Cle API Nexus effacee - la verification Nexus sera desactivee."
        : L"Cle API Nexus enregistree.");
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
// Chaque action prend l'index du mod concerne EXPLICITEMENT (idx), passe par
// le bouton de la carte qui l'a declenchee - il n'y a plus de "selection"
// au sens ListView depuis le passage a la vue en cartes.
static bool ValidMod(HWND hwnd, int idx) {
    if (idx >= 0 && idx < (int)g_mods.size()) return true;
    Info(hwnd, L"Ce mod n'est plus dans la liste (elle a change entre-temps).");
    return false;
}
static void ActionOpen(HWND hwnd, int idx, bool stamp) {
    if (!ValidMod(hwnd, idx)) return;
    std::wstring url = g_mods[idx].url;
    if (url.empty()) { Info(hwnd, L"Ce mod n'a pas de lien."); return; }
    ShellExecuteW(NULL, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    if (stamp) {
        g_mods[idx].last = NowStamp();
        SaveMods();
        RefillMods();
    }
}
static void ActionMark(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    g_mods[idx].last = NowStamp();
    SaveMods();
    RefillMods();
}
static void ActionAdd(HWND hwnd) {
    Mod m;
    std::wstring clip = ClipboardText();
    if (clip.compare(0, 4, L"http") == 0 && clip.size() < 400) m.url = clip;
    if (ShowModEditor(hwnd, L"Ajouter un mod", m)) {
        g_mods.push_back(m);
        SaveMods();
        RefillMods();
        ScrollToMod(m.uid);
    }
}
static void ActionEdit(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    Mod m = g_mods[idx];
    if (ShowModEditor(hwnd, L"Modifier le mod", m)) {
        g_mods[idx] = m;
        SaveMods();
        RefillMods();
        ScrollToMod(m.uid);
    }
}
static void ActionDelete(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    std::wstring q = L"Supprimer \"" + g_mods[idx].name + L"\" de la liste ?\n"
                     L"(le mod n'est pas desinstalle, seule la fiche est supprimee)";
    if (MessageBoxW(hwnd, q.c_str(), L"ValMods", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    g_mods.erase(g_mods.begin() + idx);
    SaveMods();
    RefillMods();
}
static void ActionCopy(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    CopyToClipboard(hwnd, g_mods[idx].url);
}
static void ActionOpenHistory(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    if (g_mods[idx].changelogUrl.empty()) {
        Info(hwnd, L"Ce mod n'a pas de lien vers son historique des versions.\n"
                   L"Ajoute-le en modifiant le mod (bouton Modif.).");
        return;
    }
    ShellExecuteW(NULL, L"open", g_mods[idx].changelogUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
}
static void ActionLocateDll(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    const Mod& m = g_mods[idx];
    if (m.modDir.empty()) {
        Info(hwnd, L"Aucun dossier de mod associe a ce mod.\nAssocie-le en modifiant le mod (bouton Modif.).");
        return;
    }
    if (!DirExists(m.modDir)) {
        std::wstring msg = L"Le dossier du mod associe est introuvable :\n" + m.modDir;
        MessageBoxW(hwnd, msg.c_str(), L"ValMods", MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring dll = ResolveModDll(m);
    if (dll.empty()) {
        std::wstring msg = L"Aucun DLL trouve dans ce dossier :\n" + m.modDir;
        MessageBoxW(hwnd, msg.c_str(), L"ValMods", MB_OK | MB_ICONWARNING);
        return;
    }
    RevealFile(dll);
}
static void ActionOpenDllDir(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    const std::wstring& dir = g_mods[idx].modDir;
    if (dir.empty()) {
        Info(hwnd, L"Aucun dossier de mod associe a ce mod.\nAssocie-le en modifiant le mod (bouton Modif.).");
        return;
    }
    OpenFolder(hwnd, dir, L"dossier du mod");
}
static void ActionLocateConfig(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    const std::wstring& p = g_mods[idx].configPath;
    if (p.empty()) {
        Info(hwnd, L"Aucun fichier de config associe a ce mod.\nAssocie-le en modifiant le mod (bouton Modif.).");
        return;
    }
    if (!FileExists(p)) {
        std::wstring msg = L"Le fichier de config associe est introuvable :\n" + p;
        MessageBoxW(hwnd, msg.c_str(), L"ValMods", MB_OK | MB_ICONWARNING);
        return;
    }
    RevealFile(p);
}
static void ActionOpenConfig(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    const std::wstring& p = g_mods[idx].configPath;
    if (p.empty()) {
        Info(hwnd, L"Aucun fichier de config associe a ce mod.\nAssocie-le en modifiant le mod (bouton Modif.).");
        return;
    }
    if (!FileExists(p)) {
        std::wstring msg = L"Le fichier de config associe est introuvable :\n" + p;
        MessageBoxW(hwnd, msg.c_str(), L"ValMods", MB_OK | MB_ICONWARNING);
        return;
    }
    // ouvre avec l'application associee (Notepad par defaut pour .cfg sous
    // Windows, ou tout autre editeur que l'utilisateur a associe) plutot
    // que de forcer un programme en particulier.
    ShellExecuteW(NULL, L"open", p.c_str(), NULL, NULL, SW_SHOWNORMAL);
}
static void ShowRowOverflowMenu(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    g_ctxMenuModIndex = idx;
    POINT pt; GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_CTX_COPY,        L"Copier le lien");
    AppendMenuW(m, MF_STRING, IDM_CTX_LOCATE_DLL,  L"Localiser le DLL dans l'explorateur");
    AppendMenuW(m, MF_STRING, IDM_CTX_OPEN_DLLDIR, L"Ouvrir le dossier du mod");
    AppendMenuW(m, MF_STRING, IDM_CTX_LOCATE_CONFIG, L"Localiser le fichier de config");
    AppendMenuW(m, MF_STRING, IDM_CTX_OPEN_CONFIG,   L"Ouvrir le fichier de config");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_CTX_DELETE,      L"Supprimer");
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(m);
}
// Verifie la derniere version publiee (bouton eclair sur une carte),
// via Thunderstore ou Nexus selon la source configuree pour ce mod (voir
// Mod::apiSource / menu deroulant "Source de verification" de l'editeur).
static void ActionCheckThunderstore(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    if (g_mods[idx].apiSource == API_NONE) {
        Info(hwnd, L"La verification est desactivee pour ce mod (source \"Aucune\", voir Modifier).");
        return;
    }
    std::wstring url = g_mods[idx].url;
    int uid = g_mods[idx].uid;
    bool useNexus  = (g_mods[idx].apiSource == API_NEXUS);
    bool useHexium = (g_mods[idx].apiSource == API_HEXIUM);

    std::wstring latestVersion, latestDate, description, error;
    bool matched = false, ok = false, deprecated = false;

    HCURSOR oldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    if (useNexus) {
        NexusCheckResult r = CheckNexusVersion(url);
        matched = r.isNexus; ok = r.ok; error = r.error;
        latestVersion = r.latestVersion; latestDate = r.latestDate; description = r.description;
        deprecated = r.unavailable;
    } else if (useHexium) {
        HexiumCheckResult r = CheckHexiumVersion(url);
        matched = r.isHexium; ok = r.ok; error = r.error;
        latestVersion = r.latestVersion; latestDate = r.latestDate; description = r.description;
        deprecated = r.deprecated;
    } else {
        TsCheckResult r = CheckThunderstoreVersion(url);
        matched = r.isThunderstore; ok = r.ok; error = r.error;
        latestVersion = r.latestVersion; latestDate = r.latestDate; description = r.description;
        deprecated = r.deprecated;
    }
    SetCursor(oldCursor);

    if (!matched) {
        std::wstring m = useNexus
            ? L"Le lien de ce mod ne pointe pas vers nexusmods.com.\n"
              L"La verification Nexus ne fonctionne que pour un lien "
              L"nexusmods.com/valheim/mods/<id>."
            : useHexium
            ? L"Le lien de ce mod ne pointe pas vers hexium.gg.\n"
              L"La verification Hexium ne fonctionne que pour un lien "
              L"valheim.hexium.gg/mods/<equipe>/<nom>."
            : L"Le lien de ce mod ne pointe pas vers thunderstore.io.\n"
              L"La verification automatique ne fonctionne que pour les mods\n"
              L"heberges sur Thunderstore (change la source en \"Nexus\"/\"Hexium\" "
              L"dans Modifier si ce mod vient de l'un de ces sites).";
        Info(hwnd, m.c_str());
        return;
    }
    if (!ok) {
        std::wstring m = L"Verification impossible :\n" + error;
        MessageBoxW(hwnd, m.c_str(), L"ValMods", MB_OK | MB_ICONWARNING);
        return;
    }

    // la liste peut avoir ete retriee pendant l'appel reseau : on retrouve
    // le mod par son identifiant stable (voir Mod::uid), PAS par son nom -
    // deux mods peuvent partager le meme nom (le meme mod ajoute a deux
    // modpacks comme deux fiches separees), et chercher par nom mettrait a
    // jour la mauvaise fiche dans ce cas.
    for (size_t k = 0; k < g_mods.size(); ++k) {
        if (g_mods[k].uid == uid) {
            g_mods[k].tsVersion = latestVersion;
            g_mods[k].tsLatestDate = latestDate;
            // bonus non-destructif : on ne remplit la description que si
            // elle est vide, contrairement a Auto-remplir qui l'ecrase
            // toujours (ici ce n'est pas l'action demandee explicitement).
            if (g_mods[k].description.empty() && !description.empty())
                g_mods[k].description = description;
            g_mods[k].last = NowStamp();   // une verification en ligne compte comme une verification
            SaveMods();
            RefillMods();
            break;
        }
    }

    std::wstring sourceLabel = useNexus ? L"Nexus" : (useHexium ? L"Hexium" : L"Thunderstore");
    std::wstring msg = std::wstring(L"Derniere version publiee sur ") + sourceLabel +
        L" : " + latestVersion;
    if (deprecated)
        msg += useNexus
            ? L"\n\nATTENTION : ce mod semble retire/masque sur Nexus."
            : L"\n\nATTENTION : ce mod est marque comme deprecie (abandonne) "
              L"par son auteur sur " + sourceLabel + L".";
    Info(hwnd, msg.c_str());
}
// Un mod est eligible a "Tout verifier" si sa source n'est pas desactivee
// et que son lien correspond bien au site attendu par cette source.
static bool ModEligibleForBulkCheck(const Mod& m) {
    if (m.apiSource == API_NONE) return false;
    std::wstring a, b;
    if (m.apiSource == API_NEXUS)  return ParseNexusUrl(m.url, a);
    if (m.apiSource == API_HEXIUM) return ParseHexiumUrl(m.url, a, b);
    return ParseThunderstoreUrl(m.url, a, b);
}
// Verifie tous les mods eligibles (Thunderstore ou Nexus selon leur source,
// voir ModEligibleForBulkCheck) en une seule fois. Synchrone et sequentiel
// comme le reste des appels reseau de l'appli - potentiellement long pour
// une longue liste, d'ou la confirmation prealable qui indique combien de
// mods sont concernes avant de se lancer.
static void ActionCheckAll(HWND hwnd) {
    if (g_mods.empty()) { Info(hwnd, L"Aucun mod dans la liste."); return; }

    int eligible = 0;
    for (size_t i = 0; i < g_mods.size(); ++i) {
        if (!ModMatchesFilters(g_mods[i])) continue;
        if (ModEligibleForBulkCheck(g_mods[i])) ++eligible;
    }
    if (eligible == 0) {
        Info(hwnd, L"Aucun mod eligible : il faut un lien vers thunderstore.io,\n"
                   L"nexusmods.com ou hexium.gg correspondant a la source choisie\n"
                   L"(voir Modifier), ne pas etre marque \"Aucune\", et correspondre\n"
                   L"aux filtres modpack/tag actuels s'il y en a.");
        return;
    }
    std::wstring packLabel = g_modpackFilter.empty() ? L"tous les mods" : (L"modpack \"" + g_modpackFilter + L"\"");
    if (!g_tagFilter.empty()) packLabel += L", tag \"" + g_tagFilter + L"\"";
    std::wstring q = L"Verifier les " + std::to_wstring(eligible) + L" mod(s) eligibles de la liste (" +
        packLabel + L") ?\n\n"
        L"Derniere verification globale : " +
        (g_lastGlobalCheck.empty() ? L"jamais" : g_lastGlobalCheck) + L"\n\n"
        L"Ca ne modifie PAS la date de verification individuelle de chaque\n"
        L"mod (reservee a une verification manuelle) - seule une date de\n"
        L"verification globale, separee, sera mise a jour.\n\n"
        L"Ca peut prendre du temps : un appel reseau par mod, l'un apres\n"
        L"l'autre (pas en parallele).";
    if (MessageBoxW(hwnd, q.c_str(), L"ValMods", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    // On retrouve chaque mod par son identifiant stable (voir Mod::uid),
    // PAS par son nom : deux mods peuvent partager le meme nom (le meme mod
    // ajoute a deux modpacks differents comme deux fiches separees), et
    // chercher par nom ferait retomber les DEUX occurrences sur la premiere
    // trouvee - un des deux mods serait alors traite deux fois (une fois
    // pour rien) pendant que l'autre ne serait jamais verifie du tout.
    std::vector<int> uids;
    for (size_t i = 0; i < g_mods.size(); ++i) uids.push_back(g_mods[i].uid);

    HCURSOR oldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    int outdated = 0, upToDate = 0, errors = 0, skipped = 0;
    for (size_t i = 0; i < uids.size(); ++i) {
        int idx = -1;
        for (size_t k = 0; k < g_mods.size(); ++k)
            if (g_mods[k].uid == uids[i]) { idx = (int)k; break; }
        if (idx < 0) continue;
        if (!ModMatchesFilters(g_mods[idx]) || !ModEligibleForBulkCheck(g_mods[idx])) { ++skipped; continue; }

        std::wstring url = g_mods[idx].url;
        std::wstring latestVersion, latestDate, description; bool ok = false;
        if (g_mods[idx].apiSource == API_NEXUS) {
            NexusCheckResult r = CheckNexusVersion(url);
            ok = r.ok; latestVersion = r.latestVersion; latestDate = r.latestDate; description = r.description;
        } else if (g_mods[idx].apiSource == API_HEXIUM) {
            HexiumCheckResult r = CheckHexiumVersion(url);
            ok = r.ok; latestVersion = r.latestVersion; latestDate = r.latestDate; description = r.description;
        } else {
            TsCheckResult r = CheckThunderstoreVersion(url);
            ok = r.ok; latestVersion = r.latestVersion; latestDate = r.latestDate; description = r.description;
        }
        if (!ok) { ++errors; continue; }

        bool isOutdated = false;
        std::wstring installed = GetEffectiveInstalledVersion(g_mods[idx]);
        if (!installed.empty() && VersionLess(installed, latestVersion)) isOutdated = true;

        g_mods[idx].tsVersion = latestVersion;
        g_mods[idx].tsLatestDate = latestDate;
        if (g_mods[idx].description.empty() && !description.empty())
            g_mods[idx].description = description;
        // PAS de g_mods[idx].last ici : "Tout verifier" est une operation
        // groupee/automatisee, distincte d'une verification personnelle -
        // seule g_lastGlobalCheck (globale, separee) est mise a jour plus
        // bas. Toucher "last" ici casserait le code couleur individuel
        // (rouge/orange/vert/gris) qui sert justement a repondre a "quels
        // mods n'ai-je pas regardes moi-meme depuis longtemps ?".

        if (isOutdated) ++outdated; else ++upToDate;
    }
    SetCursor(oldCursor);

    g_lastGlobalCheck = NowStamp();
    SaveMods();
    RefillMods();

    std::wstring summary = L"Verification terminee (" + g_lastGlobalCheck + L").\n\n" +
        std::to_wstring(outdated) + L" mise(s) a jour disponible(s)\n" +
        std::to_wstring(upToDate) + L" a jour\n" +
        std::to_wstring(errors)  + L" erreur(s)\n" +
        std::to_wstring(skipped) + L" ignore(s) (source \"Aucune\" / lien non reconnu / hors modpack ou tag)";
    Info(hwnd, summary.c_str());
}
// Un mod a des informations "completables" s'il lui manque au moins un des
// champs que l'auto-remplissage peut fournir. Nexus n'expose pas de
// categorie/lien historique (voir FetchNexusAutofill) : on ne considere que
// description/icone pour lui, sinon un mod Nexus resterait indefiniment
// "incomplet" pour des champs qu'aucune source ne peut jamais renseigner.
static bool ModMissingAutofillableField(const Mod& m) {
    if (m.apiSource == API_NEXUS) return m.description.empty() || m.iconPath.empty();
    // Hexium fournit une categorie (owner) mais pas de lien de changelog
    // separe (voir FetchHexiumAutofill) - meme logique que Nexus pour ce
    // champ, mais categorie tout de meme prise en compte comme Thunderstore.
    if (m.apiSource == API_HEXIUM) return m.cat.empty() || m.description.empty() || m.iconPath.empty();
    return m.cat.empty() || m.changelogUrl.empty() || m.description.empty() || m.iconPath.empty();
}
// "Completer la liste" (menu Outils) : repasse l'auto-remplissage sur tous
// les mods eligibles, mais de facon NON DESTRUCTIVE - contrairement au
// bouton Auto-remplir de l'editeur qui ecrase toujours les champs, ici on
// ne touche QUE ce qui est actuellement vide (categorie / lien historique /
// description / icone), pour ne jamais effacer une valeur deja saisie ou
// corrigee a la main. tsVersion/tsLatestDate sont rafraichis a chaque fois
// (bonus sans risque, deja recuperes par le meme appel reseau) ; en
// revanche installedVersion n'est JAMAIS touche - ce champ est reserve a
// Telecharger/Tout DL ou a une saisie manuelle (voir GetEffectiveInstalledVersion).
static void ActionFixList(HWND hwnd) {
    if (g_mods.empty()) { Info(hwnd, L"Aucun mod dans la liste."); return; }

    int candidates = 0;
    for (size_t i = 0; i < g_mods.size(); ++i) {
        if (!ModMatchesFilters(g_mods[i])) continue;
        if (ModEligibleForBulkCheck(g_mods[i]) && ModMissingAutofillableField(g_mods[i])) ++candidates;
    }
    if (candidates == 0) {
        Info(hwnd, L"Rien a completer parmi les mods eligibles (lien Thunderstore/\n"
                   L"Nexus valide, source non \"Aucune\", et correspondant aux filtres\n"
                   L"modpack/tag actuels s'il y en a) : categorie, lien historique,\n"
                   L"description et icone sont deja renseignes partout.");
        return;
    }
    std::wstring packLabel = g_modpackFilter.empty() ? L"tous les mods" : (L"modpack \"" + g_modpackFilter + L"\"");
    if (!g_tagFilter.empty()) packLabel += L", tag \"" + g_tagFilter + L"\"";
    std::wstring q = L"Completer les informations manquantes de " + std::to_wstring(candidates) +
        L" mod(s) de la liste (" + packLabel + L") ?\n\n"
        L"Remplit UNIQUEMENT les champs actuellement VIDES (categorie, lien\n"
        L"historique, description, icone) - une valeur deja saisie ou\n"
        L"corrigee a la main n'est jamais ecrasee. La derniere version\n"
        L"connue (tsVersion) est aussi rafraichie au passage.\n\n"
        L"Ne touche JAMAIS la version installee (installedVersion).\n\n"
        L"Ca peut prendre du temps : un appel reseau par mod concerne, l'un\n"
        L"apres l'autre (pas en parallele).";
    if (MessageBoxW(hwnd, q.c_str(), L"ValMods", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    // Identifiants stables (voir Mod::uid) plutot que des index directs : un
    // appel reseau peut prendre du temps, mieux vaut ne jamais supposer que
    // g_mods n'a pas bouge entre-temps (voir ActionCheckAll, meme logique).
    std::vector<int> uids;
    for (size_t i = 0; i < g_mods.size(); ++i) uids.push_back(g_mods[i].uid);

    HCURSOR oldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    int filled = 0, alreadyOk = 0, errors = 0, skipped = 0;
    for (size_t i = 0; i < uids.size(); ++i) {
        int idx = -1;
        for (size_t k = 0; k < g_mods.size(); ++k)
            if (g_mods[k].uid == uids[i]) { idx = (int)k; break; }
        if (idx < 0) continue;
        if (!ModMatchesFilters(g_mods[idx]) || !ModEligibleForBulkCheck(g_mods[idx]) ||
            !ModMissingAutofillableField(g_mods[idx])) { ++skipped; continue; }

        Mod& m = g_mods[idx];
        std::wstring category, changelogUrl, description, iconPath, tsVersion, tsLatestDate;
        bool ok = false;
        if (m.apiSource == API_NEXUS) {
            NexusAutofillResult r = FetchNexusAutofill(m.url);
            ok = r.ok;
            description = r.description; iconPath = r.localIconPath;
            tsVersion = r.latestVersion; tsLatestDate = r.latestDate;
        } else if (m.apiSource == API_HEXIUM) {
            HexiumAutofillResult r = FetchHexiumAutofill(m.url);
            ok = r.ok;
            category = r.category; description = r.description; iconPath = r.localIconPath;
            tsVersion = r.latestVersion; tsLatestDate = r.latestDate;
        } else {
            TsAutofillResult r = FetchThunderstoreAutofill(m.url);
            ok = r.ok;
            category = r.category; changelogUrl = r.changelogUrl; description = r.description;
            iconPath = r.localIconPath;
            tsVersion = r.latestVersion; tsLatestDate = r.latestDate;
        }
        if (!ok) { ++errors; continue; }

        bool changed = false;
        if (m.cat.empty() && !category.empty())               { m.cat = category;               changed = true; }
        if (m.changelogUrl.empty() && !changelogUrl.empty()) { m.changelogUrl = changelogUrl;   changed = true; }
        if (m.description.empty() && !description.empty())   { m.description = description;    changed = true; }
        if (m.iconPath.empty() && !iconPath.empty())          { m.iconPath = iconPath;           changed = true; }
        if (!tsVersion.empty())    m.tsVersion = tsVersion;       // bonus non-destructif : toujours a jour
        if (!tsLatestDate.empty()) m.tsLatestDate = tsLatestDate;

        if (changed) ++filled; else ++alreadyOk;
        // PAS de m.last ni de g_lastGlobalCheck ici : completer des champs
        // descriptifs n'est ni une verification personnelle, ni "Tout
        // verifier" - les deux dates gardent leur sens propre (voir plus haut).
    }
    SetCursor(oldCursor);

    SaveMods();
    RefillMods();

    std::wstring summary = L"Completion terminee.\n\n" +
        std::to_wstring(filled)    + L" mod(s) complete(s)\n" +
        std::to_wstring(alreadyOk) + L" deja a jour (rien de nouveau trouve)\n" +
        std::to_wstring(errors)    + L" erreur(s)\n" +
        std::to_wstring(skipped)   + L" ignore(s) (source \"Aucune\" / lien non reconnu / "
                                      L"deja complets / hors modpack ou tag)";
    Info(hwnd, summary.c_str());
}
static void ActionDownloadLatest(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    if (g_mods[idx].apiSource == API_NEXUS) {
        Info(hwnd, L"Le telechargement direct n'est pas disponible pour Nexus\n"
                   L"(l'API de telechargement de Nexus est reservee aux comptes\n"
                   L"Premium) - utilise le bouton Watch pour aller chercher le\n"
                   L"fichier a la main sur la page du mod.");
        return;
    }
    if (g_mods[idx].apiSource == API_NONE) {
        Info(hwnd, L"Le telechargement est desactive pour ce mod (source \"Aucune\", voir Modifier).");
        return;
    }
    bool useHexium = (g_mods[idx].apiSource == API_HEXIUM);
    std::wstring url = g_mods[idx].url;
    int uid = g_mods[idx].uid;

    if (useHexium) {
        // Contrairement a Thunderstore, il n'existe pas de pattern d'URL de
        // telechargement a reconstruire a la main : on interroge Hexium
        // pour recuperer le download_url de la derniere version, meme si
        // tsVersion est deja connu (voir CheckHexiumVersion/DownloadHexiumZip).
        std::wstring team, pkgName;
        if (!ParseHexiumUrl(url, team, pkgName)) {
            Info(hwnd, L"Le lien de ce mod ne pointe pas vers hexium.gg :\n"
                       L"le telechargement direct ne fonctionne que pour les mods\n"
                       L"heberges sur Hexium.");
            return;
        }
        HCURSOR oldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
        HexiumCheckResult chk = CheckHexiumVersion(url);
        SetCursor(oldCursor);
        if (!chk.ok || chk.downloadUrl.empty()) {
            std::wstring m = L"Impossible de determiner la derniere version :\n" +
                (chk.ok ? std::wstring(L"lien de telechargement manquant dans la reponse Hexium.") : chk.error);
            MessageBoxW(hwnd, m.c_str(), L"ValMods", MB_OK | MB_ICONWARNING);
            return;
        }

        MakeDirs(DownloadsRoot());
        std::wstring suggested = SanitizeFileName(team) + L"-" + SanitizeFileName(pkgName) + L"-" +
                                 SanitizeFileName(chk.latestVersion) + L".zip";
        std::wstring destPath = BrowseSaveFile(hwnd,
            L"Archive zip (*.zip)\0*.zip\0Tous les fichiers (*.*)\0*.*\0",
            DownloadsRoot(), suggested, L"Enregistrer le zip du mod sous...");
        if (destPath.empty()) return;   // annule par l'utilisateur

        oldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
        std::wstring err;
        bool ok = DownloadHexiumZip(chk.downloadUrl, destPath, err);
        SetCursor(oldCursor);

        if (!ok) {
            std::wstring m = L"Telechargement impossible :\n" + err;
            MessageBoxW(hwnd, m.c_str(), L"ValMods", MB_OK | MB_ICONWARNING);
            return;
        }
        for (size_t k = 0; k < g_mods.size(); ++k) {
            if (g_mods[k].uid == uid) {
                g_mods[k].tsVersion = chk.latestVersion;
                g_mods[k].tsLatestDate = chk.latestDate;
                g_mods[k].installedVersion = chk.latestVersion;
                g_mods[k].last = NowStamp();
                SaveMods();
                RefillMods();
                break;
            }
        }
        std::wstring msg = L"Version " + chk.latestVersion + L" telechargee :\n" + destPath +
            L"\n\nA extraire toi-meme dans BepInEx\\plugins - ValMods ne modifie\n"
            L"jamais tes fichiers de jeu automatiquement.\n\n"
            L"La version suivie comme installee a ete mise a jour a " + chk.latestVersion +
            L" (a corriger dans Modifier si tu n'extrais pas ce zip).";
        Info(hwnd, msg.c_str());
        return;
    }

    std::wstring ns, pkgName;
    if (!ParseThunderstoreUrl(url, ns, pkgName)) {
        Info(hwnd, L"Le lien de ce mod ne pointe pas vers thunderstore.io :\n"
                   L"le telechargement direct ne fonctionne que pour les mods\n"
                   L"heberges sur Thunderstore.");
        return;
    }

    std::wstring version = g_mods[idx].tsVersion;
    std::wstring latestDate = g_mods[idx].tsLatestDate;
    if (version.empty()) {
        // pas encore verifie : on va d'abord chercher la derniere version
        HCURSOR oldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
        TsCheckResult chk = CheckThunderstoreVersion(url);
        SetCursor(oldCursor);
        if (!chk.ok) {
            std::wstring m = L"Impossible de determiner la derniere version :\n" + chk.error;
            MessageBoxW(hwnd, m.c_str(), L"ValMods", MB_OK | MB_ICONWARNING);
            return;
        }
        version = chk.latestVersion;
        latestDate = chk.latestDate;
    }

    // "Parcourir" pour choisir ou enregistrer le zip - suggestion par defaut
    // dans downloads/ mais entierement modifiable, comme demande.
    MakeDirs(DownloadsRoot());
    std::wstring suggested = SanitizeFileName(ns) + L"-" + SanitizeFileName(pkgName) + L"-" +
                             SanitizeFileName(version) + L".zip";
    std::wstring destPath = BrowseSaveFile(hwnd,
        L"Archive zip (*.zip)\0*.zip\0Tous les fichiers (*.*)\0*.*\0",
        DownloadsRoot(), suggested, L"Enregistrer le zip du mod sous...");
    if (destPath.empty()) return;   // annule par l'utilisateur

    HCURSOR oldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    std::wstring err;
    bool ok = DownloadThunderstoreZip(ns, pkgName, version, destPath, err);
    SetCursor(oldCursor);

    if (!ok) {
        std::wstring m = L"Telechargement impossible :\n" + err;
        MessageBoxW(hwnd, m.c_str(), L"ValMods", MB_OK | MB_ICONWARNING);
        return;
    }

    // confirme/rafraichit tsVersion + date de verification (le telechargement
    // implique qu'on connait desormais la version exacte tres precisement).
    // installedVersion est aussi mis a jour : on suppose que ce zip va etre
    // extrait dans BepInEx/plugins (editable a la main dans l'editeur si ce
    // n'est pas le cas, ou si l'installation se fait autrement).
    // identifiant stable (voir Mod::uid), PAS le nom - deux mods peuvent
    // partager le meme nom (le meme mod ajoute a deux modpacks differents).
    for (size_t k = 0; k < g_mods.size(); ++k) {
        if (g_mods[k].uid == uid) {
            g_mods[k].tsVersion = version;
            g_mods[k].tsLatestDate = latestDate;
            g_mods[k].installedVersion = version;
            g_mods[k].last = NowStamp();
            SaveMods();
            RefillMods();
            break;
        }
    }

    std::wstring msg = L"Version " + version + L" telechargee :\n" + destPath +
        L"\n\nA extraire toi-meme dans BepInEx\\plugins - ValMods ne modifie\n"
        L"jamais tes fichiers de jeu automatiquement.\n\n"
        L"La version suivie comme installee a ete mise a jour a " + version +
        L" (a corriger dans Modifier si tu n'extrais pas ce zip).";
    Info(hwnd, msg.c_str());
}
// Telecharge le zip de la derniere version de tous les mods eligibles dans
// UN SEUL dossier choisi une fois pour toutes (au lieu d'une boite
// "Enregistrer sous..." par mod). Ne met PAS a jour la date de verification
// individuelle de chaque mod - meme logique que "Tout verifier" (voir
// ActionCheckAll) : une operation groupee n'est pas une verification
// personnelle. tsVersion est en revanche mis a jour (donnee utile, pas un
// "j'ai regarde ce mod moi-meme").
static void ActionDownloadAll(HWND hwnd) {
    if (g_mods.empty()) { Info(hwnd, L"Aucun mod dans la liste."); return; }

    int eligible = 0;
    for (size_t i = 0; i < g_mods.size(); ++i) {
        if (!ModPassesFilters(g_mods[i])) continue;
        std::wstring a, b;
        if (g_mods[i].apiSource == API_THUNDERSTORE && ParseThunderstoreUrl(g_mods[i].url, a, b)) ++eligible;
        else if (g_mods[i].apiSource == API_HEXIUM && ParseHexiumUrl(g_mods[i].url, a, b)) ++eligible;
    }
    if (eligible == 0) {
        Info(hwnd, L"Aucun mod eligible : il faut un lien vers thunderstore.io ou\n"
                   L"hexium.gg avec la source correspondante (voir Modifier - le\n"
                   L"telechargement direct n'est pas disponible pour Nexus), et\n"
                   L"correspondre aux filtres actuels (modpack / tag / masquage des\n"
                   L"mods a jour).");
        return;
    }

    std::wstring packLabel = g_modpackFilter.empty() ? L"tous les mods" : (L"modpack \"" + g_modpackFilter + L"\"");
    if (!g_tagFilter.empty()) packLabel += L", tag \"" + g_tagFilter + L"\"";
    std::wstring q = L"Telecharger les zips des " + std::to_wstring(eligible) +
        L" mod(s) Thunderstore/Hexium de la liste (" + packLabel +
        (g_hideUpToDate ? L", mods a jour masques" : L"") + L") dans UN dossier de ton choix ?\n\n"
        L"Un zip par mod. Si sa derniere version n'est pas deja connue, elle\n"
        L"est verifiee d'abord (appel reseau supplementaire pour ce mod).\n\n"
        L"Ca peut prendre du temps sur une longue liste.";
    if (MessageBoxW(hwnd, q.c_str(), L"ValMods", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    MakeDirs(DownloadsRoot());
    std::wstring targetDir = BrowseFolder(hwnd,
        L"Choisir le dossier ou enregistrer tous les zips", DownloadsRoot());
    if (targetDir.empty()) return;   // annule

    // identifiants stables (voir Mod::uid), PAS les noms : deux mods du meme
    // nom (le meme mod ajoute a deux modpacks differents comme deux fiches
    // separees) feraient sinon retomber les deux occurrences sur la premiere
    // trouvee - un modpack traiterait deux fois la meme fiche pendant que
    // l'autre modpack ne recevrait jamais son propre telechargement.
    std::vector<int> uids;
    for (size_t i = 0; i < g_mods.size(); ++i) uids.push_back(g_mods[i].uid);

    HCURSOR oldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    int downloaded = 0, errors = 0, skipped = 0;
    for (size_t i = 0; i < uids.size(); ++i) {
        int idx = -1;
        for (size_t k = 0; k < g_mods.size(); ++k)
            if (g_mods[k].uid == uids[i]) { idx = (int)k; break; }
        if (idx < 0) continue;
        bool isTs = (g_mods[idx].apiSource == API_THUNDERSTORE);
        bool isHexium = (g_mods[idx].apiSource == API_HEXIUM);
        if ((!isTs && !isHexium) || !ModPassesFilters(g_mods[idx])) { ++skipped; continue; }

        std::wstring ns, nm;
        if (isTs) {
            if (!ParseThunderstoreUrl(g_mods[idx].url, ns, nm)) { ++skipped; continue; }

            std::wstring version = g_mods[idx].tsVersion;
            if (version.empty()) {
                TsCheckResult chk = CheckThunderstoreVersion(g_mods[idx].url);
                if (!chk.ok) { ++errors; continue; }
                version = chk.latestVersion;
                g_mods[idx].tsVersion = version;
                g_mods[idx].tsLatestDate = chk.latestDate;
            }

            std::wstring destPath = targetDir + L"\\" + SanitizeFileName(ns) + L"-" +
                                    SanitizeFileName(nm) + L"-" + SanitizeFileName(version) + L".zip";
            std::wstring err;
            if (DownloadThunderstoreZip(ns, nm, version, destPath, err)) {
                ++downloaded;
                // meme logique que Telecharger (mod par mod) : on suppose que ce
                // zip va etre extrait, donc on suit cette version comme installee.
                g_mods[idx].installedVersion = version;
            } else {
                ++errors;
            }
        } else {   // isHexium
            if (!ParseHexiumUrl(g_mods[idx].url, ns, nm)) { ++skipped; continue; }

            // Pas de pattern d'URL a reconstruire : il faut le download_url
            // renvoye par Hexium, donc on interroge systematiquement (mis en
            // cache par FetchHexiumPackageList, voir plus haut) meme si
            // tsVersion est deja connu.
            HexiumCheckResult chk = CheckHexiumVersion(g_mods[idx].url);
            if (!chk.ok || chk.downloadUrl.empty()) { ++errors; continue; }
            g_mods[idx].tsVersion = chk.latestVersion;
            g_mods[idx].tsLatestDate = chk.latestDate;

            std::wstring destPath = targetDir + L"\\" + SanitizeFileName(ns) + L"-" +
                                    SanitizeFileName(nm) + L"-" + SanitizeFileName(chk.latestVersion) + L".zip";
            std::wstring err;
            if (DownloadHexiumZip(chk.downloadUrl, destPath, err)) {
                ++downloaded;
                g_mods[idx].installedVersion = chk.latestVersion;
            } else {
                ++errors;
            }
        }
    }
    SetCursor(oldCursor);

    SaveMods();
    RefillMods();

    std::wstring summary = L"Telechargement groupe termine.\n\n"
        L"Dossier : " + targetDir + L"\n\n" +
        std::to_wstring(downloaded) + L" telecharge(s)\n" +
        std::to_wstring(errors)     + L" erreur(s)\n" +
        std::to_wstring(skipped)    + L" ignore(s) (source non Thunderstore/Hexium / hors filtres)\n\n"
        L"A extraire toi-meme dans BepInEx\\plugins - ValMods ne modifie\n"
        L"jamais tes fichiers de jeu automatiquement.\n\n"
        L"La version suivie comme installee a ete mise a jour pour chaque\n"
        L"mod telecharge (a corriger dans Modifier si tu n'extrais pas un zip).";
    Info(hwnd, summary.c_str());
}

// ---------------------------------------------------------------- parametres
static void ChooseValheimDir(HWND hwnd) {
    // passe par le meme helper que le reste de l'appli (BrowseFolder) -
    // cette fonction dupliquait auparavant son corps ET son callback.
    std::wstring picked = BrowseFolder(hwnd,
        L"Selectionne le dossier d'installation de Valheim (celui qui contient valheim.exe)",
        g_valheimDir);
    if (picked.empty()) return;   // annule
    g_valheimDir = picked;
    SaveIni();
    if (!FileExists(g_valheimDir + L"\\valheim.exe"))
        Info(hwnd, L"Attention : valheim.exe n'a pas ete trouve dans ce dossier.\n"
                   L"Le chemin est quand meme enregistre.");
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

    // --- onglet mods : barre sur 5 rangees + panneau de cartes scrollable
    // (rangee 0 : recherche texte libre - rangee 1 : cases "sur quoi
    // chercher" - rangee 2 : tri de la liste - rangee 3 : filtres
    // (modpack/tag/masquage a jour) - rangee 4 : actions groupees, qui
    // operent sur le sous-ensemble filtre par les rangees 0/1/3)
    const int bh = 28, gap = 6, topH = 5 * (bh + gap) + 4;
    HWND searchBox = GetDlgItem(hwnd, IDC_SEARCHBOX);
    int searchClearW = 90;
    if (searchBox) MoveWindow(searchBox, px, py, pw - searchClearW - gap, bh, TRUE);
    HWND searchClearBtn = GetDlgItem(hwnd, IDC_SEARCHCLEAR);
    if (searchClearBtn) MoveWindow(searchClearBtn, px + pw - searchClearW, py, searchClearW, bh, TRUE);

    // Cases "sur quoi chercher" - alignees a la meme hauteur, largeurs
    // ajustees a la longueur de chaque libelle.
    int ySearchOpts = py + (bh + gap);
    int x = px;
    static const int searchChkW[] = { 60, 90, 100, 70, 70, 100, 70 };
    static const int searchChkIds[] = { IDC_SEARCH_NAME, IDC_SEARCH_CAT, IDC_SEARCH_DESC,
        IDC_SEARCH_NOTE, IDC_SEARCH_TAGS, IDC_SEARCH_MODPACK, IDC_SEARCH_URL };
    for (int si = 0; si < 7; ++si) {
        HWND chk = GetDlgItem(hwnd, searchChkIds[si]);
        if (chk) MoveWindow(chk, x, ySearchOpts, searchChkW[si], bh, TRUE);
        x += searchChkW[si] + gap;
    }

    int y1 = py + 2 * (bh + gap);
    HWND badd = GetDlgItem(hwnd, IDC_BADD);
    x = px;
    if (badd) MoveWindow(badd, x, y1, 100, bh, TRUE);
    x += 100 + gap;
    HWND combo = GetDlgItem(hwnd, IDC_SORTCOMBO);
    // la hauteur passee a un CBS_DROPDOWNLIST fixe celle de la liste DEROULEE,
    // pas celle du controle ferme (determinee par la police) - 200 est large.
    if (combo) MoveWindow(combo, x, y1, 220, 200, TRUE);
    x += 220 + gap;
    HWND dirBtn = GetDlgItem(hwnd, IDC_SORTDIR);
    if (dirBtn) MoveWindow(dirBtn, x, y1, 120, bh, TRUE);

    int y2 = py + 3 * (bh + gap);
    x = px;
    HWND packCombo = GetDlgItem(hwnd, IDC_MODPACK);
    if (packCombo) MoveWindow(packCombo, x, y2, 180, 200, TRUE);
    x += 180 + gap;
    HWND tagCombo = GetDlgItem(hwnd, IDC_TAGFILTER);
    if (tagCombo) MoveWindow(tagCombo, x, y2, 160, 200, TRUE);
    x += 160 + gap;
    HWND hideUpToDateChk = GetDlgItem(hwnd, IDC_HIDEUPTODATE);
    if (hideUpToDateChk) MoveWindow(hideUpToDateChk, x, y2, 150, bh, TRUE);

    int y3 = py + 4 * (bh + gap);
    x = px;
    HWND checkAllBtn = GetDlgItem(hwnd, IDC_CHECKALL);
    if (checkAllBtn) MoveWindow(checkAllBtn, x, y3, 130, bh, TRUE);
    x += 130 + gap;
    HWND dlAllBtn = GetDlgItem(hwnd, IDC_DLALL);
    if (dlAllBtn) MoveWindow(dlAllBtn, x, y3, 110, bh, TRUE);
    x += 110 + gap;
    HWND show10Chk = GetDlgItem(hwnd, IDC_SHOW10);
    if (show10Chk) MoveWindow(show10Chk, x, y3, 90, bh, TRUE);
    if (g_hCardsHost) MoveWindow(g_hCardsHost, px, py + topH, pw, ph - topH, TRUE);

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
// Case a cocher generique, position/taille reelles posees par LayoutAll
// ensuite (comme MkButton) - "checked" fixe l'etat initial (voir les
// reglages persistes g_searchIn*/g_hideUpToDate/g_showValheim10).
static HWND MkCheck(HWND p, int id, const wchar_t* txt, bool checked) {
    HWND h = CreateWindowExW(0, L"BUTTON", txt,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 10, 10,
        p, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(h, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
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

// ---------------------------------------------------------------- panneau cartes
// Fenetre custom qui heberge les cartes de mods et gere son propre
// defilement vertical (molette + barre de scroll). RefillMods() cree/detruit
// les controles enfants ; cette proc ne fait que scroller/redimensionner ce
// qui existe deja, et transmettre la couleur de texte des controles "status"
// (voir MkCardStatic : la couleur est stockee dans GWLP_USERDATA de chacun).
static LRESULT CALLBACK CardsHostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    // Un bouton envoie WM_COMMAND (BN_CLICKED) a son PARENT IMMEDIAT, qui est
    // g_hCardsHost pour tous les boutons de carte - pas la fenetre principale.
    // Sans ce relais, DefWindowProcW l'aurait simplement absorbe en silence :
    // c'est ce qui rendait tous les boutons de carte muets.
    case WM_COMMAND:
        return SendMessageW(GetParent(hwnd), WM_COMMAND, wp, lp);
    case WM_SIZE:
        UpdateCardsScrollInfo();
        RepositionCards();
        return 0;
    case WM_VSCROLL: {
        SCROLLINFO si; memset(&si, 0, sizeof(si));
        si.cbSize = sizeof(si); si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);
        int pos = si.nPos;
        switch (LOWORD(wp)) {
            case SB_LINEUP:   pos -= 30; break;
            case SB_LINEDOWN: pos += 30; break;
            case SB_PAGEUP:   pos -= (int)si.nPage; break;
            case SB_PAGEDOWN: pos += (int)si.nPage; break;
            case SB_THUMBTRACK: case SB_THUMBPOSITION: pos = si.nTrackPos; break;
            case SB_TOP:    pos = si.nMin; break;
            case SB_BOTTOM: pos = si.nMax; break;
            default: break;
        }
        int maxPos = si.nMax - (int)si.nPage + 1; if (maxPos < 0) maxPos = 0;
        if (pos < 0) pos = 0;
        if (pos > maxPos) pos = maxPos;
        g_scrollPos = pos;
        si.fMask = SIF_POS; si.nPos = pos;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        RepositionCards();
        RedrawCardsHost();
        return 0;
    }
    case WM_MOUSEWHEEL: {
        // GET_WHEEL_DELTA_WPARAM vient de <windowsx.h>, non inclus ici ;
        // c'est litteralement sa definition : le mot haut de wParam, signe.
        short delta = (short)HIWORD(wp);
        int amount = -((int)delta / WHEEL_DELTA) * 60;   // 60px par cran, sens naturel
        SCROLLINFO si; memset(&si, 0, sizeof(si));
        si.cbSize = sizeof(si); si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);
        int maxPos = si.nMax - (int)si.nPage + 1; if (maxPos < 0) maxPos = 0;
        int pos = g_scrollPos + amount;
        if (pos < 0) pos = 0;
        if (pos > maxPos) pos = maxPos;
        g_scrollPos = pos;
        si.fMask = SIF_POS; si.nPos = pos;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        RepositionCards();
        RedrawCardsHost();
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HWND ctrl = (HWND)lp;
        LONG_PTR cr = GetWindowLongPtrW(ctrl, GWLP_USERDATA);
        if (cr != 0) {
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, (COLORREF)cr);
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        break;   // pas de couleur assignee (nom du mod, separateur...) : par defaut
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
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
    AppendMenuW(d, MF_STRING, IDM_DOWNLOADS, L"Telechargements (zips Thunderstore)");
    HMENU p = CreatePopupMenu();
    AppendMenuW(p, MF_STRING, IDM_SETDIR, L"Definir le dossier de Valheim...");
    AppendMenuW(p, MF_STRING, IDM_NEXUSKEY, L"Definir la cle API Nexus...");
    HMENU o = CreatePopupMenu();
    AppendMenuW(o, MF_STRING, IDM_FIXLIST, L"Completer les infos manquantes...");
    HMENU a = CreatePopupMenu();
    AppendMenuW(a, MF_STRING, IDM_ABOUT, L"A propos");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)f, L"Fichier");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)d, L"Dossiers");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)p, L"Parametres");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)o, L"Outils");
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

        // page mods : barre (recherche + Ajouter + tri) + panneau de cartes
        // scrollable
        g_nMods = 0;

        // Recherche texte libre : filtre nom/categorie/description/note/
        // tags/modpack/lien (voir ModMatchesSearch), mise a jour a chaque
        // frappe (EN_CHANGE). Non persistee (voir g_searchQuery).
        HWND searchBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 10, 10, hwnd, (HMENU)IDC_SEARCHBOX, g_hInst, NULL);
        SendMessageW(searchBox, WM_SETFONT, (WPARAM)g_font, TRUE);
        // Texte d'invite affiche tant que le champ est vide et sans focus -
        // necessite Common Controls v6 (deja requis par le manifeste de
        // l'appli pour les autres controles modernes).
        SendMessageW(searchBox, EM_SETCUEBANNER, FALSE,
            (LPARAM)L"Rechercher (nom, categorie, description, note, tag, modpack, lien)...");
        g_pageMods[g_nMods++] = searchBox;

        HWND searchClearBtn = MkButton(hwnd, IDC_SEARCHCLEAR, L"Effacer");
        g_pageMods[g_nMods++] = searchClearBtn;

        // Cases a cocher "sur quoi chercher" (voir g_searchIn*/ModMatchesSearch) -
        // toutes cochees par defaut (comportement inchange si on n'y touche pas).
        HWND chkSearchName = MkCheck(hwnd, IDC_SEARCH_NAME, L"Nom", g_searchInName);
        g_pageMods[g_nMods++] = chkSearchName;
        HWND chkSearchCat = MkCheck(hwnd, IDC_SEARCH_CAT, L"Categorie", g_searchInCat);
        g_pageMods[g_nMods++] = chkSearchCat;
        HWND chkSearchDesc = MkCheck(hwnd, IDC_SEARCH_DESC, L"Description", g_searchInDesc);
        g_pageMods[g_nMods++] = chkSearchDesc;
        HWND chkSearchNote = MkCheck(hwnd, IDC_SEARCH_NOTE, L"Note", g_searchInNote);
        g_pageMods[g_nMods++] = chkSearchNote;
        HWND chkSearchTags = MkCheck(hwnd, IDC_SEARCH_TAGS, L"Tags", g_searchInTags);
        g_pageMods[g_nMods++] = chkSearchTags;
        HWND chkSearchModpack = MkCheck(hwnd, IDC_SEARCH_MODPACK, L"Modpack", g_searchInModpack);
        g_pageMods[g_nMods++] = chkSearchModpack;
        HWND chkSearchUrl = MkCheck(hwnd, IDC_SEARCH_URL, L"Lien", g_searchInUrl);
        g_pageMods[g_nMods++] = chkSearchUrl;

        g_pageMods[g_nMods++] = MkButton(hwnd, IDC_BADD, L"Ajouter");

        HWND combo = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 10, 200, hwnd, (HMENU)IDC_SORTCOMBO, g_hInst, NULL);
        SendMessageW(combo, WM_SETFONT, (WPARAM)g_font, TRUE);
        const wchar_t* sortLabels[] = { L"Nom", L"Categorie", L"Derniere verification",
                                        L"DLL", L"MAJ Thunderstore", L"Source", L"Lien", L"Note" };
        for (int si = 0; si < 8; ++si) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)sortLabels[si]);
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        g_pageMods[g_nMods++] = combo;

        // "Tri" plutot que "Croissant"/"Decroissant" : le glyphe (^ / v) dit
        // deja le sens, pas la peine de repeter la meme info en toutes lettres.
        HWND dirBtn = MkButton(hwnd, IDC_SORTDIR, L"^ Tri");
        g_pageMods[g_nMods++] = dirBtn;

        HWND checkAllBtn = MkButton(hwnd, IDC_CHECKALL, L"Tout verifier");
        g_pageMods[g_nMods++] = checkAllBtn;

        HWND dlAllBtn = MkButton(hwnd, IDC_DLALL, L"Tout DL");
        g_pageMods[g_nMods++] = dlAllBtn;

        // Filtre par modpack : reconstruit dynamiquement dans RefillMods()
        // (voir RefillModpackCombo) a partir des modpacks reellement
        // utilises - vide au demarrage, rempli une fois LoadData()+
        // RefillMods() executes plus bas.
        HWND packCombo = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 10, 200, hwnd, (HMENU)IDC_MODPACK, g_hInst, NULL);
        SendMessageW(packCombo, WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(packCombo, CB_ADDSTRING, 0, (LPARAM)L"Tous les mods");
        SendMessageW(packCombo, CB_SETCURSEL, 0, 0);
        g_pageMods[g_nMods++] = packCombo;

        // Filtre par tag : meme principe que le filtre modpack juste au-dessus
        // (reconstruit dynamiquement dans RefillMods() - voir RefillTagCombo),
        // mais un mod peut avoir plusieurs tags contrairement au modpack.
        HWND tagCombo = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 10, 200, hwnd, (HMENU)IDC_TAGFILTER, g_hInst, NULL);
        SendMessageW(tagCombo, WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(tagCombo, CB_ADDSTRING, 0, (LPARAM)L"Tous les tags");
        SendMessageW(tagCombo, CB_SETCURSEL, 0, 0);
        g_pageMods[g_nMods++] = tagCombo;

        HWND hideUpToDateChk = CreateWindowExW(0, L"BUTTON", L"Masquer a jour",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 10, 10, hwnd, (HMENU)IDC_HIDEUPTODATE, g_hInst, NULL);
        SendMessageW(hideUpToDateChk, WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(hideUpToDateChk, BM_SETCHECK,
            g_hideUpToDate ? BST_CHECKED : BST_UNCHECKED, 0);
        g_pageMods[g_nMods++] = hideUpToDateChk;

        HWND show10Chk = CreateWindowExW(0, L"BUTTON", L"Info 1.0",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 10, 10, hwnd, (HMENU)IDC_SHOW10, g_hInst, NULL);
        SendMessageW(show10Chk, WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(show10Chk, BM_SETCHECK,
            g_showValheim10 ? BST_CHECKED : BST_UNCHECKED, 0);
        g_pageMods[g_nMods++] = show10Chk;

        WNDCLASSEXW cwc; memset(&cwc, 0, sizeof(cwc));
        cwc.cbSize = sizeof(cwc);
        // CS_HREDRAW | CS_VREDRAW : sans ca, Windows ne reinvalide pas toute
        // la zone client au redimensionnement, et les zones liberees par des
        // enfants qui se deplacent peuvent ne jamais etre redessinees -
        // c'etait un oubli, et la cause la plus probable de "resize fait
        // disparaitre des cartes".
        cwc.style = CS_HREDRAW | CS_VREDRAW;
        cwc.lpfnWndProc = CardsHostProc;
        cwc.hInstance = g_hInst;
        cwc.hCursor = LoadCursor(NULL, IDC_ARROW);
        cwc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        cwc.lpszClassName = L"ValModsCards";
        RegisterClassExW(&cwc);
        g_hCardsHost = CreateWindowExW(WS_EX_CLIENTEDGE, L"ValModsCards", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
            0, 0, 10, 10, hwnd, NULL, g_hInst, NULL);
        g_pageMods[g_nMods++] = g_hCardsHost;

        // icone par defaut (mods sans logo choisi) : chargee une seule fois,
        // reutilisee par toutes les cartes qui n'ont pas d'iconPath valide.
        g_defaultIcon = MakeDefaultIcon(CARD_ICON_SIZE);

        // info-bulles : repond a "a quoi sert ce bouton ?" directement dans
        // l'interface. Celles des boutons de chaque carte sont ajoutees a
        // la volee dans RefillMods (elles n'existent qu'apres la creation
        // des cartes correspondantes).
        g_hTooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd, NULL, g_hInst, NULL);
        if (g_hTooltip) {
            SetWindowPos(g_hTooltip, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            AddTip(GetDlgItem(hwnd, IDC_BADD), L"Ajoute un nouveau mod a la liste");
            AddTip(searchBox, L"Recherche en direct sur le nom, la categorie, la description,\n"
                              L"la note, les tags, le modpack et le lien - se combine avec les\n"
                              L"filtres Modpack/Tag et affecte aussi Tout verifier / Tout DL");
            AddTip(searchClearBtn, L"Efface la recherche en cours");
            AddTip(chkSearchName, L"Inclut le nom du mod dans la recherche");
            AddTip(chkSearchCat, L"Inclut la categorie/auteur dans la recherche");
            AddTip(chkSearchDesc, L"Inclut la description courte dans la recherche");
            AddTip(chkSearchNote, L"Inclut la note dans la recherche");
            AddTip(chkSearchTags, L"Inclut les tags dans la recherche");
            AddTip(chkSearchModpack, L"Inclut le modpack dans la recherche");
            AddTip(chkSearchUrl, L"Inclut le lien du mod dans la recherche - decoche-la si tu ne\n"
                                 L"veux pas que des URL polluent tes resultats (si TOUTES les\n"
                                 L"cases sont decochees, la recherche porte a nouveau sur tous\n"
                                 L"les champs)");
            AddTip(combo,  L"Choisit le critere de tri de la liste");
            AddTip(dirBtn, L"Inverse l'ordre de tri (croissant / decroissant)");
            AddTip(packCombo, L"Filtre l'affichage sur un modpack donne (regroupement de mods\n"
                              L"assigne dans l'editeur) - affecte aussi Tout verifier / Tout DL");
            AddTip(tagCombo, L"Filtre l'affichage sur un tag donne (tags libres assignes dans\n"
                             L"l'editeur, separes par des virgules) - affecte aussi Tout verifier /\n"
                             L"Tout DL. Se combine avec le filtre modpack.");
            AddTip(hideUpToDateChk, L"Masque les mods deja a jour (statut Thunderstore vert) de\n"
                                   L"l'affichage ET de Tout DL - pas de Tout verifier, qui doit\n"
                                   L"justement pouvoir decouvrir un changement de statut");
            AddTip(checkAllBtn, L"Verifie tous les mods Thunderstore de la liste, "
                                L"un par un (peut prendre du temps)");
            AddTip(dlAllBtn, L"Telecharge les zips de tous les mods Thunderstore de la liste "
                             L"dans un seul dossier de ton choix");
            AddTip(show10Chk, L"Affiche/masque l'indicateur \"1.0\" sur les cartes - a decocher "
                              L"juste apres la sortie de la 1.0 si les moddeurs n'ont pas encore\n"
                              L"eu le temps de mettre a jour leurs mods");
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
        // les cases ont ete creees avant LoadData() (elles doivent exister
        // avant pour que RefillMods/BuildDetailsLine, qui suivent, s'appuient
        // sur le bon etat) - on resynchronise donc leur affichage avec la
        // valeur effectivement chargee, sans quoi elles resteraient toujours
        // a leur etat par defaut meme si le fichier disait le contraire.
        SendMessageW(GetDlgItem(hwnd, IDC_SHOW10), BM_SETCHECK,
            g_showValheim10 ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(GetDlgItem(hwnd, IDC_HIDEUPTODATE), BM_SETCHECK,
            g_hideUpToDate ? BST_CHECKED : BST_UNCHECKED, 0);
        RefillMods();
        ShowPage(0);
        UpdateTitle();
        return 0;
    }

    case WM_SIZE:
        LayoutAll(hwnd);
        return 0;

    // CS_HREDRAW|CS_VREDRAW se charge du repaint pendant le glisser
    // (peu couteux, gere par le systeme) ; on ajoute un repaint complet et
    // explicite une fois le redimensionnement termine, en filet de securite
    // pour les controles qui echapperaient encore a l'invalidation normale
    // (ex: le menu de tri).
    case WM_EXITSIZEMOVE:
        RedrawEverything();
        break;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        // 900 : marge confortable pour les 9 boutons de chaque carte de mod.
        mm->ptMinTrackSize.x = 900;
        mm->ptMinTrackSize.y = 480;
        return 0;
    }

    case WM_COMMAND: {
        int cid = LOWORD(wp);

        // boutons dynamiques d'une carte de mod (voir RA_BASE/RA_COUNT) :
        // id = RA_BASE + position_visible * RA_COUNT + action. Un filtre
        // (modpack, masquage des mods a jour) peut avoir saute des mods :
        // rowIdx est une position parmi les cartes AFFICHEES, pas un index
        // direct dans g_mods - on traduit via g_visibleIndices avant tout
        // usage, pour que tout le code en aval (Action*, menu "...") reste
        // simple et manipule directement le bon index reel.
        if (cid >= RA_BASE) {
            int raw = cid - RA_BASE;
            int action = raw % RA_COUNT;
            int rowIdx = raw / RA_COUNT;
            int modIdx = (rowIdx >= 0 && rowIdx < (int)g_visibleIndices.size())
                ? g_visibleIndices[rowIdx] : -1;
            if (modIdx < 0) return 0;   // liste filtree/modifiee entre le clic et le traitement
            if (action == RA_MORE) ShowRowOverflowMenu(hwnd, modIdx);
            else PostMessageW(hwnd, WM_APP_ROWACTION, (WPARAM)action, (LPARAM)modIdx);
            return 0;
        }
        if (cid == IDC_SORTCOMBO && HIWORD(wp) == CBN_SELCHANGE) {
            int sel = (int)SendMessageW(GetDlgItem(hwnd, IDC_SORTCOMBO), CB_GETCURSEL, 0, 0);
            static const int SORT_MAP[] = { COL_NAME, COL_CAT, COL_LASTCHECK, COL_DLL,
                                            COL_TSVER, COL_SOURCE, COL_URL, COL_NOTE };
            if (sel >= 0 && sel < 8) { g_sortCol = SORT_MAP[sel]; RefillMods(); }
            return 0;
        }
        if (cid == IDC_MODPACK && HIWORD(wp) == CBN_SELCHANGE) {
            HWND pc = GetDlgItem(hwnd, IDC_MODPACK);
            int sel = (int)SendMessageW(pc, CB_GETCURSEL, 0, 0);
            if (sel <= 0) {
                g_modpackFilter.clear();   // "Tous les mods" est toujours l'entree 0
            } else {
                wchar_t buf[256] = L"";
                SendMessageW(pc, CB_GETLBTEXT, sel, (LPARAM)buf);
                g_modpackFilter = buf;
            }
            SaveMods();
            RefillMods();
            return 0;
        }
        if (cid == IDC_TAGFILTER && HIWORD(wp) == CBN_SELCHANGE) {
            HWND tc = GetDlgItem(hwnd, IDC_TAGFILTER);
            int sel = (int)SendMessageW(tc, CB_GETCURSEL, 0, 0);
            if (sel <= 0) {
                g_tagFilter.clear();   // "Tous les tags" est toujours l'entree 0
            } else {
                wchar_t buf[256] = L"";
                SendMessageW(tc, CB_GETLBTEXT, sel, (LPARAM)buf);
                g_tagFilter = buf;
            }
            SaveMods();
            RefillMods();
            return 0;
        }
        // Recherche mise a jour a chaque frappe - PAS de SaveMods() ici,
        // volontairement non persistee (voir g_searchQuery).
        if (cid == IDC_SEARCHBOX && HIWORD(wp) == EN_CHANGE) {
            g_searchQuery = GetTextOf(GetDlgItem(hwnd, IDC_SEARCHBOX));
            RefillMods();
            return 0;
        }

        switch (cid) {
        case IDC_BADD: ActionAdd(hwnd); return 0;
        case IDC_SEARCHCLEAR:
            SetWindowTextW(GetDlgItem(hwnd, IDC_SEARCHBOX), L"");
            g_searchQuery.clear();
            RefillMods();
            return 0;
        case IDC_CHECKALL: ActionCheckAll(hwnd); return 0;
        case IDC_DLALL: ActionDownloadAll(hwnd); return 0;
        case IDC_SHOW10:
            g_showValheim10 = (SendMessageW(GetDlgItem(hwnd, IDC_SHOW10), BM_GETCHECK, 0, 0) == BST_CHECKED);
            SaveMods();
            RefillMods();
            return 0;
        case IDC_HIDEUPTODATE:
            g_hideUpToDate = (SendMessageW(GetDlgItem(hwnd, IDC_HIDEUPTODATE), BM_GETCHECK, 0, 0) == BST_CHECKED);
            SaveMods();
            RefillMods();
            return 0;
        // Cases "sur quoi chercher" (voir g_searchIn*/ModMatchesSearch) - le
        // meme geste (lire l'etat coche, persister, rafraichir) pour chacune.
        case IDC_SEARCH_NAME:
            g_searchInName = (SendMessageW(GetDlgItem(hwnd, IDC_SEARCH_NAME), BM_GETCHECK, 0, 0) == BST_CHECKED);
            SaveMods(); RefillMods(); return 0;
        case IDC_SEARCH_CAT:
            g_searchInCat = (SendMessageW(GetDlgItem(hwnd, IDC_SEARCH_CAT), BM_GETCHECK, 0, 0) == BST_CHECKED);
            SaveMods(); RefillMods(); return 0;
        case IDC_SEARCH_DESC:
            g_searchInDesc = (SendMessageW(GetDlgItem(hwnd, IDC_SEARCH_DESC), BM_GETCHECK, 0, 0) == BST_CHECKED);
            SaveMods(); RefillMods(); return 0;
        case IDC_SEARCH_NOTE:
            g_searchInNote = (SendMessageW(GetDlgItem(hwnd, IDC_SEARCH_NOTE), BM_GETCHECK, 0, 0) == BST_CHECKED);
            SaveMods(); RefillMods(); return 0;
        case IDC_SEARCH_TAGS:
            g_searchInTags = (SendMessageW(GetDlgItem(hwnd, IDC_SEARCH_TAGS), BM_GETCHECK, 0, 0) == BST_CHECKED);
            SaveMods(); RefillMods(); return 0;
        case IDC_SEARCH_MODPACK:
            g_searchInModpack = (SendMessageW(GetDlgItem(hwnd, IDC_SEARCH_MODPACK), BM_GETCHECK, 0, 0) == BST_CHECKED);
            SaveMods(); RefillMods(); return 0;
        case IDC_SEARCH_URL:
            g_searchInUrl = (SendMessageW(GetDlgItem(hwnd, IDC_SEARCH_URL), BM_GETCHECK, 0, 0) == BST_CHECKED);
            SaveMods(); RefillMods(); return 0;
        case IDC_SORTDIR:
            g_sortAsc = !g_sortAsc;
            SetWindowTextW(GetDlgItem(hwnd, IDC_SORTDIR), g_sortAsc ? L"^ Tri" : L"v Tri");
            RefillMods();
            return 0;

        // menu "..." d'une carte (actions moins frequentes) - egalement
        // POSTEES : le menu a ete ouvert depuis un bouton de carte, on est
        // donc toujours dans la meme pile d'appels imbriquee que ci-dessus.
        case IDM_CTX_LOCATE_DLL:  PostMessageW(hwnd, WM_APP_ROWACTION, RA_MENU_LOCATE_DLL, g_ctxMenuModIndex);  return 0;
        case IDM_CTX_OPEN_DLLDIR: PostMessageW(hwnd, WM_APP_ROWACTION, RA_MENU_OPEN_DLLDIR, g_ctxMenuModIndex); return 0;
        case IDM_CTX_LOCATE_CONFIG: PostMessageW(hwnd, WM_APP_ROWACTION, RA_MENU_LOCATE_CONFIG, g_ctxMenuModIndex); return 0;
        case IDM_CTX_OPEN_CONFIG:   PostMessageW(hwnd, WM_APP_ROWACTION, RA_MENU_OPEN_CONFIG, g_ctxMenuModIndex);   return 0;
        case IDM_CTX_COPY:        PostMessageW(hwnd, WM_APP_ROWACTION, RA_MENU_COPY, g_ctxMenuModIndex);        return 0;
        case IDM_CTX_DELETE:      PostMessageW(hwnd, WM_APP_ROWACTION, RA_MENU_DELETE, g_ctxMenuModIndex);      return 0;

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
        case IDM_DOWNLOADS: MakeDirs(DownloadsRoot());
                            OpenFolder(hwnd, DownloadsRoot(), L"telechargements"); return 0;
        case IDM_OPENDATA: OpenFolder(hwnd, ExeDir(),     L"donnees");          return 0;
        case IDM_SETDIR:   ChooseValheimDir(hwnd); UpdateTitle(); return 0;
        case IDM_NEXUSKEY: ChooseNexusApiKey(hwnd); return 0;
        case IDM_FIXLIST:  ActionFixList(hwnd); return 0;
        case IDM_EXIT:     DestroyWindow(hwnd); return 0;
        case IDM_ABOUT: {
            std::wstring about =
                L"ValMods " + U2W(VALMODS_VERSION) + L" - gestionnaire manuel de mods Valheim\n\n";
            about +=
                L"Chaque mod est une carte avec ses propres boutons (survole-les pour\n"
                L"voir leur nom, ils sont a base de symboles plutot que de texte) :\n"
                L"\u2197  Watch  : ouvre la page du mod\n"
                L"\u2261  Hist.  : ouvre la page des changements / versions\n"
                L"\u21BB  Check+ : ouvre la page du mod ET note la date de verification\n"
                L"\u2713  OK     : note la date de verification SANS ouvrir de lien\n"
                L"           (utile si tu as deja verifie ailleurs : Discord du\n"
                L"           mod, changelog deja ouvert dans un autre onglet...)\n"
                L"\u26A1  TS     : verifie la derniere version (Thunderstore ou Nexus)\n"
                L"\u2193  DL     : telecharge le zip (Thunderstore uniquement)\n"
                L"\u270E  Modif. : modifie le mod\n"
                L"\u2699  Config  : ouvre le fichier de config avec le programme\n"
                L"           associe par Windows (grise si aucun n'est defini)\n"
                L"...       : copier le lien, localiser le DLL/la config,\n"
                L"           ouvrir le dossier du mod ou la config, supprimer\n\n"
                L"Le menu deroulant en haut choisit le critere de tri (dont \"Source\" :\n"
                L"Thunderstore / Nexus / Aucune), le bouton a cote inverse l'ordre.\n\n"
                L"Recherche (en haut de la liste) filtre en direct sur les champs\n"
                L"coches juste en dessous (nom, categorie, description, note, tags,\n"
                L"modpack, lien - decoche ceux qui ne t'interessent pas). Si TOUTES les\n"
                L"cases sont decochees, la recherche porte a nouveau sur tous les\n"
                L"champs. Le texte tape n'est pas enregistre d'un lancement a l'autre,\n"
                L"contrairement au choix des champs et aux filtres ci-dessous.\n\n"
                L"Filtre Modpack et filtre Tag (menus deroulants) se combinent : seuls\n"
                L"les mods qui correspondent a TOUS les filtres actifs (recherche\n"
                L"comprise) sont affiches, et affectent aussi Tout verifier / Tout DL.\n"
                L"Les tags sont du texte libre separe par des virgules (champ \"Tags\"\n"
                L"dans l'editeur) - un mod peut en avoir plusieurs, contrairement au\n"
                L"modpack qui est unique.\n\n"
                L"La ligne de details (categorie / verif / DLL / TS / 1.0) est coloree :\n"
                L"rouge = probleme (DLL manquant ou mise a jour disponible),\n"
                L"orange = verification ancienne (14+ jours), vert = tout va bien,\n"
                L"gris = jamais verifie.\n\n"
                L"\"1.0\" indique si la derniere version publiee du mod date d'apres la\n"
                L"sortie de Valheim 1.0 (Deep North, 9 septembre 2026) : \"inconnu\" tant\n"
                L"que la version n'a jamais ete verifiee, \"pas encore sortie\" tant que\n"
                L"le 9 septembre n'est pas arrive (rien d'anormal), \"a verifier\" une\n"
                L"fois la 1.0 sortie si ce mod n'a pas ete mis a jour depuis.\n\n"
                L"Chaque mod a une source de verification (voir Modifier) : Thunderstore\n"
                L"(API publique, aucune cle requise), Nexus Mods (necessite une cle API\n"
                L"personnelle - menu Parametres > \"Definir la cle API Nexus...\"), ou\n"
                L"Aucune (desactive TS/DL pour ce mod). Le telechargement direct (DL)\n"
                L"ne fonctionne que pour Thunderstore - l'API de telechargement de\n"
                L"Nexus est reservee aux comptes Premium.\n\n"
                L"Dans l'editeur, Auto-remplir detecte automatiquement Thunderstore ou\n"
                L"Nexus d'apres le lien colle, et recupere nom, description, derniere\n"
                L"version et icone. Verif. TS complete aussi la description si elle est vide.\n\n"
                L"Telecharger (DL) propose une boite Enregistrer sous - a extraire\n"
                L"toi-meme dans BepInEx\\plugins, rien n'est installe automatiquement.\n\n"
                L"Dans l'editeur, indique le DOSSIER dans lequel le mod est installe\n"
                L"(pas le fichier .dll lui-meme) : le DLL a l'interieur est retrouve\n"
                L"automatiquement (jusqu'a 2 sous-dossiers de profondeur), ce qui\n"
                L"evite d'avoir a re-pointer vers un .dll qui change de nom d'une\n"
                L"version a l'autre. Un fichier de config (optionnel, ex: le .cfg\n"
                L"BepInEx du mod) peut aussi etre associe pour le localiser/l'ouvrir\n"
                L"rapidement depuis le menu \"...\".\n\n"
                L"Les icones telechargees ou choisies via Parcourir sont copiees dans\n"
                L"icons\\ a cote de l'exe et enregistrees en chemin RELATIF : tu peux\n"
                L"envoyer valmods.json + valmods.exe + icons\\ a un ami sans que les\n"
                L"icones cassent (n'oublie pas de vider ta cle API Nexus avant, elle\n"
                L"est personnelle).\n\n"
                L"Menu Outils > Completer les infos manquantes : repasse l'auto-\n"
                L"remplissage (Thunderstore/Nexus) sur tous les mods eligibles, mais\n"
                L"SANS RIEN ECRASER - seuls les champs actuellement vides (categorie,\n"
                L"lien historique, description, icone) sont completes ; une valeur\n"
                L"deja saisie ou corrigee a la main reste intouchee. La version\n"
                L"installee (installedVersion) n'est elle non plus jamais modifiee -\n"
                L"seul Telecharger/Tout DL ou une saisie manuelle y touchent.\n\n"
                L"Un valmods.json plus ancien ou d'une autre origine est repris tel\n"
                L"quel a l'ouverture (structure et noms de champ tolerants) - une copie\n"
                L"de l'original est gardee a cote (valmods.json.premigration) si le\n"
                L"fichier semblait vraiment different du format actuel.\n\n"
                L"Donnees : valmods.json, a cote de l'exe.";
            Info(hwnd, about.c_str());
            return 0;
        }
        }
        break;
    }

    // Execution reelle, differee, des actions de carte (voir le commentaire
    // pres de WM_APP_ROWACTION) : on est ici sur une iteration de boucle de
    // messages fraiche, plus aucun bouton n'est en train de se traiter -
    // RefillMods() peut detruire/recreer les cartes sans risque.
    case WM_APP_ROWACTION: {
        int action = (int)wp;
        int idx = (int)lp;
        switch (action) {
            case RA_WATCH: ActionOpen(hwnd, idx, false);        break;
            case RA_HIST:  ActionOpenHistory(hwnd, idx);        break;
            case RA_CHECK: ActionOpen(hwnd, idx, true);         break;
            case RA_MARK:  ActionMark(hwnd, idx);               break;
            case RA_TS:    ActionCheckThunderstore(hwnd, idx);  break;
            case RA_DL:    ActionDownloadLatest(hwnd, idx);     break;
            case RA_EDIT:  ActionEdit(hwnd, idx);               break;
            case RA_CONFIG: ActionOpenConfig(hwnd, idx);        break;
            case RA_MENU_COPY:        ActionCopy(hwnd, idx);        break;
            case RA_MENU_LOCATE_DLL:  ActionLocateDll(hwnd, idx);   break;
            case RA_MENU_OPEN_DLLDIR: ActionOpenDllDir(hwnd, idx);  break;
            case RA_MENU_LOCATE_CONFIG: ActionLocateConfig(hwnd, idx); break;
            case RA_MENU_OPEN_CONFIG:   ActionOpenConfig(hwnd, idx);   break;
            case RA_MENU_DELETE:      ActionDelete(hwnd, idx);      break;
        }
        return 0;
    }

    case WM_NOTIFY: {
        NMHDR* nh = (NMHDR*)lp;
        if (nh->hwndFrom == g_hTab && nh->code == TCN_SELCHANGE) {
            ShowPage(TabCtrl_GetCurSel(g_hTab));
            return 0;
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
    // GetOrLoadHIcon/MakeDefaultIcon renverront simplement NULL, donc pas
    // d'icone affichee plutot qu'un crash.

    NONCLIENTMETRICSW ncm;
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    if (!g_font) g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    // variante grasse du meme corps de police, pour le nom du mod sur chaque
    // carte (repli sur g_font si la creation echoue, tres improbable).
    LOGFONTW lfBold; memset(&lfBold, 0, sizeof(lfBold));
    if (GetObjectW(g_font, sizeof(lfBold), &lfBold)) {
        lfBold.lfWeight = FW_BOLD;
        g_fontBold = CreateFontIndirectW(&lfBold);
    }
    if (!g_fontBold) g_fontBold = g_font;

    WNDCLASSEXW wc; memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    // meme oubli corrige que pour ValModsCards : sans CS_HREDRAW|CS_VREDRAW,
    // Windows ne reinvalide pas toute la zone client au redimensionnement,
    // ce qui peut laisser des enfants directs (barre Ajouter/tri) ne pas se
    // redessiner correctement apres coup.
    wc.style = CS_HREDRAW | CS_VREDRAW;
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

    if (!g_iconCache.empty()) {
        for (std::map<std::wstring, HICON>::iterator it = g_iconCache.begin();
             it != g_iconCache.end(); ++it)
            DestroyIcon(it->second);
        g_iconCache.clear();
    }
    if (g_defaultIcon) { DestroyIcon(g_defaultIcon); g_defaultIcon = NULL; }
    if (g_fontBold && g_fontBold != g_font) { DeleteObject(g_fontBold); g_fontBold = NULL; }
    if (gdiStatus == Gdiplus::Ok) Gdiplus::GdiplusShutdown(g_gdiplusToken);
    OleUninitialize();
    return (int)msg.wParam;
}
