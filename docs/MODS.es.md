# Mods de archivos

*In English: [MODS.md](MODS.md).*

CTR Nitro-Fueled guarda sus datos en la carpeta `archives/` del juego: archivos IGA (`*.pak`) que
contienen los ficheros IGZ (Alchemy) del juego, junto a los videos Bink (`*.bk2`). Un mod de
archivos sustituye archivos `.pak` completos (o añade otros). `scripts/mods.py` superpone los mods
activados sobre la carpeta del juego en cada arranque; la carpeta del juego nunca se modifica.

## Instalar un mod

Pon cada mod en su propia carpeta dentro de la carpeta de mods:

| Instalación | Carpeta de mods |
|---|---|
| Windows portable | `mods\` junto al launcher (`BB_DATA_DIR`) |
| AppImage | `~/.local/share/sacredfire/mods/` |
| Desde el repositorio (`run.sh`) | `mods/` en el repositorio, o `BB_MODS_DIR` |

Descomprime antes los ZIP/7z. Estructuras aceptadas (mayúsculas o minúsculas, da igual):

```text
mods/
  Mi kart/
    archives/
      loadingscreen_octane.pak
  Envuelto/
    CUSA13795/archives/...      (también app0/, CUSA14876/ o Image0/)
  Solo archivos/
    juicedomain_hub.pak         (los *.pak sueltos van a archives/)
    leeme.txt
  Descargado v1.2/
    Descargado v1.2/archives/...   (una carpeta de más, como al extraer un ZIP en una carpeta con su nombre)
```

Cualquier otra carpeta de datos que tenga el juego (salvo `sce_module` y `sce_sys`) se puede
sustituir igual. Los leeme, imágenes y carpetas que no son del juego se quedan fuera. El registro
de arranque dice qué trae cada mod: `Mods: Mi kart: 1 files` y después
`Mods: 1 game files replaced, 0 added`. Si un mod debería sustituir archivos y dice
`0 game files replaced`, las rutas dentro del mod están mal.

## Orden y activación

Las carpetas nuevas se activan solas y se añaden al final en orden alfabético. `mods.json`, en la
carpeta de datos, guarda el orden y los desactivados; gana el mod que va más abajo en `order`:

```json
{"order": ["Primero", "Segundo"], "disabled": ["Primero"]}
```

Si dos mods sustituyen el mismo archivo, el registro nombra a los dos
(`Mods: archives/x.pak: Primero -> Segundo`). Los `.pak` nunca se combinan: el último gana entero.
`BB_MODS_ENABLED=0` desactiva todos los mods y `BB_MODS_CONFIG` indica otro `mods.json`.

Una carpeta `CUSA13795-mods/` junto a la del juego (la convención de shadPS4) se carga primero. Si
la carpeta de mods contiene `archives/` directamente, se carga entera como un solo mod.

```sh
BB_GAME_DIR=/ruta/a/CUSA13795 BB_MODS_DIR=/ruta/a/mods bash run.sh
BB_GAME_DIR=/ruta/a/CUSA13795 BB_MODS_ENABLED=0 bash run.sh
```

`BB_MOD_TRACE=1` (con `BB_MODS_DIR`) registra las primeras lecturas de los archivos de los mods.

## Límites

- **Ultra-panorámico** (Ajustes > Pantalla > Relación de aspecto 21:9 o 32:9) modifica 13 archivos
  en su propia vista preparada: `h100_hub`, `juicedomain_hub`, `juicedomain_permanent`,
  `juicedomain_race`, `juicedomain_race_flareday`, `juicedomain_race_flarenight`,
  `juicedomain_startscreen`, `legal`, `loadingscreen_octane`, `octane_startscreen`, `update`,
  `update2`, `update_hub2`. Los mods se aplican encima de esa vista: un mod que sustituye uno de
  esos archivos gana, y esa parte de la pantalla vuelve a 16:9.
- El ejecutable y los archivos del sistema (`eboot.bin`, `sce_module/`, `sce_sys/`) se rechazan;
  los cambios de código son parches (`patches/`, XML de shadPS4). Los enlaces simbólicos dentro de
  un mod también se rechazan.
- Los mods deben estar hechos para el juego de PS4 (CUSA13795 1.21, o CUSA14876 para la edición
  europea). Los archivos de las versiones de Switch o Xbox son de otra plataforma y no se espera
  que carguen. Algunos ficheros de la edición europea difieren de los de EE. UU. en sus registros
  de dependencias, así que un mod de EE. UU. puede necesitar adaptarse (`tools/igz_merge.py` combina
  una edición de EE. UU. con un fichero europeo).

## Cómo funciona

La superposición es un árbol de enlaces en la carpeta de datos (`out/mod-game-*`; en Windows sin
permiso de enlaces simbólicos y con el juego en otra unidad, junto al juego): se enlaza cada
entrada del juego, y cada carpeta que toca un mod se abre un nivel para que solo los archivos
sustituidos apunten al mod. El juego ve ese árbol como `/app0`, de solo lectura. Se borra al
cerrar el juego; un árbol que quede tras un cierre forzado no se reutiliza.
