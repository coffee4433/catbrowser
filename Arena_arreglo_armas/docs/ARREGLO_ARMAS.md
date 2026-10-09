# Arreglo: escopeta / rifle / francotirador en la mano, correr y recargar

Síntoma: al equipar un arma de dos manos la skin seguía con los brazos de la locomoción sin arma (brazos colgando o
balanceándose al correr), el arma quedaba cruzada a la altura de la cadera y la recarga movía las manos lejos del arma.

## Qué ocurría

Las poses de sujeción de Fortnite (`Pose_Rifle_NonTargeted_CMF`, `Shotgun_StraightGrip_NonTargeted_CMM`...) duran dos
fotogramas (0,034 s). El componente las reproducía como montaje y las congelaba a velocidad 0 (`HoldLastFrame`), lo que
dependía de que el asset del montaje tuviera el bucle y `bEnableAutoBlendOut` exactamente como esperaba el código y, para
ello, modificaba los assets en memoria durante el juego. Además el giro del arma (`WeaponFit`) y la posición de la mano
izquierda se guardaban en `GameUserSettings.ini` y se volvían a aplicar en cada equipamiento: un giro calculado con la pose
rota quedaba fijo para siempre.

## Cambios (`Plugins/FortnitePorting`)

### Runtime (`FortnitePortingCharacterComponent`)

- `ResolveWeaponMontage`: la pose de sujeción y el trote con arma se reproducen como **montajes transitorios en bucle**
  creados con `CreateSlotAnimationAsDynamicMontage` a partir de la animación importada. Ya no se congelan ni se reinician
  cada frame, y no se toca ningún asset.
- `CheckUpperBodySlot`: 0,4 s después de que la pose esté al 100 % en el slot `UpperBody`, comprueba que ese slot llega a la
  pose final del Anim Blueprint (`GetSlotNodeGlobalWeight`). Si el ABP no lo tiene bien cableado, escribe un error en el
  Output Log y, desde ese momento, reproduce los montajes del arma en `DefaultSlot` (cuerpo completo) para que al menos se
  vea bien mientras se repara el ABP.
- Se elimina la persistencia en `.ini` del giro del arma y del agarre de la mano izquierda. Se recalculan en cada equip.
- Al equipar se escribe una línea de log (`LogFortnitePortingRuntime`) con los montajes elegidos, el perfil y el hueso de
  enganche, para diagnosticar sin abrir el editor.

### Editor (`AnimBlueprintGenerator`, `FortnitePortingBuilder`)

- `RepairUpperBodyLayer`: comprueba en cada `ABP_<Skin>` que `UseCachedPose -> UpperBody slot -> Layered blend per bone
  (spine_01, peso 1)` existe y está conectado; si no, borra esa parte y la vuelve a construir. Se ejecuta con
  `FP.UpgradeAnimBlueprints` y cada vez que se envía un arma desde Fortnite Porting.
- El peso de la capa de armas se fija explícitamente a 1 en el pin.

## Qué hacer

1. Recompilar el proyecto (los dos módulos del plugin cambian: `FortnitePorting` y `FortnitePortingRuntime`).
2. En el editor, consola: `FP.UpgradeAnimBlueprints`. Guardar los ABP que marque como modificados.
3. Probar con la skin: equipar escopeta/rifle/francotirador, correr y recargar.
4. Si algo sigue mal, buscar en el Output Log `LogFortnitePortingRuntime` las líneas `equipped on` y cualquier error sobre
   `UpperBody`, y pasarlas junto con una captura.
