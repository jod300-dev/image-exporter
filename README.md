# Image Exporter (mod de Geode)

Convierte una imagen en objetos de Geometry Dash dentro del editor, con **8 metodos** y un tope duro de **10.000 objetos**.
Base tecnica: el mod *Art importer* (Grian/CelestialGecko): objeto `3092` (cuadrado de 1 u), color por HSV absoluto sobre el canal 1010 y `createObjectsFromString`.

## Los 8 metodos

| # | Metodo | Idea | Mejor para |
|---|--------|------|-----------|
| 1 | Cuadricula uniforme | 1 celda = 1 objeto, color medio | pixel art, rapido |
| 2 | Quadtree adaptativo | divide en 4 solo donde hay detalle (mejor-primero, usa justo el limite) | general |
| 3 | BSP adaptativo (k-d) | corte optimo en X o Y, rectangulos de cualquier proporcion | **fotos** (rapido) |
| 4 | Piramide de capas | bloque grande + capas de detalle solo donde el color difiere (criterio rate-distortion) | imagenes con zonas lisas |
| 5 | Regiones por paleta | cuantiza a paleta (median-cut, o exacta si <=256 colores) y fusiona zonas en rectangulos voraces; busca sola resolucion/colores | **ilustraciones y pixel art** (muy pocos objetos) |
| 6 | Franjas horizontales | tiras por fila con tolerancia de color; elige sola filas/tolerancia | estilo "escaneo" |
| 7 | Rectangulos rotados | busqueda aleatoria + escalada de colina guiada por el mapa de error (estilo Geometrize) | look pictorico |
| 8 | Hibrido BSP + rotados | base BSP (55 %) y el resto en rectangulos rotados sobre los residuos | **maxima calidad** |

Todos garantizan `objetos <= limite` (ajustable de 500 a 10.000 en la ventana). Con transparencia, 7 y 8 pasan solos a BSP (los rectangulos solapados no pueden dejar huecos) y lo avisa.

## Estructura

```
src/engine/   motor de conversion SIN dependencias de Geode (se prueba en cualquier PC)
src/ui/       ventana (Popup): metodo, objetos, tamano, vista previa, colocar
src/main.cpp  boton en la pestana Edit (hook de EditorUI::createMoveMenu)
tests/        engine_test.cpp: prueba autonoma del motor
.github/      workflow oficial de geode-sdk/build-geode-mod
```

## Compilar

**Opcion facil (recomendada): GitHub Actions.** Sube la carpeta a un repo de GitHub; el workflow compila para Windows, macOS, iOS y Android y deja el `.geode` combinado como artefacto ("Build Output").

**Local:** instala la [Geode CLI](https://docs.geode-sdk.org/getting-started/geode-cli) y el SDK, y en esta carpeta ejecuta `geode build`.

`mod.json` apunta a **Geode 5.11.2 / GD 2.2091** (lo ultimo del SDK). Si tu juego/loader es otro, cambia `geode` y `gd` en `mod.json` (tu *Art importer* usaba 5.3.0 / 2.2081).

## Probar el motor sin Geode

```
g++ -O2 -std=c++20 -Iinclude -Isrc/engine tests/engine_test.cpp -o engine_test
./engine_test imagen.png 10000 carpeta_salida all
```
Comprueba para cada metodo: objetos <= limite, calidad (PSNR) y que la cadena de GD generada, reinterpretada, reproduce el mismo dibujo.

## Resultados medidos (10.000 objetos)

PSNR (dB) contra la imagen reducida a 512 px; en la pintura (1000x1000) y la foto (1024x768):

| Metodo | Pintura | Foto | Tiempo |
|---|---|---|---|
| 1 Cuadricula | 22,6 | 22,2 | ~15 ms |
| 2 Quadtree | 26,6 | 23,1 | ~40 ms |
| 3 BSP | 30,8 | 24,8 | ~55 ms |
| 4 Piramide | 26,4 | 23,6 | ~65 ms |
| 5 Regiones | 25,4 | 22,5 | ~170 ms |
| 6 Franjas | 27,5 | 23,7 | ~250 ms |
| 7 Rotados | 26,4 | 24,4 | ~1,5 s |
| 8 Hibrido | **32,4** | **25,6** | ~1,2 s |

En ilustracion plana y pixel art la calidad es casi sin perdida con muchos menos objetos (p. ej. imagen plana 900x600: Regiones 1.796 objetos, Hibrido 2.221; pixel art 64x64: Regiones 99 objetos).
Tiempos medidos en 1 nucleo; una imagen de 4000x3000 tarda <= 1,6 s en cualquier metodo.

## Lo que NO esta verificado (leer antes de usar)

* **El codigo de Geode (src/ui, src/main.cpp) no se ha compilado ni probado dentro del juego.** Se escribio contra las cabeceras reales del SDK 5.11.2 y los bindings de GD 2.2091, y se comprobo su sintaxis contra un mock minimo, pero la primera compilacion real puede pedir algun ajuste menor.
* El sentido de giro de los rectangulos rotados (metodos 7 y 8) sigue la convencion de cocos2d (horario). Si salen girados al reves, activa el ajuste **"Invertir giro"**.
* El objeto 3092 con escala X/Y (claves 128/129) y el color HSV se toman del *Art importer*, que funciona; no se han probado aqui en GD.
* La importacion **no se puede deshacer** (como en el Art importer); guarda el nivel antes. Los objetos quedan seleccionados para moverlos o borrarlos.
