# Minishell — interpréteur de commandes simplifié

Projet pédagogique en langage C utilisant les primitives Unix/POSIX.

## Fonctionnalités

- Exécution de commandes externes avec `fork()` et `execvp()`.
- Attente des processus avec `waitpid()`.
- Commandes internes : `cd`, `pwd`, `help`, `exit`.
- Redirections : `<`, `>`, `>>`.
- Tubes et pipelines : `ls | wc -l`, `cat fichier | grep mot | wc -l`.
- Exécution en arrière-plan avec `&`.
- Gestion de `SIGCHLD` pour récupérer les processus terminés.
- Gestion basique de `SIGINT` et `SIGQUIT`.
- Guillemets simples/doubles et échappement simple des caractères.

## Compilation

Sur Linux, WSL ou une machine Unix :

```bash
make
```

## Lancement

```bash
./minishell
```

## Exemples

```bash
pwd
ls -l
cd /tmp
echo "bonjour le monde"
cat fichier.txt | grep erreur | wc -l
ls > liste.txt
echo "une ligne" >> liste.txt
wc -l < liste.txt
sleep 10 &
help
exit
```

## Nettoyage

```bash
make clean
```

## Limites connues

Cette version est un minishell pédagogique, pas un remplacement complet de Bash.
Elle ne gère pas les variables d'environnement dans les commandes, les expansions
(`$HOME`, `~`, `*`), les substitutions, les séparateurs `;`/`&&`/`||`, ni le
contrôle interactif complet des groupes de processus (job control avec `fg`/`bg`).
La gestion des erreurs lors d'un échec de `fork()` au milieu d'un pipeline peut
également être améliorée.

## Fichiers

- `minishell.c` : code source principal.
- `Makefile` : compilation.
- `README.md` : documentation.
