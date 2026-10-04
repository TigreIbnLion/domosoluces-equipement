# DOMOSOLUCES IoT V2 — Capabilities

Statut: NORMATIF V2.0 ADDITIF.
IOT_V1 reste obligatoire et inchangé pour les équipements V1. Un équipement V2 doit rester compatible avec les invariants V1 applicables (identité, QoS1, heartbeat, ACK, idempotence, autorité serveur de mode). Aucun client ne déduit une capability depuis `device.type`, le nom commercial, un GPIO ou le matériel Keyestudio.

## 1. Séparation des concepts

- **capability**: faculté logique stable d'un équipement, indépendante du matériel.
- **command**: intention demandée à une capability actionnable. Une publication n'est jamais un état confirmé.
- **state/value**: dernière valeur fonctionnelle confirmée connue.
- **telemetry**: mesure périodique/échantillonnée, historisable.
- **event**: occurrence discrète issue d'un capteur ou du firmware.
- **availability**: indique si la capability est lisible, commandable ou configurable.
- **configuration**: paramètres persistants autorisés; distincts de l'état courant.
- **alert**: objet métier Laravel dérivé éventuellement d'un event/telemetry; ce n'est pas un message capteur brut.
- **automation**: règle métier pouvant consommer un event/telemetry et produire une command.

Chaîne normative: mesure/telemetry -> event capteur éventuel -> règle/alerte métier -> automation éventuelle -> command actionneur -> ACK/state confirmé.

## 2. Manifeste Capability V2

Le serveur provisionne un sous-ensemble de capabilities par device. Forme canonique:

```json
{
  "id": "switch",
  "kind": "actuator",
  "semantic": "switch",
  "state": {"type":"string","enum":["on","off"]},
  "commands": [
    {"name":"set_state","input":{"type":"string","enum":["on","off"]}}
  ],
  "telemetry": null,
  "availability": {"readable":true,"commandable":true,"configurable":false}
}
```

Champs:
- `id`: identifiant logique unique dans le device, <=80 caractères.
- `kind`: `actuator|sensor|hybrid`.
- `semantic`: vocabulaire standard ci-dessous.
- `state`: schéma de valeur ou null.
- `commands[]`: commandes réellement implémentées; vide pour une capability non commandable.
- `telemetry`: schéma de mesure ou null.
- `availability.readable|commandable|configurable`: booléens.
- `metadata`: optionnel; peut préciser une sémantique d'usage mais jamais un GPIO/secret requis par un client.

### Vocabulaire standard
Ce registre n'affirme pas qu'un device donné possède toutes ces capabilities. La présence est exclusivement déterminée par son manifeste.

| semantic | usage logique | état/mesure canonique | commande standard si actionnable |
|---|---|---|---|
| `switch` | commutation | `on|off` | `set_state(on|off)` |
| `level` | variation ou vitesse générique | nombre, unité définie par manifeste, typiquement `percent` 0..100 | `set_level(number)` |
| `temperature` | mesure température | nombre `°C` | aucune |
| `humidity` | humidité relative | nombre `%RH` | aucune |
| `motion` | présence/mouvement | boolean | aucune |
| `contact` | ouverture/fermeture | `open|closed` | aucune par défaut |
| `alarm` | avertisseur/buzzer logique | boolean ou `on|off` selon manifeste | `set_active(boolean)` si retenue |
| `gas` | mesure/détection gaz non certifiée | schéma + unité définis par manifeste | aucune |
| `water` | eau/pluie/fuite non certifiée | schéma + unité définis par manifeste | aucune |
| `rgb` | indicateur/couleur | objet `{r,g,b}`, 0..255 | `set_color({r,g,b})` si retenue |
| `rfid` | lecture d'identifiant local | événement; aucune valeur persistante obligatoire | aucune |

`level` ne signifie ni ventilateur ni lumière: `metadata.role` peut valoir par exemple `brightness` ou `speed` si le produit le décide. Le protocole ne crée pas `fan_speed`.
`contact` n'implique pas `open_door`: un capteur d'ouverture est en lecture seule sauf manifeste explicite d'un actionneur distinct.
`rgb` n'autorise `set_color` que si cette commande est déclarée dans le manifeste.

## 3. Topics

Tous les topics V1 restent valides. V2 ajoute seulement:
- `domosoluces/kits/{kit_serial}/devices/{device_uid}/event` : équipement -> serveur.

Les topics `command`, `ack`, `state`, `telemetry`, `heartbeat` existants transportent aussi les payloads V2 via `schema_version:"2.0"`. QoS=1.

## 4. Command V2

Payload:
```json
{
  "schema_version":"2.0",
  "command_id":"UUID",
  "capability_id":"switch",
  "command":"set_state",
  "value":"on",
  "origin":"cloud",
  "device_uid":"...",
  "kit_serial":"...",
  "sent_at":"ISO-8601"
}
```

- `command_id` est obligatoire et idempotent comme V1.
- `origin`: `cloud|local|automation|recovery`. Une commande API client produit `cloud`; une automation serveur produit `automation`. Une action physique locale n'est normalement pas une command MQTT: elle publie un state/event avec origin `local`.
- capability, command et value doivent correspondre exactement au manifeste.
- validation des types: `boolean|number|integer|string|object`, enum/min/max si déclarés.
- une commande inconnue ou invalide ne doit jamais être exécutée par défaut.

Erreurs ACK V2 recommandées et stables: `unsupported_capability|unsupported_command|invalid_value|unavailable|hardware_error|forbidden_mode|internal_error`. Un message humain optionnel peut compléter `error_code`.

## 5. ACK et état confirmé

ACK V2:
```json
{
  "schema_version":"2.0",
  "command_id":"UUID",
  "status":"executed",
  "capability_id":"switch",
  "value":"on",
  "origin":"cloud",
  "error_code":null,
  "error":null,
  "device_uid":"...",
  "kit_serial":"...",
  "firmware_version":"...",
  "uptime_ms":1234
}
```

`status=executed|failed`. Le serveur n'accepte l'ACK que pour le bon device et une commande `pending|sent`. Un doublon ne rejoue pas l'action et ne réécrit pas un état finalisé. Pour V2, `capability_id` doit correspondre à la commande persistée avant que `value` puisse devenir confirmé.

State V2:
```json
{
  "schema_version":"2.0",
  "capability_id":"switch",
  "value":"off",
  "reason":"local",
  "origin":"local",
  "observed_at":"ISO-8601"
}
```
`reason`: `command|local|boot|reconnect|recovery`. State est utilisable pour resynchronisation et changements locaux. Le serveur ignore une capability non provisionnée.

## 6. Telemetry et event

Telemetry V2:
```json
{"schema_version":"2.0","readings":[
  {"capability_id":"temperature","value":26.4,"unit":"°C","observed_at":"ISO-8601"}
]}
```
Chaque reading doit appartenir au manifeste et respecter le schéma/unité déclaré. Les champs électriques V1 restent acceptés.

Event V2:
```json
{
  "schema_version":"2.0",
  "capability_id":"motion",
  "event":"detected",
  "value":true,
  "origin":"device",
  "observed_at":"ISO-8601"
}
```
Un event est un fait brut. Laravel décide séparément s'il crée une alerte, déclenche une automation ou ne fait qu'historiser.

RFID: l'événement peut transporter un identifiant pseudonymisé/tokenisé défini par le produit; le contrat n'impose jamais de publier une clé d'accès secrète.

## 7. status/state/mode V1 et V2

Les champs V1 restent inchangés:
- `status`: santé/connectivité globale du device.
- `state`: compatibilité fonctionnelle globale binaire `on|off` pour les devices V1 qui la portent.
- `mode`: autorité métier serveur `provision|smart|normal|error`.

Les valeurs de capabilities V2 ne remplacent pas automatiquement `device.state`. Un device multi-capabilities peut ne pas avoir de correspondance binaire globale. Web/Mobile utilisent `/capabilities` pour les fonctions avancées et continuent d'utiliser DeviceV1 pour le socle. `device.type` n'est jamais une source de découverte de capability.

## 8. Recovery

Le serveur conserve une politique par capability actionnable:
- `force_off`: au boot/recovery, forcer la valeur sûre OFF/false définie par le schéma.
- `restore_last_state`: restaurer la dernière valeur confirmée persistée localement uniquement si cette politique a été explicitement autorisée pour cette capability.
- `safe_value`: appliquer la valeur explicite `value` validée par le schéma.

Forme:
```json
{"switch":{"policy":"force_off"},"level":{"policy":"safe_value","value":0}}
```

Défaut normatif: tout actionneur sans politique explicite est traité **fail-safe**; aucune restauration automatique de ON. Les sensors n'ont pas de recovery actionneur. La politique est une configuration persistante provisionnée; V2.0 ne crée pas de commande runtime implicite pour la modifier.

Après reboot/reconnexion: appliquer recovery local -> MQTT -> heartbeat -> publier state des capabilities lisibles avec `reason:"recovery"` ou `reconnect`. Les `command_id` déjà exécutés restent idempotents.

## 9. Sécurité capteurs

Les capteurs de prototype, notamment gaz/eau/pluie, sont des composants de démonstration/automatisation. Ils ne sont pas contractuellement des détecteurs certifiés de sécurité des personnes, incendie, gaz ou inondation. Web/Mobile ne doivent pas présenter une mesure prototype comme une garantie de sécurité.

## 10. Provisioning et ownership

Laravel ne reçoit, ne journalise et ne stocke **jamais** le SSID ni le mot de passe Wi-Fi du client.

Le serveur fournit un claim d'ownership distinct de la configuration Wi-Fi:
1. un opérateur autorisé crée un token de pairing aléatoire, TTL 15 min, lié à `kit_id+device_id`;
2. Laravel stocke uniquement SHA-256(token), jamais le token en clair après la réponse d'émission;
3. le Mobile obtient physiquement le `pairing_id + kit_serial + device_uid + token` via un canal local/emballage protégé défini par EQUIPEMENT; il ne doit pas provenir d'un secret statique public;
4. `POST /api/client/pairing/claim` valide token, expiration, identité kit/device et ownership;
5. token consommé une seule fois; un kit déjà possédé par un autre compte est refusé;
6. la remise des credentials Wi-Fi au device se fait localement Mobile<->équipement selon le contrat EQUIPEMENT, hors Laravel.

`kit_secret`, credentials MQTT, CA/private keys et mots de passe Wi-Fi ne sont jamais des données de QR public ni des réponses API client.

## 11. Compatibilité

Un firmware V1 continue de fonctionner sans manifeste V2. Un device sans capabilities retourne une liste vide côté API V2. Aucun champ DeviceV1 n'est supprimé ou renommé. Le runner V1 21/0 reste une gate de non-régression.
