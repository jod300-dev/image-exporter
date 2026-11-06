# Image Exporter (mod de Geode)

Convierte una imagen en objetos de Geometry Dash dentro del editor, con **8 metodos normales + 9 estilos monocromos** y un tope duro de **10.000 objetos**.
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

## Estilos monocromos (9 metodos extra, seccion aparte en la ventana)

Pensados para ser **ligeros**: tinta negra (canal 1010) sobre papel blanco (canal 1011), **sin HSV por objeto**, y normalmente con bastantes menos objetos que el maximo. En la ventana estan debajo de la parte normal; elegir un metodo normal desactiva el estilo y viceversa. Ajuste "Tinta blanca sobre negro" para fondos oscuros.

| # | Estilo | Idea | Objetos (foto, limite 10.000) |
|---|--------|------|------|
| 9 | Halftone (diamantes) | cuadrados girados 45 grados, tamano segun oscuridad | 9.872 |
| 10 | Grabado de lineas | lineas paralelas con grosor variable | 6.537 |
| 11 | Sombreado cruzado | capas de rayas a 45/135/0/90 grados segun el tono | 2.071 |
| 12 | Estarcido | puntos mas densos donde es oscuro | 9.977 |
| 13 | Tramado Bayer | blanco y negro ordenado, fusionado en rectangulos | 9.891 |
| 14 | Tinta (comic) | umbral adaptativo, manchas solidas | 10.000 (7.934 en la pintura) |
| 15 | Boceto de trazos | trazos cortos sobre los bordes | 7.212 |
| 16 | Espiral | espiral de grosor variable | 9.963 |
| 17 | Diagonales 10 PRINT | una "/" o "\\" por celda segun la estructura local | 9.987 |

Cada estilo tiene una separacion minima visible, asi que con presupuestos grandes a veces usa menos objetos de los permitidos (p. ej. el sombreado cruzado).

### Sobre el borrado automatico de niveles
No he encontrado informacion publica sobre la regla exacta de los servidores, **asi que no puedo garantizar que un nivel no sea borrado**. Lo que hace el mod es atacar las causas que mencionas: menos objetos, sin HSV en los estilos monocromos, y un indicador **"~KB"** en la ventana con el tamano aproximado de la cadena del nivel (sin comprimir) para que veas cuanto pesa cada opcion. Los metodos normales con HSV rondan 800-900 KB a 10.000 objetos; los monocromos entre ~130 y ~680 KB. Para ir aun mas ligero, baja el limite de objetos. Prueba primero con un nivel pequeno.

## Estilos monocromos (seccion de abajo de la ventana)

Pensados para ser **ligeros**: tinta negra (canal 1010) sobre papel blanco (canal 1011), **sin claves de HSV por objeto** y, en general, muchos menos objetos. La cadena de objetos es ~25 % mas corta que con HSV, y el ASCII es unas 20 veces mas pequeno.

| # | Estilo | Idea |
|---|--------|------|
| 9 | Halftone (diamantes) | cuadrados girados 45 grados; el tamano sigue la oscuridad |
| 10 | Grabado de lineas | lineas paralelas de grosor variable por tramos |
| 11 | Sombreado cruzado | rayas a 45, 135, 0 y 90 grados segun el tono |
| 12 | Estarcido | puntos con densidad proporcional a la oscuridad (Poisson ponderado) |
| 13 | Tramado Bayer | blanco/negro ordenado, fusionado en rectangulos |
| 14 | Tinta (comic) | umbral adaptativo: manchas solidas, muy pocos objetos |
| 15 | Boceto de trazos | trazos cortos orientados sobre los bordes (Sobel + supresion de no-maximos) |
| 16 | Espiral | espiral de Arquimedes de grosor variable |
| 17 | Diagonales 10 PRINT | una diagonal / o \ por celda segun la estructura local |
| 18 | Arte ASCII | **una fila de texto = un objeto de texto (914)**; la imagen entera en ~50-200 objetos |

Cada estilo tiene una separacion minima visible, asi que **con 10.000 de limite suelen usar bastantes menos** (sombreado cruzado ~2.000, grabado ~4.600-6.500, ASCII ~150).
Ajustes utiles: *Tinta blanca sobre fondo negro*, *Canal negro/blanco*, *ASCII: ancho de caracter* y *ASCII: alto/ancho de caracter*.

### Canales de color, grises y "Estilos ligeros" en color

**Nada de triggers.** Los colores se escriben directamente en los ajustes de color del nivel (lo que lee el selector *Base Color* del editor) y cada objeto solo lleva su canal (`21,<canal>`): sin HSV y sin objetos extra.

**Canales libres.** Antes de colocar, el mod mira que canales usa el nivel (el color base y de detalle de **todos los objetos** y los colores ya definidos) y reparte los colores de la imagen **solo en canales libres**, empezando en *Primer canal de la paleta* (por defecto 2). **El canal 1 nunca se usa.** Al colocar una segunda imagen coge otros canales distintos, asi que **no sobrescribe** los colores de la anterior. Si no quedan suficientes, avisa y no coloca nada.

**Metodos 1-8: tres checks** (ninguno marcado = HSV a color, como siempre):
* *Canales de color*: los colores se reducen a una paleta (por defecto 64, ajustable) y cada color es un canal.
* *Gris (HSV)*: la imagen en escala de grises usando HSV.
* *Gris (canales)*: lo mismo, pero con canales (hasta 32 tonos de gris).

**Estilos ligeros (9-18):**
* Por defecto son blanco y negro en **2 canales libres** (negro y blanco). Con el ajuste *Blanco y negro con canales 1010/1011* usan los canales especiales.
* Con el check *Colores en canales (sin HSV)* salen **a color y mas limpios**: debajo va un **mosaico de color** (BSP) con el tono de la imagen, y encima el estilo con una version mas oscura del mismo tono. Asi no hay huecos transparentes ni puntos blancos/negros sueltos. La base compensa la cobertura real de tinta de cada zona, y los trazos largos se parten donde cambia el color.
* La mitad de los canales de la paleta (*Estilos ligeros: canales en color*, por defecto 48) son para la base y la otra mitad para la tinta.

**El z-order se exporta siempre** (clave 25) y los estilos ligeros ponen la base/papel en una capa inferior a la tinta (clave 24). Si falta la clave 25, GD usa el z por defecto del objeto 3092 y el papel puede quedar encima: eso causaba las imagenes en blanco.

* **Sin verificar en GD:** definir los colores usa `GJEffectManager::getColorAction`, `getAllColorActions` y los campos de `ColorAction` (segun los bindings 2.2081); la deteccion de canales usa `GameObject::m_baseColor/m_detailColor`.

### Sobre el ASCII (leer)
* El objeto de texto es el **914**, con el texto en base64 en la clave **31** (formato tomado de librerias de la comunidad, no probado aqui en GD).
* **Las filas se parten en objetos de texto** de *ASCII: caracteres por objeto* (60 por defecto; 14 o menos en color) colocados uno junto a otro, para que GD no recorte ni entrecorte las filas largas. Hay hasta 400 columnas (260 en color).
* En color, cada tramo de texto usa el tono de su zona (canal propio) sobre un mosaico de color.
* **La fuente es un ajuste del nivel entero**, no del objeto: elige una monoespaciada (p. ej. la fuente 5, Joystix).
* Hay que **calibrar** segun la fuente: *ancho de caracter* (unidades con escala 1; por defecto 24, estimacion), *alto/ancho* (por defecto 1.0) y, si hace falta, *espaciado de letras* (clave 488). Pon una fila de prueba, mide y ajusta; los trozos se colocan con ese ancho, asi que un ancho mal calibrado deja huecos o solapes entre trozos.

### Sobre los borrados automaticos de RobTop
No he encontrado informacion publica sobre la regla exacta, asi que **no hay garantia** de que un nivel no se borre. Lo que si hace el mod es atacar lo que se sabe que pesa: menos objetos, sin HSV por objeto y un indicador (`~KB`) del tamano de la cadena para comparar metodos. Si vas a subir un nivel, prueba antes con uno pequeno.

## La ventana (430x268, cabe en los 320 de alto de la pantalla de GD)

* **Izquierda:** vista previa (140x140) y estado: objetos, tamano aproximado de la cadena (`~KB`), calidad en dB y tiempo.
* **Derecha:** dos **pestanas**, *Principales* (metodos 1-8) y *Ligeros* (estilos 9-18); flechas para cambiar de metodo con su descripcion en dos lineas; dos **sliders** (*Objetos*, de 500 a 10.000 en pasos de 100, y *Lado largo*, de 150 a 6.000 u); y una **sola fila de color** que cambia con la pestana (*Principales*: Canales / Gris HSV / Gris canales, ninguno = HSV a color; *Ligeros*: Colores en canales).
* **Botones:** *Imagen*, *Colocar*, *i* (ayuda) y *Ajustes* (abre los ajustes del mod).
* La ventana recuerda la ultima configuracion. El calculo se rehace 0,35 s despues de soltar un slider.

## Calidad de los 8 metodos principales segun el numero de objetos

Medido con PSNR y SSIM (luma) contra una referencia comun a 512 px, en 4 imagenes (foto, pintura, ilustracion plana y logo) a 1.000 / 3.000 / 10.000 objetos. PSNR en dB; entre parentesis, antes de esta version:

| Metodo | 1.000 | 3.000 | 10.000 |
|---|---|---|---|
| 3 BSP | 26,9 (26,7) | 34,2 (33,7) | 50,6 (50,3) |
| 4 Piramide | 20,1 (19,7) | 23,8 (23,3) | 31,8 (30,8) |
| 7 Rectangulos rotados | **30,4** (20,9) | **36,3** (23,3) | **44,9** (26,3) |
| 8 Hibrido | 30,0 (27,3) | 37,0 (32,9) | 51,3 (45,8) |

Que cambio:
* **Recolor por visibilidad:** en los metodos con capas (Piramide, 7 y 8) cada forma toma como color la media de los pixeles que realmente se ven, y las formas totalmente tapadas por otras se **eliminan y su presupuesto se reinvierte** en mas formas (hasta 3 rondas).
* **Rectangulos rotados** parte ahora de una base BSP gruesa (6 %) y usa la misma resolucion interna que el Hibrido; antes empezaba de un fondo plano y convergia muy despacio.
* **Hibrido:** base BSP del 15 % (medido: entre 10 % y 35 % da lo mismo; mas base empeora). Si el BSP con todo el presupuesto ya es casi perfecto (>= 38 dB: logos, ilustraciones), devuelve el BSP.
* **BSP y Quadtree:** la prioridad mira dos niveles (un corte pobre que abre otro muy valioso sube en la cola). Se probo ademas una fase de intercambios fusionar+dividir y **no mejoraba**, asi que se descarto: el voraz ya es casi optimo en esa familia.
* **Franjas:** elige filas o columnas segun lo que comprima mejor.
* **Calidad de busqueda:** por defecto *Alto* (antes *Normal*), nuevo nivel *Maximo*, y con 3.000 objetos o menos se sube un nivel solo (sobra tiempo). *Alto* aporta ~+0,9 dB y +0,02 SSIM frente a *Normal* por ~1,7x de tiempo; con 10.000 objetos el 7 y el 8 tardan hasta ~5 s en una imagen de 512 px.
* Sin cambios apreciables: Cuadricula (ya es el promedio exacto), Regiones.

## Correcciones tras la primera prueba en el juego

* **La Piramide de capas se quedaba en un % para siempre** con imagenes transparentes y pocos objetos (un sprite con mucho borde). Los bordes obligaban a un minimo de objetos que no dependia del umbral, y ese minimo superaba el presupuesto; el bucle de seguridad subia el umbral sin fin. Ahora las celdas mixtas (parte opaca, parte transparente) se tratan por mayoria cuando el minimo no cabe, como hace el BSP, y todos los bucles de seguridad tienen tope. Se reprodujo antes de arreglarlo (cuelgue con 1.500, 3.000 y 6.000 objetos) y se comprobo despues con 500, 1.500, 3.000 y 10.000 en los 4 modos de color.
* **Aviso de seguridad:** si un calculo pasa de 90 s se cancela y la ventana lo dice.
* **Interfaz:** la descripcion se salia de la ventana (ahora se mide el texto y se parte en lineas a mano), las pestanas tapaban el titulo (son mas pequenas), y *Ajustes* quedaba fuera del marco (ahora *Ajustes* e *i* estan arriba a la derecha).

## Estructura

```
src/engine/   motor de conversion SIN dependencias de Geode (core, partition, regions, geometrize, mono, palette, lightcolor, gdexport)
src/ui/       ventana (Popup): metodo, objetos, tamano, vista previa, colocar
src/main.cpp  boton en la pestana Edit (hook de EditorUI::createMoveMenu)
tests/        engine_test.cpp (validacion) y bench.cpp (PSNR/SSIM por metodo y presupuesto)
.github/      workflow oficial de geode-sdk/build-geode-mod
```

## Compilar

**Opcion facil (recomendada): GitHub Actions.** Sube la carpeta a un repo de GitHub; el workflow compila para Windows, macOS, iOS y Android y deja el `.geode` combinado como artefacto ("Build Output").

**Local:** instala la [Geode CLI](https://docs.geode-sdk.org/getting-started/geode-cli) y el SDK, y en esta carpeta ejecuta `geode build`.

`mod.json` apunta a **Geode 5.11.2 / GD 2.2081** (el ultimo release del SDK). Si tu juego/loader es otro, cambia `geode` y `gd` en `mod.json` (tu *Art importer* usaba 5.3.0, tambien con 2.2081).

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
* **Canal blanco 1011:** los estilos monocromos usan el canal 1010 (negro, el mismo del Art importer) y el 1011 como blanco; el 1011 no lo he podido comprobar en el juego. Si el papel no sale blanco, cambia el ajuste "Canal blanco".
* Los estilos monocromos asumen que el **canal 1011 es blanco** (y el 1010 negro, como usa el Art importer). Si el blanco no se ve bien, cambia *Canal blanco* en los ajustes.
* El ASCII depende de la fuente del nivel y de la calibracion descrita arriba.
* El sentido de giro de los rectangulos rotados (metodos 7 y 8) sigue la convencion de cocos2d (horario). Si salen girados al reves, activa el ajuste **"Invertir giro"**.
* El objeto 3092 con escala X/Y (claves 128/129) y el color HSV se toman del *Art importer*, que funciona; no se han probado aqui en GD.
* La importacion **no se puede deshacer** (como en el Art importer); guarda el nivel antes. Los objetos quedan seleccionados para moverlos o borrarlos.
