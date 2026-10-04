# Build et flash du prototype Keyestudio

## Securite materielle
Aucun GPIO ni polarite de relais n'est fixe dans le depot. Sans mapping injecte, le firmware compile
mais HardwareAdapter reste indisponible et aucune commande physique n'est executee.
Pour le premier essai: uniquement LED/charge basse tension/module du kit. Jamais de secteur 230 V.

## Parametres injectes hors Git
- DOMO_WIFI_SSID / DOMO_WIFI_PASSWORD
- DOMO_MQTT_HOST / DOMO_MQTT_PORT
- DOMO_MQTT_USER / DOMO_MQTT_PASSWORD si requis par le broker recette
- DOMO_MQTT_TLS=1 pour MQTT TLS; DOMO_MQTT_CA_CERT doit contenir le certificat CA PEM injecte localement
- DOMO_KIT_SERIAL / DOMO_DEVICE_UID, identiques aux identites Laravel/broker
- DOMO_RELAY_PIN, seulement apres verification du mapping Keyestudio
- DOMO_RELAY_ACTIVE_HIGH: 1 si ON=HIGH, 0 si ON=LOW, seulement apres verification

Aucun secret de production ne doit etre utilise ni commite.

## Compilation neutre
```
pio run -e keyestudio-esp32
```
Cette compilation CI n'arme aucune sortie physique.

## Compilation et flash recette
Utiliser localement PLATFORMIO_BUILD_FLAGS avec toutes les valeurs recette. Forme attendue:
```
PLATFORMIO_BUILD_FLAGS='-D DOMO_WIFI_SSID=\"<SSID>\" -D DOMO_WIFI_PASSWORD=\"<PASSWORD>\" -D DOMO_MQTT_HOST=\"<HOST>\" -D DOMO_MQTT_PORT=<PORT> -D DOMO_MQTT_USER=\"<USER>\" -D DOMO_MQTT_PASSWORD=\"<PASSWORD>\" -D DOMO_KIT_SERIAL=\"<KIT_SERIAL>\" -D DOMO_DEVICE_UID=\"<DEVICE_UID>\" -D DOMO_RELAY_PIN=<GPIO_CONFIRME> -D DOMO_RELAY_ACTIVE_HIGH=<0_OU_1>' pio run -e keyestudio-esp32
```
Avec exactement les memes flags:
```
PLATFORMIO_BUILD_FLAGS='...' pio run -e keyestudio-esp32 -t upload
```
Moniteur:
```
pio device monitor -b 115200
```

## Premier E2E physique
1. ESP32 hors tension: connecter uniquement la LED/charge basse tension/module du kit.
2. Verifier le GPIO et la polarite active sur la documentation/carte physique avant de les injecter.
3. Compiler/flasher avec Wi-Fi, broker, kit_serial et device_uid de recette.
4. Demarrer le Laravel listener MQTT de recette puis l'application/API et le Mobile.
5. Au boot ESP32, verifier cote broker/listener: heartbeat puis state reason=boot.
6. Depuis Mobile, envoyer ON une fois: Mobile pending -> API commande -> MQTT command -> ESP32.
7. Observer la charge basse tension ON; verifier state reason=command et ACK executed/state=on;
   verifier ensuite API puis Mobile etat confirme ON.
8. Envoyer OFF une fois et verifier exactement la chaine inverse jusqu'a Mobile confirme OFF.
9. Couper/reprendre le Wi-Fi: aucune commutation spontanee; au retour heartbeat puis state reason=reconnect.
10. Redemarrer l'ESP32: state reason=boot, aucune ancienne commande ne doit etre rejouee.
11. Rejouer depuis le banc MQTT un command_id deja execute: ACK peut etre repete, action physique non rejouee.

La lecture actuelle confirme le niveau de sortie GPIO du MCU, pas le contact reel d'un relais ni la charge.
La recette physique doit donc observer independamment la LED/module.


## Recette ESP32 TLS externe

Parametres publics valides pour la recette physique:
- MQTT host: 157.173.107.21
- MQTT port: 8883
- TLS: obligatoire, verification CA active
- KIT_SERIAL: KIT-RECIPE-ESP32-001
- DEVICE_UID: ESP32-KEYESTUDIO-001
- MQTT username: esp32_keyestudio_recipe
- Keyestudio LED: IO12, active HIGH

Le mot de passe MQTT et le CA PEM restent hors Git. Recuperer uniquement `ca.crt`; ne jamais copier `ca.key`.
Le CA est injecte a la compilation via `DOMO_MQTT_CA_CERT`. Le firmware refuse de demarrer MQTT TLS si ce CA est absent.
Ne jamais desactiver la verification du certificat pour contourner un probleme de connexion.
