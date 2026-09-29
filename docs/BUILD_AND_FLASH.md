# Build et flash du prototype

## Prérequis
- PlatformIO CLI ou extension VS Code.
- ESP32 Keyestudio connecté en USB.
- Aucun secret de production.

## Configuration locale
Les valeurs sensibles et propres au banc doivent être injectées localement avec des build flags
non committés:
- DOMO_WIFI_SSID
- DOMO_WIFI_PASSWORD
- DOMO_MQTT_HOST
- DOMO_MQTT_PORT
- DOMO_MQTT_USER
- DOMO_MQTT_PASSWORD
- DOMO_KIT_SERIAL
- DOMO_DEVICE_UID

Le dépôt fournit uniquement des valeurs neutres de développement.

## Vérification compilation
Depuis la racine:
```
pio run -e keyestudio-esp32
```

## Flash
```
pio run -e keyestudio-esp32 -t upload
```

## Moniteur série
```
pio device monitor -b 115200
```

## Validation
Après flash, exécuter tests/MQTT_INTEGRATION.md. Une compilation seule ne valide ni le câblage
Keyestudio, ni QoS 1 de bout en bout, ni les règles de sécurité production.
