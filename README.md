# Image Exporter (mod de Geode)

Convierte una imagen en objetos de Geometry Dash dentro del editor, con **8 métodos normales + 10 estilos monocromos** y un tope duro de **10.000 objetos**.
Base técnica: el mod *Art importer* (Grian/CelestialGecko): objeto `3092` (cuadrado de 1 u), color por HSV absoluto sobre el canal 1010 y `createObjectsFromString`.

## Los 8 métodos

| # | Método | Idea | Mejor para |
|---|--------|------|-----------|
| 1 | Cuadrícula uniforme | 1 celda = 1 objeto, color medio | pixel art, rápido |
| 2 | Quadtree adaptativo | divide en 4 solo donde hay detalle (mejor-primero, usa justo el límite) | general |
| 3 | BSP adaptativo (k-d) | corte optimo en X o Y, rectángulos de cualquier proporción | **fotos** (rápido) |
| 4 | Pirámide de capas | bloque grande + capas de detalle solo donde el color difiere (criterio rate-distortion) | imágenes con zonas lisas |
| 5 | Regiones por paleta | cuantiza a paleta (median-cut, o exacta si <=256 colores) y fusiona zonas en rectángulos voraces; busca sola resolución/colores | **ilustraciones y pixel art** (muy pocos objetos) |
| 6 | Franjas horizontales | tiras por fila con tolerancia de color; elige sola filas/tolerancia | estilo "escaneo" |
| 7 | Rectángulos rotados | búsqueda aleatoria + escalada de colina guiada por el mapa de error (estilo Geometrize) | look pictórico |
| 8 | Híbrido BSP + rotados | base BSP (55 %) y el resto en rectángulos rotados sobre los residuos | **máxima calidad** |

Todos garantizan `objetos <= límite` (ajustable de 500 a 10.000 en la ventana). Con transparencia, 7 y 8 pasan solos a BSP (los rectángulos solapados no pueden dejar huecos) y lo avisa.

## Estilos monocromos (9 métodos extra, seccion aparte en la ventana)

Pensados para ser **ligeros**: tinta negra (canal 1010) sobre papel blanco (canal 1011), **sin HSV por objeto**, y normalmente con bastantes menos objetos que el máximo. En la ventana estan debajo de la parte normal; elegir un método normal desactiva el estilo y viceversa. Ajuste "Tinta blanca sobre negro" para fondos oscuros.

| # | Estilo | Idea | Objetos (foto, límite 10.000) |
|---|--------|------|------|
| 9 | Halftone (diamantes) | cuadrados girados 45 grados, tamaño según oscuridad | 9.872 |
| 10 | Grabado de líneas | líneas paralelas con grosor variable | 6.537 |
| 11 | Sombreado cruzado | capas de rayas a 45/135/0/90 grados según el tono | 2.071 |
| 12 | Estarcido | puntos más densos donde es oscuro | 9.977 |
| 13 | Tramado Bayer | blanco y negro ordenado, fusionado en rectángulos | 9.891 |
| 14 | Tinta (cómic) | umbral adaptativo, manchas sólidas | 10.000 (7.934 en la pintura) |
| 15 | Boceto de trazos | trazos cortos sobre los bordes | 7.212 |
| 16 | Espiral | espiral de grosor variable | 9.963 |
| 17 | Diagonales 10 PRINT | una "/" o "\\" por celda según la estructura local | 9.987 |

Cada estilo tiene una separación mínima visible, así que con presupuestos grandes a veces usa menos objetos de los permitidos (p. ej. el sombreado cruzado).

### Sobre el borrado automatico de niveles
No he encontrado información publica sobre la regla exacta de los servidores, **así que no puedo garantizar que un nivel no sea borrado**. Lo que hace el mod es atacar las causas que mencionas: menos objetos, sin HSV en los estilos monocromos, y un indicador **"~KB"** en la ventana con el tamaño aproximado de la cadena del nivel (sin comprimir) para que veas cuanto pesa cada opcion. Los métodos normales con HSV rondan 800-900 KB a 10.000 objetos; los monocromos entre ~130 y ~680 KB. Para ir aún más ligero, baja el límite de objetos. Prueba primero con un nivel pequeño.

## Estilos monocromos (seccion de abajo de la ventana)

Pensados para ser **ligeros**: tinta negra (canal 1010) sobre papel blanco (canal 1011), **sin claves de HSV por objeto** y, en general, muchos menos objetos. La cadena de objetos es ~25 % más corta que con HSV, y el ASCII es unas 20 veces más pequeño.

| # | Estilo | Idea |
|---|--------|------|
| 9 | Halftone (diamantes) | cuadrados girados 45 grados; el tamaño sigue la oscuridad |
| 10 | Grabado de líneas | líneas paralelas de grosor variable por tramos |
| 11 | Sombreado cruzado | rayas a 45, 135, 0 y 90 grados según el tono |
| 12 | Estarcido | puntos con densidad proporcional a la oscuridad (Poisson ponderado) |
| 13 | Tramado Bayer | blanco/negro ordenado, fusionado en rectángulos |
| 14 | Tinta (cómic) | umbral adaptativo: manchas sólidas, muy pocos objetos |
| 15 | Boceto de trazos | trazos cortos orientados sobre los bordes (Sobel + supresión de no-maximos) |
| 16 | Espiral | espiral de Arquimedes de grosor variable |
| 17 | Diagonales 10 PRINT | una diagonal / o \ por celda según la estructura local |
| 18 | Arte ASCII | **una fila de texto = un objeto de texto (914)**; la imagen entera en ~50-200 objetos |

Cada estilo tiene una separación mínima visible, así que **con 10.000 de límite suelen usar bastantes menos** (sombreado cruzado ~2.000, grabado ~4.600-6.500, ASCII ~150).
Ajustes útiles: *Tinta blanca sobre fondo negro*, *Canal negro/blanco*, *ASCII: ancho de carácter* y *ASCII: alto/ancho de carácter*.

### Diagnóstico de la interfaz (v1.1.1)

Si la ventana se ve con errores, activa en Ajustes **"Guardar diagnóstico de la interfaz (txt)"**. Al abrir la ventana (y cada vez que se calcula una imagen) el mod escribe `ui-dump.txt` en su carpeta de guardado; la ruta exacta aparece en una notificacion. El txt incluye la version, el tamaño de pantalla y el factor de escala, la posición/ancla/tamaño/escala y la **caja real en pantalla** de cada elemento, los **solapes** entre textos y botones, lo que se **sale del marco**, el estado del mod y los datos del ultimo resultado (por ejemplo cuantas formas sobresalen de la imagen). Enviando ese archivo se puede ver con exactitud como genera GD la ventana.

### Canales de color, grises y "Estilos ligeros" en color

**Nada de triggers.** Los colores se escriben directamente en los ajustes de color del nivel (lo que lee el selector *Base Color* del editor) y cada objeto solo lleva su canal (`21,<canal>`): sin HSV y sin objetos extra.

**Canales libres.** Antes de colocar, el mod mira que canales usa el nivel (el color base y de detalle de **todos los objetos** y los colores ya definidos) y reparte los colores de la imagen **solo en canales libres**, empezando en *Primer canal de la paleta* (por defecto 2). **El canal 1 nunca se usa.** Al colocar una segunda imagen coge otros canales distintos, así que **no sobrescribe** los colores de la anterior. Si no quedan suficientes, avisa y no coloca nada.

**Métodos 1-8: tres checks** (ninguno marcado = HSV a color, como siempre):
* *Canales de color*: los colores se reducen a una paleta (por defecto 64, ajustable) y cada color es un canal.
* *Gris (HSV)*: la imagen en escala de grises usando HSV.
* *Gris (canales)*: lo mismo, pero con canales (hasta 32 tonos de gris).

**Estilos ligeros (9-18):**
* Por defecto son blanco y negro en **2 canales libres** (negro y blanco). Con el ajuste *Blanco y negro con canales 1010/1011* usan los canales especiales.
* Con el check *Colores en canales (sin HSV)* salen **a color y más limpios**: debajo va un **mosaico de color** (BSP) con el tono de la imagen, y encima el estilo con una version más oscura del mismo tono. Así no hay huecos transparentes ni puntos blancos/negros sueltos. La base compensa la cobertura real de tinta de cada zona, y los trazos largos se parten donde cambia el color.
* La mitad de los canales de la paleta (*Estilos ligeros: canales en color*, por defecto 48) son para la base y la otra mitad para la tinta.

**El z-order se exporta siempre** (clave 25) y los estilos ligeros ponen la base/papel en una capa inferior a la tinta (clave 24). Si falta la clave 25, GD usa el z por defecto del objeto 3092 y el papel puede quedar encima: eso causaba las imágenes en blanco.

* **Sin verificar en GD:** definir los colores usa `GJEffectManager::getColorAction`, `getAllColorActions` y los campos de `ColorAction` (según los bindings 2.2081); la deteccion de canales usa `GameObject::m_baseColor/m_detailColor`.

### Sobre el ASCII (leer)
* El objeto de texto es el **914**, con el texto en base64 en la clave **31** (formato tomado de librerias de la comunidad, no probado aqui en GD).
* **Las filas se parten en objetos de texto** de *ASCII: caracteres por objeto* (60 por defecto; 14 o menos en color) colocados uno junto a otro, para que GD no recorte ni entrecorte las filas largas. Hay hasta 400 columnas (260 en color).
* En color, cada tramo de texto usa el tono de su zona (canal propio) sobre un mosaico de color.
* **La fuente es un ajuste del nivel entero**, no del objeto: elige una monoespaciada (p. ej. la fuente 5, Joystix).
* Hay que **calibrar** según la fuente: *ancho de carácter* (unidades con escala 1; por defecto 24, estimacion), *alto/ancho* (por defecto 1.0) y, si hace falta, *espaciado de letras* (clave 488). Pon una fila de prueba, mide y ajusta; los trozos se colocan con ese ancho, así que un ancho mal calibrado deja huecos o solapes entre trozos.

### Sobre los borrados automaticos de RobTop
No he encontrado información publica sobre la regla exacta, así que **no hay garantia** de que un nivel no se borre. Lo que si hace el mod es atacar lo que se sabe que pesa: menos objetos, sin HSV por objeto y un indicador (`~KB`) del tamaño de la cadena para comparar métodos. Si vas a subir un nivel, prueba antes con uno pequeño.

## La ventana (430x268, cabe en los 320 de alto de la pantalla de GD)

* **Izquierda:** vista previa (140x140) y estado: objetos, tamaño aproximado de la cadena (`~KB`), calidad en dB y tiempo.
* **Derecha:** dos **pestanas**, *Principales* (métodos 1-8) y *Ligeros* (estilos 9-18); flechas para cambiar de método con su descripcion en dos líneas; dos **sliders** (*Objetos*, de 500 a 10.000 en pasos de 100, y *Lado largo*, de 150 a 6.000 u); y una **sola fila de color** que cambia con la pestaña (*Principales*: Canales / Gris HSV / Gris canales, ninguno = HSV a color; *Ligeros*: Colores en canales).
* **Botones:** *Imagen*, *Colocar*, *i* (ayuda) y *Ajustes* (abre los ajustes del mod).
* La ventana recuerda la ultima configuracion. El cálculo se rehace 0,35 s despues de soltar un slider.

## Calidad de los 8 métodos principales según el número de objetos

Medido con PSNR y SSIM (luma) contra una referencia comun a 512 px, en 4 imágenes (foto, pintura, ilustracion plana y logo) a 1.000 / 3.000 / 10.000 objetos. PSNR en dB; entre parentesis, antes de esta version:

| Método | 1.000 | 3.000 | 10.000 |
|---|---|---|---|
| 3 BSP | 26,9 (26,7) | 34,2 (33,7) | 50,6 (50,3) |
| 4 Pirámide | 20,1 (19,7) | 23,8 (23,3) | 31,8 (30,8) |
| 7 Rectángulos rotados | **30,4** (20,9) | **36,3** (23,3) | **44,9** (26,3) |
| 8 Híbrido | 30,0 (27,3) | 37,0 (32,9) | 51,3 (45,8) |

Que cambio:
* **Recolor por visibilidad:** en los métodos con capas (Pirámide, 7 y 8) cada forma toma como color la media de los píxeles que realmente se ven, y las formas totalmente tapadas por otras se **eliminan y su presupuesto se reinvierte** en más formas (hasta 3 rondas).
* **Rectángulos rotados** parte ahora de una base BSP gruesa (6 %) y usa la misma resolución interna que el Híbrido; antes empezaba de un fondo plano y convergia muy despacio.
* **Híbrido:** base BSP del 15 % (medido: entre 10 % y 35 % da lo mismo; más base empeora). Si el BSP con todo el presupuesto ya es casi perfecto (>= 38 dB: logos, ilustraciones), devuelve el BSP.
* **BSP y Quadtree:** la prioridad mira dos niveles (un corte pobre que abre otro muy valioso sube en la cola). Se probo además una fase de intercambios fusionar+dividir y **no mejoraba**, así que se descarto: el voraz ya es casi optimo en esa familia.
* **Franjas:** elige filas o columnas según lo que comprima mejor.
* **Calidad de búsqueda:** por defecto *Alto* (antes *Normal*), nuevo nivel *Máximo*, y con 3.000 objetos o menos se sube un nivel solo (sobra tiempo). *Alto* aporta ~+0,9 dB y +0,02 SSIM frente a *Normal* por ~1,7x de tiempo; con 10.000 objetos el 7 y el 8 tardan hasta ~5 s en una imagen de 512 px.
* Sin cambios apreciables: Cuadrícula (ya es el promedio exacto), Regiones.

## Correcciones tras la primera prueba en el juego

* **La Pirámide de capas se quedaba en un % para siempre** con imágenes transparentes y pocos objetos (un sprite con mucho borde). Los bordes obligaban a un mínimo de objetos que no dependia del umbral, y ese mínimo superaba el presupuesto; el bucle de seguridad subia el umbral sin fin. Ahora las celdas mixtas (parte opaca, parte transparente) se tratan por mayoria cuando el mínimo no cabe, como hace el BSP, y todos los bucles de seguridad tienen tope. Se reprodujo antes de arreglarlo (cuelgue con 1.500, 3.000 y 6.000 objetos) y se comprobo despues con 500, 1.500, 3.000 y 10.000 en los 4 modos de color.
* **Aviso de seguridad:** si un cálculo pasa de 90 s se cancela y la ventana lo dice.
* **Interfaz:** la descripcion se salia de la ventana (ahora se mide el texto y se parte en líneas a mano), las pestanas tapaban el titulo (son más pequenas), y *Ajustes* quedaba fuera del marco (ahora *Ajustes* e *i* estan arriba a la derecha).

## Estructura

```
src/engine/   motor de conversion SIN dependencias de Geode (core, partition, regions, geometrize, mono, palette, lightcolor, gdexport)
src/ui/       ventana (Popup): método, objetos, tamaño, vista previa, colocar
src/main.cpp  boton en la pestaña Edit (hook de EditorUI::createMoveMenu)
tests/        engine_test.cpp (validacion) y bench.cpp (PSNR/SSIM por método y presupuesto)
.github/      workflow oficial de geode-sdk/build-geode-mod
```

## Compilar

**Opcion facil (recomendada): GitHub Actions.** Sube la carpeta a un repo de GitHub; el workflow compila para Windows, macOS, iOS y Android y deja el `.geode` combinado como artefacto ("Build Output").

**Local:** instala la [Geode CLI](https://docs.geode-sdk.org/getting-started/geode-cli) y el SDK, y en esta carpeta ejecuta `geode build`.

`mod.json` apunta a **Geode 5.11.2 / GD 2.2081** (el ultimo release del SDK). Si tu juego/loader es otro, cambia `geode` y `gd` en `mod.json` (tu *Art importer* usaba 5.3.0, también con 2.2081).

## Probar el motor sin Geode

```
g++ -O2 -std=c++20 -Iinclude -Isrc/engine tests/engine_test.cpp -o engine_test
./engine_test imagen.png 10000 carpeta_salida all
```
Comprueba para cada método: objetos <= límite, calidad (PSNR) y que la cadena de GD generada, reinterpretada, reproduce el mismo dibujo.

## Resultados medidos (10.000 objetos)

PSNR (dB) contra la imagen reducida a 512 px; en la pintura (1000x1000) y la foto (1024x768):

| Método | Pintura | Foto | Tiempo |
|---|---|---|---|
| 1 Cuadrícula | 22,6 | 22,2 | ~15 ms |
| 2 Quadtree | 26,6 | 23,1 | ~40 ms |
| 3 BSP | 30,8 | 24,8 | ~55 ms |
| 4 Pirámide | 26,4 | 23,6 | ~65 ms |
| 5 Regiones | 25,4 | 22,5 | ~170 ms |
| 6 Franjas | 27,5 | 23,7 | ~250 ms |
| 7 Rotados | 26,4 | 24,4 | ~1,5 s |
| 8 Híbrido | **32,4** | **25,6** | ~1,2 s |

En ilustracion plana y pixel art la calidad es casi sin perdida con muchos menos objetos (p. ej. imagen plana 900x600: Regiones 1.796 objetos, Híbrido 2.221; pixel art 64x64: Regiones 99 objetos).
Tiempos medidos en 1 nucleo; una imagen de 4000x3000 tarda <= 1,6 s en cualquier método.

## Lo que NO esta verificado (leer antes de usar)

* **El codigo de Geode (src/ui, src/main.cpp) no se ha compilado ni probado dentro del juego.** Se escribio contra las cabeceras reales del SDK 5.11.2 y los bindings de GD 2.2091, y se comprobo su sintaxis contra un mock mínimo, pero la primera compilacion real puede pedir algun ajuste menor.
* **Canal blanco 1011:** los estilos monocromos usan el canal 1010 (negro, el mismo del Art importer) y el 1011 como blanco; el 1011 no lo he podido comprobar en el juego. Si el papel no sale blanco, cambia el ajuste "Canal blanco".
* Los estilos monocromos asumen que el **canal 1011 es blanco** (y el 1010 negro, como usa el Art importer). Si el blanco no se ve bien, cambia *Canal blanco* en los ajustes.
* El ASCII depende de la fuente del nivel y de la calibracion descrita arriba.
* El sentido de giro de los rectángulos rotados (métodos 7 y 8) sigue la convencion de cocos2d (horario). Si salen girados al revés, activa el ajuste **"Invertir giro"**.
* El objeto 3092 con escala X/Y (claves 128/129) y el color HSV se toman del *Art importer*, que funciona; no se han probado aqui en GD.
* La importacion **no se puede deshacer** (como en el Art importer); guarda el nivel antes. Los objetos quedan seleccionados para moverlos o borrarlos.
