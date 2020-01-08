# Convertisseur d'images bmp

Ce programme multi-threadé développé en C appliquera différents effets à une image BMP.

Pour cela, compilez le programme avec :

`gcc-9 edge-detect.c bitmap.c -O2 -ftree-vectorize -fopt-info -mavx2 -fopt-info-vec-all -pthread -o apply-effect`

puis lancez le programme :

`./apply-effect "./in/" "./out/" 3 edge-detect`

Où `apply-effect` est le nom de l'exécutable, `in` est le nom du fichier contenant des fichiers .bmp, `out` est le nom du dossier qui contiendra les fichiers convertis, `3` est le nomre de threads et `edge-detect` est l'effet désiré.

Les effets disponibles sont :

- `boxblur` : applique un effet de flou.
- `edge-detect` : détection des bords.
- `sharpen` : accentuation des bords.

**Attantion !** Le nombre de threads ne peut dépasser 10. L'application ne traite que des images BMP 24bits.

© Marion Hurteau & Robin St Georges 2020