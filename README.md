# ValMods

Petit gestionnaire **manuel** de mods Valheim, en C++ / Win32 natif.
Rien ne s'installe ni ne s'extrait tout seul dans `BepInEx/plugins` : tu
gardes le contrôle. L'appli sait vérifier les versions et télécharger les
zips sur Thunderstore, mais chaque action reste déclenchée par un clic
explicite — pas de scan automatique de tes dossiers, pas d'opération en
tâche de fond.

- Un seul `.exe`, aucune dépendance, ~200 Ko (GDI+ et les boîtes de dialogue
  de fichier sont livrées avec Windows).
- **Vue en cartes**, pas un tableau : icône bien visible (40×40), nom en
  gras, une description courte, une ligne de petits détails (catégorie ·
  dernière vérif · état DLL · état Thunderstore), une ligne de note, et les
  boutons d'action directement sur chaque carte (à glyphe plutôt qu'à texte,
  survole-les pour voir leur nom) :
  - **↗ Watch** : ouvre le lien du mod
  - **≡ Hist.** : ouvre la page des changements / versions (champ séparé)
  - **↻ Check+** : ouvre le lien + horodate la vérification
  - **✓ OK** : horodate SANS ouvrir de lien — utile si tu as déjà vérifié
    ailleurs (Discord du mod, changelog déjà ouvert dans un autre onglet…)
  - **⚡ TS** : interroge Thunderstore pour la dernière version publiée
  - **↓ DL** : télécharge le zip (voir plus bas)
  - **✎ Modif.** : modifie le mod
  - **... Plus** : copier le lien, localiser le DLL, ouvrir son dossier,
    supprimer
  - Tri via un menu déroulant + un bouton croissant/décroissant (nom,
    catégorie, dernière vérif, DLL, MAJ Thunderstore, lien, note)
  - La ligne de détails est colorée : rouge si un problème concret existe
    (DLL manquant ou mise à jour disponible), orange si la dernière
    vérification date de 14+ jours, vert si tout va bien, gris si jamais
    vérifié
  - **Indicateur "1.0"** dans la ligne de détails : indique si la dernière
    version publiée du mod (date Thunderstore) est postérieure à la sortie
    de Valheim 1.0 (Deep North, 9 septembre 2026). Quatre états : inconnu
    (jamais vérifié), pas encore sortie (rien d'anormal tant que le 9
    septembre n'est pas arrivé), à jour, à vérifier (la 1.0 est sortie et ce
    mod n'a pas bougé depuis). **Case "Info 1.0"** (barre du haut) pour
    afficher/masquer cet indicateur — pratique à décocher juste après la
    sortie de la 1.0, le temps que les moddeurs mettent à jour leurs mods
    (ils ne vont pas tous switcher instantanément), puis à recocher plus
    tard.
  - **Modpacks** : chaque mod peut être rattaché à un "modpack" (nom libre,
    tapé ou choisi dans l'éditeur — utile pour regrouper les mods d'un même
    playthrough ou serveur). Le **menu déroulant de filtre** (barre du haut)
    limite l'affichage à un modpack donné, et affecte aussi **Tout
    vérifier** / **Tout DL** (qui n'opèrent alors que sur ce sous-ensemble).
  - **Masquer à jour** (case à cocher, barre du haut) : cache les mods déjà
    à jour (statut Thunderstore vert) de l'affichage et de **Tout DL** —
    pratique pour ne télécharger que ce qui manque dans le modpack courant.
    N'affecte **pas** "Tout vérifier", qui doit justement pouvoir découvrir
    un changement de statut.
  - **Suivi de version installée** : chaque mod a une `installedVersion`
    (visible/éditable dans l'éditeur) que ValMods suit lui-même, distincte
    de la version *embarquée* dans le fichier DLL — beaucoup de DLL de mods
    Unity/BepInEx n'ont pas de ressource de version fiable, donc s'appuyer
    dessus donnait de faux "jamais vérifié". **Télécharger**/**Tout DL**
    mettent à jour ce suivi automatiquement (ils supposent que le zip va
    être extrait) ; à corriger à la main si ce n'est pas le cas, ou si
    l'installation se fait autrement. C'est ce champ que **Tout vérifier**
    et le statut affiché sur la carte utilisent en priorité.
  - Icône par mod : PNG/JPG/BMP/ICO/GIF, choisie via un sélecteur de fichier
    dans l'éditeur (aperçu affiché), ou récupérée automatiquement (voir
    Auto-remplir ci-dessous)
  - **Auto-remplir depuis Thunderstore** (dans l'éditeur) : à partir du lien
    du mod, récupère nom, catégorie, lien historique, description, dernière
    version et icône (téléchargée dans `icons/` à côté de l'exe). Ne
    remplace jamais le chemin du DLL installé, qui reste propre à ta
    machine.
  - **TS** interroge l'API publique de Thunderstore (aucune clé requise) pour
    connaître la dernière version publiée d'un mod, et la compare à la
    version installée suivie (voir plus haut), en complétant la description
    si elle est encore vide. Ne fonctionne que pour les mods dont le lien
    pointe vers `thunderstore.io` (Nexus/GitHub n'ont pas
    d'équivalent aussi simple sans clé d'API).
  - **Tout vérifier** (barre du haut) : lance **TS** sur tous les mods
    éligibles de la liste, l'un après l'autre (pas en parallèle — l'appli
    reste bloquée le temps de l'opération). Une confirmation préalable
    indique combien de mods seront vérifiés avant de lancer, et un résumé
    final récapitule mises à jour disponibles / à jour / erreurs / ignorés.
    **Ne touche jamais** à la date de vérification individuelle de chaque
    mod (réservée à une vérification manuelle, personnelle — c'est elle qui
    alimente le code couleur rouge/orange/vert/gris) : seule une date de
    vérification globale, séparée (`lastGlobalCheck`), est mise à jour.
    Peut prendre du temps sur une longue liste.
  - **Non Thunderstore** (case à cocher dans l'éditeur) : désactive les
    boutons **TS**/**DL** sur la carte de ce mod et l'exclut de
    **Tout vérifier** / **Tout DL** — pratique pour un mod Nexus/GitHub dont
    le lien ne permettrait pas de le deviner automatiquement.
  - **Tout DL** (barre du haut) : télécharge le zip de la dernière version
    de tous les mods éligibles dans **un seul dossier** choisi une fois
    (au lieu d'une boîte "Enregistrer sous..." par mod). Si la version d'un
    mod n'est pas déjà connue, elle est vérifiée d'abord automatiquement.
    Même logique que **Tout vérifier** : ne touche pas aux dates
    individuelles.
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
  "version": 10,
  "valheimDir": "D:\\SteamLibrary\\steamapps\\common\\Valheim",
  "lastGlobalCheck": "2026-08-19 14:30",
  "showValheim10Check": true,
  "modpackFilter": "",
  "hideUpToDate": false,
  "mods": [
    {
      "name": "ValheimPlus",
      "category": "QoL",
      "url": "https://thunderstore.io/...",
      "changelogUrl": "https://thunderstore.io/.../changelog/",
      "dllPath": "D:\\...\\BepInEx\\plugins\\ValheimPlus.dll",
      "iconPath": "C:\\Users\\...\\Pictures\\valheimplus.png",
      "tsVersion": "0.9.9.16",
      "tsLatestDate": "2026-03-26T21:26:57Z",
      "installedVersion": "0.9.9.16",
      "description": "QoL and building overhaul for Valheim.",
      "modpack": "Serveur du vendredi",
      "nonThunderstore": false,
      "lastCheck": "2026-08-19 14:30",
      "note": "v0.9.9.16"
    }
  ]
}
```

`lastGlobalCheck` est la date du dernier "Tout vérifier" — distincte de
`lastCheck` propre à chaque mod (une vérification manuelle, individuelle).
Le bulk check ne touche jamais aux dates individuelles, volontairement :
elles alimentent le code couleur (rouge/orange/vert/gris) qui répond à
"quels mods je n'ai pas regardés moi-même depuis longtemps" — un "Tout
vérifier" automatisé ne doit pas réinitialiser ce signal.

`showValheim10Check` contrôle l'affichage de l'indicateur "1.0" (voir plus
haut) ; réglé via la case à cocher "Info 1.0" de la barre du haut.

`modpack` (par mod) regroupe des mods pour un même playthrough/serveur —
tapé librement ou choisi dans l'éditeur. `modpackFilter` (racine) est le
modpack actuellement sélectionné dans le menu déroulant de filtre de la
barre du haut ("" = tous les mods). `hideUpToDate` masque les mods déjà à
jour de l'affichage et de "Tout DL" (mais pas de "Tout vérifier", qui doit
justement pouvoir découvrir un changement de statut).

`installedVersion` est la version que ValMods **suit lui-même** comme étant
installée — mise à jour automatiquement par **Télécharger**/**Tout DL** (qui
supposent que le zip va être extrait), et éditable à la main dans l'éditeur.
C'est ce champ, prioritaire, qui sert de base à toutes les comparaisons
("à jour"/"mise à jour disponible", et "Tout vérifier") : la version
embarquée dans le fichier DLL (`dllPath`) n'est utilisée qu'en repli, quand
`installedVersion` est vide, car beaucoup de DLL de mods Unity/BepInEx n'ont
pas de ressource de version fiable.

Un fichier `valmods.json` plus ancien (sans `changelogUrl`/`dllPath`/
`iconPath`/`tsVersion`/`tsLatestDate`/`installedVersion`/`description`/
`modpack`/`nonThunderstore`/`lastGlobalCheck`/`showValheim10Check`/
`modpackFilter`/`hideUpToDate`) reste lisible tel
quel : les champs manquants sont simplement traités comme vides / `true` /
`false` selon le champ.

Fichier texte, éditable à la main. `valmods.json` n'est volontairement pas
versionné (voir `.gitignore`) : c'est ta liste personnelle, pas un fichier
du projet.
