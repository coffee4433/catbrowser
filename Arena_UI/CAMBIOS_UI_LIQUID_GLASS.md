# Rediseño Liquid Glass de la UI

Cambios en `Source/Arena` (solo C++; los .uasset no se tocan):

- **ArenaGlassStyle.h/.cpp** – nuevo kit glass compartido: `Sheen()` (brillo especular con la textura
  `T_UI_GradientV` recortada a la forma redondeada), `EdgeGlow()` (luz suave en el borde interior),
  `HoverLight()` (color al que se ilumina el cristal), `Layered()` (UMG: brillo + superficie + borde en un overlay)
  y el estilo de botón hover con borde más grueso y luminoso.
- **ArenaGlassButton** – animación completa: hover (borde que respira, cristal que se aclara hacia blanco hielo,
  elevación de 2 px y escala 1.035), pulsación (se hunde, escala 0.955) y destello al soltar.
- **ArenaPauseMenu** – rediseño total: fondo desenfocado con luces de color violeta/menta/azul, botones de
  cristal con subtítulo e indicador de color (CONTINUAR hielo, AJUSTES blanco, ABANDONAR coral), panel social
  en cristal con brillo y borde, sonido al pasar el ratón y animación de entrada (fade + deslizamiento).
- **ArenaInventoryWidget** – panel sobre desenfoque del juego, superficie translúcida con borde brillante,
  brillo superior, slots y tarjetas translúcidos, botones con el estilo glass.
- **ArenaLobbyWidget (WrapGlass)**, **ArenaSettingsWidget (paneles)**, **ArenaEmoteWheel (disco)** –
  todos los paneles pasan por `Layered()` y ganan el brillo especular y la luz de borde.

Requiere recompilar el módulo Arena (UE5). El cvar `arena.BlurRadius` sigue ajustando la esquina del blur.
