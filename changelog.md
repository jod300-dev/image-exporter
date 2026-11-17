# v1.0.0
- Primera version: 8 metodos de conversion, limite duro de 10.000 objetos, vista previa y colocacion en el editor.

# v1.1.0
- 9 estilos monocromos (halftone, grabado, cross-hatch, estarcido, Bayer, tinta, trazos, espiral, 10 PRINT) sin HSV por objeto.
- Indicador del tamano aproximado de la cadena (~KB).
- Tono 0 exacto: ya no desplaza 1 grado (menos error de color en rojos puros).

# v1.1.1
- Metodos 7 y 8: los rectangulos girados ya no sobresalen del borde de la imagen (en GD no se pueden recortar y dejaban restos fuera del cuadro). La imagen queda como un cuadro limpio; la calidad (PSNR) casi no cambia.
- Nuevo ajuste *Guardar diagnostico de la interfaz (txt)*: escribe ui-dump.txt con el arbol de nodos de la ventana (posicion, tamano, escala, caja real en pantalla), los solapes y lo que se sale del marco, mas el estado del mod y del resultado.

# v1.1.2
- Corregido el error de compilacion en Windows (una variable llamada "small" chocaba con una macro de Windows) y la advertencia de localtime.

# v1.1.3
- Interfaz reacomodada a partir del ui-dump.txt: Ajustes ya no se sale del marco ni tapa la pestana "Ligeros"; las pestanas ya no pisan el titulo; las perillas de los sliders ya no pisan sus etiquetas (Objetos / Lado largo); botones mas bajos, ventana un poco mas alta (288) y descripcion/nota ancladas arriba para que 3 lineas no choquen con lo de abajo.

# v1.1.4
- Los textos vuelven a ser ASCII puro: la fuente de GD no tiene tildes ni enie y esos caracteres desaparecian ("Rectngulos", "bsqueda").
- Descripcion del metodo y nota de estado: ahora cada linea es una etiqueta propia con separacion fija. Se acabo el hueco enorme entre la primera y la segunda linea.
- Las pestanas Principales / Ligeros mantienen en azul la activa (el boton invertia su estado despues del clic y las dos quedaban grises). Los checks Canales / Gris HSV / Gris canales tenian el mismo fallo y tambien quedan sincronizados.
- Ajustes pasa abajo a la izquierda, en la misma fila y con el mismo tamano que Imagen y Colocar.
- El boton de info abre un cuadro propio (400x250, sin scroll): texto partido a mano, alineado a la izquierda, con colores por parrafo y el boton OK siempre visible. Ya no se sale de la pantalla ni recorta lineas.
- Diagnostico de UI (ajuste "Guardar diagnostico de la interfaz"): ahora vuelca TODO. Escena completa y arbol entero de cada ventana abierta (mod, info, ajustes de Geode, avisos), con posicion, ancla, tamano, escala, sesgo, rotacion, color, opacidad, hijos y caja real en pantalla; textos literales con sus bytes, glifos que la fuente no tiene, numero de lineas y separacion real entre lineas; resumen de textos y de botones; solapes y desbordes. Guarda una instantanea al abrir la ventana, al cambiar de pestana, al abrir info, al abrir ajustes y al calcular, todas en el mismo ui-dump.txt.

# v1.1.5
- Canales de color: al acabarse los canales 2-999 ya no se vuelve a HSV; el reparto continua en el 1020 y sigue hasta el 9999, saltando 1000-1019 (canales especiales de GD: BG, G1, G2, negro, blanco...). Hay hasta 9978 canales utilizables en vez de 998.
