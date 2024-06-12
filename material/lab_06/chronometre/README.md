# Labo 6 - DRV

Pour compiler le module:

- Modifier le path des sources du kernel dans le fichier Makefile
- make


Pour compiler l'application userspace:

- Aller dans le repértoire test: 

```
cd test
```
- Cross Compiler

```
arm-linux-gnueabihf-gcc chrono_test.c -Wall -Wextra -o /path/to/export/folder/chrono_test
```

Pour vérifier les différentes fonctionnalités, suivre la spécification de la donnée pour vérifier si tout marche correctement

Remarques:

Toutes les fonctionnalités demandées fonctionnement, inclus le reset de la liste si on est en affichage des tours.

Malheuresement j'ai beaucoup développé sans lire les contraintes et certaines fonctionnalités ont été développées avant de remarquer qu'il fallait faire différemment.
Il peut donc s'avérer que niveau architecture il y a certains problèmes car j'ai du faire des changements de dernière minute pour suivre les contraintes données.
Malheuresement plus beaucoup de temps pour rendre quelque chose de super propre, fin de semestre :(.

**André Costa**
