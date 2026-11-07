# AuctionP2P

AuctionP2P est un système d'enchères pair-à-pair (P2P) décentralisé : pas de serveur
central, chaque participant lance ce programme et devient un pair du réseau. Au démarrage,
un pair rejoint automatiquement le réseau existant ou en crée un nouveau s'il est le premier ;
les échanges entre pairs sont ensuite chiffrés (TLS) et signés cryptographiquement.

N'importe quel pair peut démarrer une enchère avec un prix de départ, et n'importe quel
autre peut enchérir en proposant un prix plus élevé. Chaque offre est validée par consensus
entre les pairs (avec réélection automatique du superviseur en cas de besoin), et la vente se
termine dès qu'aucune nouvelle offre n'arrive pendant un certain délai.

Le programme s'utilise ensuite via une console interactive : tapez `help` ou `ahelp` une
fois lancé pour voir les commandes disponibles (créer/rejoindre une enchère, enchérir,
consulter l'état des ventes, etc.).

## Dépendances

- `gcc` (ou `clang`)
- `make`
- `libreadline` (en-têtes de développement, ex. paquet `libreadline-dev` sous Debian/Ubuntu)
- OpenSSL / `libssl` (en-têtes de développement, ex. paquet `libssl-dev` sous Debian/Ubuntu),
  utilisé pour le chiffrement TLS des communications et la signature des messages

Sous macOS, les en-têtes OpenSSL et Readline installés via Homebrew sont détectés
automatiquement par le `Makefile`.

## Compilation

```bash
make
```

Cela génère l'exécutable `AP2P` à la racine du projet, à partir des sources de `src/` compilées dans `build/`.

Pour nettoyer les fichiers compilés :

```bash
make clean
```

Pour tout recompiler depuis zéro :

```bash
make re
```

### Compilation avec logs de debug

Le projet dispose d'un système de logs contrôlé par deux macros, `LOG_LEVEL` et `LOG_TRIGGER` (voir `ARCHITECTURE.md` pour le détail). Plusieurs cibles `make` permettent de les activer :

```bash
make debug        # active DEBUG_CALL, sans logs
make info          # logs de niveau LOG_INFO
make warning       # logs de niveau LOG_WARNING
make error         # logs de niveau LOG_ERROR
make max_info      # logs <= LOG_INFO
make max_warning   # logs <= LOG_WARNING
make max_error     # logs <= LOG_ERROR
```

## Exécution

```bash
./AP2P
```
