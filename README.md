# ValMods

Petit gestionnaire **manuel** de mods Valheim, en C++ / Win32 natif.
Rien ne s'installe ni ne s'extrait tout seul dans `BepInEx/plugins` : tu
gardes le contrôle. L'appli sait vérifier les versions (Thunderstore, Nexus
Mods ou Hexium) et télécharger les zips sur Thunderstore ou Hexium, mais
chaque action reste déclenchée par un clic explicite — pas de scan
automatique de tes dossiers, pas d'opération en tâche de fond.

- Un seul `.exe`, aucune dépendance, ~200 Ko (GDI+ et les boîtes de dialogue
  de fichier sont livrées avec Windows).
- **Vue en cartes**, pas un tableau : icône bien visible (64×64), nom en
  gras, une description courte, une ligne de petits détails (catégorie ·
  dernière vérif · état DLL · état Thunderstore), une ligne de note, et les
  boutons d'action directement sur chaque carte (à glyphe plutôt qu'à texte,
  survole-les pour voir leur nom) :
  - **↗ Watch** : ouvre le lien du mod
  - **≡ Hist.** : ouvre la page des changements / versions (champ séparé)
  - **↻ Check+** : ouvre le lien + horodate la vérification
  - **✓ OK** : horodate SANS ouvrir de lien — utile si tu as déjà vérifié
    ailleurs (Discord du mod, changelog déjà ouvert dans un autre onglet…)
  - **⚡ TS** : interroge Thunderstore, Nexus ou Hexium pour la dernière
    version publiée, selon la source réglée pour ce mod
  - **↓ DL** : télécharge le zip (voir plus bas)
  - **✎ Modif.** : modifie le mod
  - **⚙ Config** : ouvre le fichier de config avec le programme associé par
    Windows (grisé si aucun fichier de config n'est renseigné pour ce mod)
  - **... Plus** : copier le lien, localiser le DLL ou le fichier de
    config, ouvrir le dossier du mod ou la config, supprimer
  - Tri via un menu déroulant + un bouton **Tri** (le glyphe ^/v suffit à
    indiquer le sens, pas besoin de le répéter en toutes lettres) — nom,
    catégorie, dernière vérif, DLL, MAJ Thunderstore, **source**
    (Thunderstore/Nexus/Hexium/Aucune), lien, note
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
  - **Recherche** : champ texte en haut de la liste, filtre en direct (à
    chaque frappe) sur le nom, la catégorie, la description, la note, les
    tags, le modpack et le lien. Se combine avec les filtres Modpack/Tag et
    affecte aussi **Tout vérifier** / **Tout DL**, comme eux. Volontairement
    **non enregistré** d'un lancement à l'autre (contrairement aux filtres
    Modpack/Tag) — une recherche ponctuelle n'a pas vocation à rester active
    silencieusement. Bouton **Effacer** à côté pour la vider d'un coup.
  - **Cases "sur quoi chercher"** (juste sous le champ de recherche) : une
    case par champ inclus dans la recherche (nom, catégorie, description,
    note, tags, modpack, lien) — décoche ceux qui ne t'intéressent pas (par
    exemple les liens, si tu ne veux pas qu'une recherche remonte des mods
    juste parce que leur URL contient le mot tapé). Si **toutes** les cases
    sont décochées, la recherche repasse sur tous les champs plutôt que de
    ne plus rien afficher. Contrairement au texte tapé, ce choix **est**
    enregistré d'un lancement à l'autre (c'est une préférence d'usage, pas
    une recherche ponctuelle).
  - **Modpacks** : chaque mod peut être rattaché à un "modpack" (nom libre,
    tapé ou choisi dans l'éditeur — utile pour regrouper les mods d'un même
    playthrough ou serveur). Le **menu déroulant de filtre "Modpack"** (barre
    du haut) limite l'affichage à un modpack donné, et affecte aussi **Tout
    vérifier** / **Tout DL** (qui n'opèrent alors que sur ce sous-ensemble).
  - **Tags** : chaque mod peut aussi porter des tags libres, séparés par des
    virgules (champ "Tags" dans l'éditeur — ex: `QoL, Building, Serveur`).
    Contrairement au modpack (unique par mod), un mod peut avoir plusieurs
    tags. Le **menu déroulant de filtre "Tag"** (barre du haut, à côté du
    filtre Modpack) limite l'affichage aux mods portant le tag choisi, et se
    combine avec le filtre Modpack (les deux s'appliquent ensemble) — pratique
    pour filtrer plus vite sans avoir à créer un modpack pour chaque
    regroupement.
  - **Masquer à jour** (case à cocher, barre du haut) : cache les mods déjà
    à jour (statut Thunderstore vert) de l'affichage et de **Tout DL** —
    pratique pour ne télécharger que ce qui manque dans le modpack/tag
    courant. N'affecte **pas** "Tout vérifier", qui doit justement pouvoir
    découvrir un changement de statut.
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
  - **Dossier du mod installé** (dans l'éditeur) : plutôt qu'un chemin de
    fichier `.dll` précis, tu indiques le **dossier** dans lequel le mod
    est installé (typiquement `BepInEx\plugins\NomDuMod\`). ValMods
    retrouve le `.dll` à l'intérieur automatiquement (jusqu'à 2
    sous-dossiers de profondeur) pour vérifier sa présence et sa version —
    plus besoin de re-pointer vers le fichier si le mod renomme son DLL
    d'une version à l'autre. Si le dossier contient plusieurs `.dll`, celui
    dont le nom se rapproche le plus du nom du mod est choisi ; les autres
    sont juste comptés (`[+N]`) dans le statut affiché.
  - **Fichier de config** (optionnel, dans l'éditeur) : un chemin vers le
    fichier de config du mod (souvent un `.cfg` sous `BepInEx\config\`,
    mais n'importe quel fichier convient) pour le localiser dans
    l'explorateur ou l'ouvrir directement depuis le menu **...**. Purement
    pratique — n'intervient dans aucune vérification.
  - **Auto-remplir** (dans l'éditeur) : détecte automatiquement Thunderstore,
    Nexus ou Hexium d'après le lien du mod collé, et récupère nom,
    description, dernière version et icône (téléchargée dans `icons/` à
    côté de l'exe, en chemin **relatif** — voir plus bas). Pour
    Thunderstore, récupère aussi catégorie et lien historique ; pour
    Hexium, récupère la catégorie (l'équipe/auteur du mod) mais pas de lien
    historique séparé ; Nexus n'expose ni l'un ni l'autre de la même façon.
    Ne remplace jamais le dossier du mod installé ni le fichier de config,
    qui restent propres à ta machine.
  - **Menu Outils > Compléter les infos manquantes...** : la même logique
    qu'Auto-remplir, mais passée en revue sur **toute la liste** d'un coup,
    et de façon **non destructive** — seuls les champs actuellement **vides**
    (catégorie, lien historique, description, icône) sont complétés ; une
    valeur déjà saisie ou corrigée à la main n'est jamais écrasée. La
    dernière version connue (`tsVersion`) est rafraîchie au passage comme
    bonus sans risque. Ne touche **jamais** `installedVersion` (réservé à
    Télécharger/Tout DL ou à une saisie manuelle), ni les dates de
    vérification individuelles/globales (distinct de **Tout vérifier**).
    Respecte les filtres Modpack/Tag actifs, comme **Tout vérifier**/**Tout
    DL**.
  - **Source de vérification** (menu déroulant dans l'éditeur, remplace
    l'ancienne case "Non Thunderstore") : **Thunderstore** (API publique,
    aucune clé requise), **Nexus Mods** (API officielle Nexus, nécessite une
    clé API personnelle — gratuite, à générer sur ton compte Nexus >
    Paramètres > API Keys, puis à renseigner via **Paramètres > "Définir la
    clé API Nexus..."** dans ValMods), **Hexium** (API publique de
    [Hexium](https://valheim.hexium.gg/), aucune clé requise), ou **Aucune**
    (désactive TS/DL pour ce mod, et l'exclut de **Tout vérifier**/**Tout
    DL** — pratique pour un mod GitHub ou une page perso qu'aucune de ces
    API ne reconnaît).
  - **TS** interroge l'API publique de Thunderstore ou de Hexium (aucune clé
    requise pour les deux) — ou l'API Nexus (avec ta clé) si la source du
    mod est réglée sur Nexus — pour connaître la dernière version publiée,
    et la compare à la version installée suivie (voir plus haut), en
    complétant la description si elle est encore vide.
  - **Tout vérifier** (barre du haut) : lance **TS** sur tous les mods
    éligibles de la liste (Thunderstore, Nexus ou Hexium selon leur source),
    l'un après l'autre (pas en parallèle — l'appli reste bloquée le temps de
    l'opération). Une confirmation préalable indique combien de mods seront
    vérifiés avant de lancer, et un résumé final récapitule mises à jour
    disponibles / à jour / erreurs / ignorés. **Ne touche jamais** à la date
    de vérification individuelle de chaque mod (réservée à une vérification
    manuelle, personnelle — c'est elle qui alimente le code couleur
    rouge/orange/vert/gris) : seule une date de vérification globale,
    séparée (`lastGlobalCheck`), est mise à jour. Peut prendre du temps sur
    une longue liste (pour Hexium, le premier mod vérifié télécharge le
    catalogue complet des mods Valheim, mis en cache 5 minutes — voir plus
    bas).
  - **Tout DL** (barre du haut) : télécharge le zip de la dernière version
    de tous les mods éligibles dans **un seul dossier** choisi une fois
    (au lieu d'une boîte "Enregistrer sous..." par mod). Si la version d'un
    mod n'est pas déjà connue, elle est vérifiée d'abord automatiquement.
    Même logique que **Tout vérifier** : ne touche pas aux dates
    individuelles. Réservé aux mods dont la source est **Thunderstore** ou
    **Hexium** : l'API de téléchargement de Nexus est limitée aux comptes
    Premium, donc le téléchargement direct n'est pas proposé pour un mod
    Nexus.
  - **DL** récupère le zip de la dernière version — via l'URL de
    téléchargement officielle de Thunderstore, ou via le lien de
    téléchargement renvoyé directement par Hexium pour cette version — et
    ouvre une boîte "Enregistrer sous..." pour choisir où le sauvegarder
    (suggestion par défaut dans `downloads/`, entièrement modifiable). Ne
    l'extrait **jamais** automatiquement — ValMods reste manuel, c'est à toi
    de le déposer dans `BepInEx/plugins`. Comme "Tout DL", réservé aux mods
    Thunderstore ou Hexium.
  - **Chemins d'icône relatifs** : une icône téléchargée automatiquement
    (Auto-remplir) ou choisie via "Parcourir..." dans l'éditeur est copiée
    dans `icons/` à côté de l'exe, et enregistrée dans `valmods.json` en
    chemin **relatif** à ce dossier plutôt qu'en chemin absolu. Ça veut dire
    que tu peux envoyer `valmods.json` + `valmods.exe` + le dossier `icons/`
    à quelqu'un d'autre (par exemple pour partager une liste de mods de
    modpack) sans que les icônes se retrouvent cassées chez lui — pense
    seulement à vider ta clé API Nexus avant (`nexusApiKey` dans le JSON,
    ou Paramètres > "Définir la clé API Nexus..." avec un champ vide),
    puisque c'est un secret personnel.

- Accès rapide aux dossiers `BepInEx/plugins`, `BepInEx/config`, dossier du
  jeu, dossier d'installation ou fichier de config d'un mod donné.
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
  "version": 14,
  "valheimDir": "D:\\SteamLibrary\\steamapps\\common\\Valheim",
  "nexusApiKey": "",
  "lastGlobalCheck": "2026-08-19 14:30",
  "showValheim10Check": true,
  "modpackFilter": "",
  "tagFilter": "",
  "hideUpToDate": false,
  "searchInName": true,
  "searchInCategory": true,
  "searchInDescription": true,
  "searchInNote": true,
  "searchInTags": true,
  "searchInModpack": true,
  "searchInUrl": true,
  "mods": [
    {
      "name": "ValheimPlus",
      "category": "QoL",
      "url": "https://thunderstore.io/...",
      "changelogUrl": "https://thunderstore.io/.../changelog/",
      "modDir": "D:\\...\\BepInEx\\plugins\\ValheimPlus",
      "configPath": "D:\\...\\BepInEx\\config\\denikson.ValheimPlus.cfg",
      "iconPath": "icons\\denikson-ValheimPlus.png",
      "tsVersion": "0.9.9.16",
      "tsLatestDate": "2026-03-26T21:26:57Z",
      "installedVersion": "0.9.9.16",
      "description": "QoL and building overhaul for Valheim.",
      "modpack": "Serveur du vendredi",
      "tags": "QoL, Building",
      "apiSource": "thunderstore",
      "nonThunderstore": false,
      "lastCheck": "2026-08-19 14:30",
      "note": "v0.9.9.16"
    },
    {
      "name": "AzuExtendedPlayerInventory",
      "category": "Azumatt",
      "url": "https://valheim.hexium.gg/mods/Azumatt/AzuExtendedPlayerInventory",
      "changelogUrl": "",
      "modDir": "D:\\...\\BepInEx\\plugins\\AzuExtendedPlayerInventory",
      "configPath": "",
      "iconPath": "icons\\Azumatt-AzuExtendedPlayerInventory-hexium.png",
      "tsVersion": "2.4.5",
      "tsLatestDate": "2026-06-29T00:00:00Z",
      "installedVersion": "2.4.5",
      "description": "Inventory expansion mod for Valheim.",
      "modpack": "",
      "tags": "",
      "apiSource": "hexium",
      "nonThunderstore": false,
      "lastCheck": "",
      "note": ""
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

`nexusApiKey` est ta clé API personnelle Nexus Mods (gratuite — compte
Nexus > Paramètres > API Keys), réglée via **Paramètres > "Définir la clé
API Nexus..."**. Nécessaire pour tout mod dont la source est "nexus" ;
Thunderstore et Hexium n'en ont pas besoin (API publiques, sans clé).
**C'est un secret personnel** : vide ce champ (ou repasse par le même menu
avec un champ vide) avant d'envoyer `valmods.json` à quelqu'un d'autre.

`apiSource` (par mod) vaut `"thunderstore"`, `"nexus"`, `"hexium"` ou
`"none"` — quelle API interroger pour la vérification/l'auto-remplissage de
ce mod (voir menu déroulant "Source de vérification" dans l'éditeur).
`"none"` équivaut à l'ancienne case "Non Thunderstore" (désactive TS/DL,
exclut du bulk). Le champ `nonThunderstore` (booléen) reste écrit en
parallèle uniquement pour compatibilité avec un `valmods.json` généré par
une version antérieure à 2.1 ; à la lecture, `apiSource` est prioritaire
s'il est présent.

`showValheim10Check` contrôle l'affichage de l'indicateur "1.0" (voir plus
haut) ; réglé via la case à cocher "Info 1.0" de la barre du haut.

`modpack` (par mod) regroupe des mods pour un même playthrough/serveur —
tapé librement ou choisi dans l'éditeur. `modpackFilter` (racine) est le
modpack actuellement sélectionné dans le menu déroulant de filtre de la
barre du haut ("" = tous les mods). `hideUpToDate` masque les mods déjà à
jour de l'affichage et de "Tout DL" (mais pas de "Tout vérifier", qui doit
justement pouvoir découvrir un changement de statut).

`tags` (par mod) est du texte libre séparé par des virgules ou points-
virgules (ex: `"QoL, Building"`) — contrairement à `modpack`, un mod peut en
avoir plusieurs. `tagFilter` (racine) est le tag actuellement sélectionné
dans le menu déroulant de filtre "Tag" ("" = tous les tags) ; il se combine
avec `modpackFilter` (les deux filtres s'appliquent ensemble) et affecte,
comme lui, "Tout vérifier" / "Tout DL" en plus de l'affichage.

`searchInName`/`searchInCategory`/`searchInDescription`/`searchInNote`/
`searchInTags`/`searchInModpack`/`searchInUrl` (racine, tous booléens)
correspondent aux cases à cocher "sur quoi chercher" sous le champ de
recherche — quels champs la recherche texte libre doit inspecter. Absents
d'un fichier plus ancien, ils valent `true` par défaut (comportement
inchangé). Si tous valent `false` en même temps, la recherche repasse sur
tous les champs plutôt que de ne plus rien afficher. Contrairement au texte
recherché lui-même (jamais enregistré), ce choix de champs **est**
persisté : c'est une préférence d'usage, pas une recherche ponctuelle.

`installedVersion` est la version que ValMods **suit lui-même** comme étant
installée — mise à jour automatiquement par **Télécharger**/**Tout DL** (qui
supposent que le zip va être extrait), et éditable à la main dans l'éditeur.
C'est ce champ, prioritaire, qui sert de base à toutes les comparaisons
("à jour"/"mise à jour disponible", et "Tout vérifier") : la version
embarquée dans le DLL trouvé sous `modDir` n'est utilisée qu'en repli,
quand `installedVersion` est vide, car beaucoup de DLL de mods
Unity/BepInEx n'ont pas de ressource de version fiable.

`modDir` est le **dossier** dans lequel le mod est installé (typiquement
`BepInEx\plugins\NomDuMod\`) — pas le chemin d'un `.dll` précis. ValMods
scanne ce dossier (jusqu'à 2 sous-dossiers de profondeur) pour y trouver le
`.dll` du mod automatiquement ; s'il y en a plusieurs, celui dont le nom se
rapproche le plus du nom du mod est choisi. Un `valmods.json` v13 ou
antérieur, qui stockait `dllPath` (le chemin du `.dll` lui-même), est lu
normalement : son dossier parent est repris comme `modDir` à l'ouverture.

`configPath` (optionnel) est un chemin vers le fichier de config du mod
(souvent un `.cfg` sous `BepInEx\config\`) — purement pratique pour le
localiser/l'ouvrir depuis le menu **...**, n'intervient dans aucune
vérification.

`iconPath` est, quand c'est possible, un chemin **relatif** au dossier de
l'exe (typiquement `icons\NomDuMod.png`) plutôt qu'un chemin absolu — voir
"Chemins d'icône relatifs" plus haut. Un chemin absolu choisi par
Parcourir... est automatiquement copié dans `icons\` et relativisé ; un
chemin absolu tapé à la main qui se trouve déjà sous le dossier de l'exe est
lui aussi relativisé au prochain enregistrement. Un chemin absolu vers un
fichier hors du dossier de l'exe reste tel quel (et ne survivra pas à un
envoi de `valmods.json` à quelqu'un d'autre).

Un fichier `valmods.json` plus ancien (sans `changelogUrl`/`modDir`/
`configPath`/`iconPath`/`tsVersion`/`tsLatestDate`/`installedVersion`/`description`/
`modpack`/`tags`/`apiSource`/`nonThunderstore`/`nexusApiKey`/`lastGlobalCheck`/
`showValheim10Check`/`modpackFilter`/`tagFilter`/`hideUpToDate`/`searchInName`/
`searchInCategory`/`searchInDescription`/`searchInNote`/`searchInTags`/
`searchInModpack`/`searchInUrl`) reste lisible tel
quel : les champs manquants sont simplement traités comme vides / `true` /
`false` selon le champ.

### Rétrocompatibilité avec un ancien/autre format

Au-delà des champs simplement absents (traités comme ci-dessus), ValMods
tolère aussi quelques variations de **forme** pour un fichier plus ancien
ou issu d'une autre version personnelle :

- Le fichier peut être directement un **tableau de mods** (`[ {...}, {...} ]`)
  au lieu de l'objet englobant `{ "mods": [...] }` — le contenu est repris
  tel quel, seuls les réglages racine (dossier Valheim, filtres...) partent
  de leurs valeurs par défaut dans ce cas.
- La liste de mods est aussi cherchée sous quelques noms de clé alternatifs
  (`modList`, `list`, `items`) si `mods` est absent.
- Chaque champ d'un mod essaie, en plus de son nom actuel, quelques
  graphies plausibles utilisées par une version plus ancienne (ex : `url`
  → aussi `link`/`page` ; `lastCheck` → aussi `last`/`lastChecked` ;
  `note` → aussi `notes`/`comment`... ; `modDir` → repli sur l'ancien
  `dllPath`, avec extraction automatique du dossier parent s'il pointait
  vers un fichier `.dll`, voir plus haut).
- Un simple lien texte dans la liste (au lieu d'un objet complet) est
  accepté et devient un mod minimal (nom = lien), à compléter ensuite dans
  l'éditeur.

Si le fichier chargé semble venir d'un schéma plus ancien (ou n'a pas de
numéro de schéma du tout), ValMods garde une copie de l'original à côté
(`valmods.json.premigration`) avant de l'enregistrer au format actuel, et
affiche un message une seule fois pour le signaler. Seule une erreur de
syntaxe JSON authentique (fichier réellement invalide) déclenche le
renommage en `valmods.json.bad` déjà documenté plus haut — un fichier lisible
mais de forme inattendue n'est **jamais** traité comme corrompu.

Ce seuil (`oldSchemaVersion < VALMODS_JSON_SCHEMA_VERSION`, une seule
constante utilisée à la fois pour écrire et pour comparer le numéro de
schéma) a été testé de bout en bout contre un fichier réel généré par
ValMods 2.0.0 (schéma 10, avant Nexus/tags/dossier de mod) : `dllPath` se
convertit bien en `modDir` (dossier parent), `nonThunderstore` en
`apiSource`, et tous les autres champs (catégorie, description, version
installée, etc.) sont repris sans perte.

Si malgré tout certains mods ne réapparaissent pas après cette mise à jour
(un nom de champ trop différent de ceux prévus ci-dessus), la copie
`.premigration` permet de comparer et de rajouter à la main ce qui manque.

Fichier texte, éditable à la main. `valmods.json` n'est volontairement pas
versionné (voir `.gitignore`) : c'est ta liste personnelle, pas un fichier
du projet.

## Nexus Mods

En plus de Thunderstore, ValMods peut vérifier la version d'un mod hébergé
sur Nexus Mods, via l'API officielle de Nexus. Contrairement à Thunderstore,
Nexus exige une clé API personnelle :

1. Sur [nexusmods.com](https://www.nexusmods.com), va dans ton compte >
   Paramètres > API Keys, et génère (ou copie) ta clé personnelle.
2. Dans ValMods, menu **Paramètres > "Définir la clé API Nexus..."**, colle
   cette clé.
3. Dans l'éditeur d'un mod, choisis **Nexus Mods** dans le menu déroulant
   "Source de vérification" (ou laisse **Auto-remplir** le détecter tout
   seul à partir d'un lien `nexusmods.com/valheim/mods/<id>`).

Limites par rapport à Thunderstore :
- Le **téléchargement direct** (boutons DL / Tout DL) n'est **pas**
  disponible pour Nexus : l'API de téléchargement de Nexus est réservée aux
  comptes Premium. Pour ces mods, utilise Watch pour ouvrir la page et
  télécharge le fichier toi-même.
- Nexus ne fournit pas de catégorie ni de lien "historique" séparé de la
  même façon que Thunderstore ; Auto-remplir ne les propose donc pas pour un
  mod Nexus.

## Hexium

ValMods peut aussi vérifier la version d'un mod hébergé sur
[Hexium](https://valheim.hexium.gg/), une plateforme de mods Valheim plus
récente que Thunderstore/Nexus. Comme Thunderstore, l'API publique de
Hexium ne demande **aucune clé** :

1. Dans l'éditeur d'un mod, choisis **Hexium** dans le menu déroulant
   "Source de vérification" (ou laisse **Auto-remplir** le détecter tout
   seul à partir d'un lien `valheim.hexium.gg/mods/<équipe>/<nom>`).
2. C'est tout — pas de compte ni de clé à configurer.

Contrairement à Nexus, le **téléchargement direct** (boutons DL / Tout DL)
**est** disponible pour Hexium : chaque version renvoyée par l'API inclut
un lien de téléchargement direct du zip.

Particularité technique : l'API Hexium n'a pas d'endpoint "un seul mod par
nom" comme Thunderstore — seulement un catalogue complet de tous les mods
Valheim. ValMods télécharge donc ce catalogue au premier mod Hexium vérifié
et le garde en mémoire 5 minutes (jamais écrit sur le disque) pour ne pas le
retélécharger à chaque mod pendant un **Tout vérifier**/**Tout DL**/
**Compléter les infos manquantes**. Autre différence avec Thunderstore :
Auto-remplir récupère bien une catégorie (l'équipe/l'auteur du mod) pour un
mod Hexium, mais pas de lien "historique" séparé — Hexium n'expose pas ça
dans ce même format.

## Partager ta liste (valmods.json + valmods.exe)

`valmods.json`, `valmods.exe` et le dossier `icons\` peuvent être envoyés
ensemble à quelqu'un d'autre (même modpack, même serveur...) : les chemins
d'icône sont enregistrés en **relatif** au dossier de l'exe (voir plus
haut), donc ils continuent de fonctionner une fois copiés ailleurs. Deux
choses à vérifier avant l'envoi :
- **Vide `nexusApiKey`** dans `valmods.json` (ou repasse par Paramètres >
  "Définir la clé API Nexus..." avec un champ vide) — c'est ta clé
  personnelle, pas celle du destinataire.
- `modDir`, `configPath` et `valheimDir` restent propres à ta machine
  (chemins vers ton installation Steam) — le destinataire devra les
  réajuster dans l'éditeur ou dans Paramètres.

