# ValMods

Petit gestionnaire **manuel** de mods Valheim, en C++ / Win32 natif.
Pas de scan automatique, pas de téléchargement : tu gardes le contrôle,
l'appli se contente de retenir tes liens et la date où tu les as vérifiés.

- Un seul `.exe`, aucune dépendance, ~200 Ko (GDI+ et les boîtes de dialogue
  de fichier sont livrées avec Windows).
- **Vue en cartes**, pas un tableau : icône bien visible (40×40), nom en
  gras, une ligne de petits détails (catégorie · dernière vérif · état DLL ·
  état Thunderstore), une ligne de note, et les boutons d'action directement
  sur chaque carte :
  - **Watch** : ouvre le lien du mod
  - **Hist.** : ouvre la page des changements / versions (champ séparé)
  - **Check+** : ouvre le lien + horodate la vérification
  - **OK** : horodate SANS ouvrir de lien — utile si tu as déjà vérifié
    ailleurs (Discord du mod, changelog déjà ouvert dans un autre onglet…)
  - **TS** : interroge Thunderstore pour la dernière version publiée
  - **DL** : télécharge le zip (voir plus bas)
  - **Modif.** : modifie le mod
  - **...** : copier le lien, localiser le DLL, ouvrir son dossier,
    supprimer
  - Tri via un menu déroulant + un bouton croissant/décroissant (nom,
    catégorie, dernière vérif, DLL, MAJ Thunderstore, lien, note)
  - La ligne de détails est colorée : rouge si un problème concret existe
    (DLL manquant ou mise à jour disponible), orange si la dernière
    vérification date de 14+ jours, vert si tout va bien, gris si jamais
    vérifié
  - Icône par mod : PNG/JPG/BMP/ICO/GIF, choisie via un sélecteur de fichier
    dans l'éditeur (aperçu affiché), ou récupérée automatiquement (voir
    Auto-remplir ci-dessous)
  - **Auto-remplir depuis Thunderstore** (dans l'éditeur) : à partir du lien
    du mod, récupère nom, catégorie, lien historique, dernière version et
    icône (téléchargée dans `icons/` à côté de l'exe). Ne remplace jamais le
    chemin du DLL installé, qui reste propre à ta machine.
  - **TS** interroge l'API publique de Thunderstore (aucune clé requise) pour
    connaître la dernière version publiée d'un mod, et la compare à la
    version lue dans le DLL installé. Ne fonctionne que pour les mods dont
    le lien pointe vers `thunderstore.io` (Nexus/GitHub n'ont pas
    d'équivalent aussi simple sans clé d'API). L'appel réseau est synchrone
    et déclenché mod par mod sur clic explicite — pas de vérification
    automatique en masse.
  - **DL** récupère le zip de la dernière version (via l'URL de
    téléchargement officielle de Thunderstore) et ouvre une boîte
    "Enregistrer sous..." pour choisir où le sauvegarder (suggestion par
    défaut dans `downloads/`, entièrement modifiable). Ne l'extrait
    **jamais** automatiquement — ValMods reste manuel, c'est à toi de le
    déposer dans `BepInEx/plugins`.
- Accès rapide aux dossiers `BepInEx/plugins`, `BepInEx/config`, dossier du
  jeu, dossier du DLL d'un mod donné.
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
  "version": 3,
  "valheimDir": "D:\\SteamLibrary\\steamapps\\common\\Valheim",
  "mods": [
    {
      "name": "ValheimPlus",
      "category": "QoL",
      "url": "https://thunderstore.io/...",
      "changelogUrl": "https://thunderstore.io/.../changelog/",
      "dllPath": "D:\\...\\BepInEx\\plugins\\ValheimPlus.dll",
      "iconPath": "C:\\Users\\...\\Pictures\\valheimplus.png",
      "tsVersion": "0.9.9.16",
      "lastCheck": "2026-08-19 14:30",
      "note": "v0.9.9.16"
    }
  ]
}
```

Un fichier `valmods.json` plus ancien (sans `changelogUrl`/`dllPath`/
`iconPath`/`tsVersion`) reste lisible tel quel : les champs manquants sont
simplement traités comme vides.

Fichier texte, éditable à la main. `valmods.json` n'est volontairement pas
versionné (voir `.gitignore`) : c'est ta liste personnelle, pas un fichier
du projet.
