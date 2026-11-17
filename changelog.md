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
