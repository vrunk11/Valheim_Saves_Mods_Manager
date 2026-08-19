# ValMods

Petit gestionnaire **manuel** de mods Valheim, en C++ / Win32 natif.
Pas de scan automatique, pas de téléchargement : tu gardes le contrôle,
l'appli se contente de retenir tes liens et la date où tu les as vérifiés.

- Un seul `.exe`, aucune dépendance, ~150 Ko.
- Liste de mods : nom, catégorie, lien, dernière vérification, note.
  - **Watch** : ouvre le lien
  - **Check update** : ouvre le lien + horodate la vérification
  - **Vérifié** : horodate sans ouvrir
  - Rouge si non vérifié depuis 30+ jours, orange 14+, gris si jamais vérifié
- Accès rapide aux dossiers `BepInEx/plugins`, `BepInEx/config`, dossier du jeu,
  dossier des sauvegardes.
- Onglet Sauvegardes : liste des mondes et personnages (taille, date), backup
  en un clic vers `backups/`.
- Détection auto du dossier Valheim (registre Steam + bibliothèques multiples),
  modifiable dans Paramètres si besoin.

Tout est stocké dans `valmods.json`, créé à côté de l'exe au premier lancement.

## Build

Prérequis : CMake ≥ 3.15 et MinGW-w64 (ou Visual Studio Build Tools).
Aucune dépendance externe, vcpkg n'est pas nécessaire.

```bash
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

L'exécutable sort dans `build/valmods.exe`.

Avec Visual Studio, `cmake -B build` (sans `-G`) suffit, puis ouvrir la
solution générée ou lancer `cmake --build build --config Release`.

## Structure

```
valmods.cpp              # tout le code de l'appli
minijson.h                # mini parseur/écrivain JSON header-only, sans dépendance
CMakeLists.txt             # build
valmods.rc / .manifest       # icône des contrôles modernes + DPI-aware (MinGW)
valmods.json.example          # modèle de données pour un premier lancement
.github/workflows/ci.yml       # build sur chaque push/PR + release sur tag vX.Y.Z
```

## CI / Releases

Chaque push et pull request déclenche une compilation Release sur
`windows-latest` (MSVC, générateur par défaut de CMake — pas de setup de
toolchain nécessaire) et un test de fumée (`valmods.exe --version` doit
sortir en code 0 sans ouvrir de fenêtre). Le binaire est publié comme
artefact de workflow, téléchargeable depuis l'onglet Actions.

Pour publier une release :

```bash
git tag v1.1.0
git push origin v1.1.0
```

Un tag `vX.Y.Z` déclenche un second job qui récupère ce même binaire, le
zippe avec `valmods.json.example` et le `README.md`, et publie le tout comme
release GitHub.

## Format de données (`valmods.json`)

```json
{
  "version": 1,
  "valheimDir": "D:\\SteamLibrary\\steamapps\\common\\Valheim",
  "mods": [
    {
      "name": "ValheimPlus",
      "category": "QoL",
      "url": "https://thunderstore.io/...",
      "lastCheck": "2026-08-19 14:30",
      "note": "v0.9.9.16"
    }
  ]
}
```

Fichier texte, éditable à la main. `valmods.json` n'est volontairement pas
versionné (voir `.gitignore`) : c'est ta liste personnelle, pas un fichier
du projet.
