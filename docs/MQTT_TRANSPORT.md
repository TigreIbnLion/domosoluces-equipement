# Transport MQTT — conformité IoT V1

## Implémentation

Le firmware utilise désormais le client MQTT natif ESP32 (esp-mqtt) au lieu de PubSubClient.

Le contrat IoT V1 impose QoS 1. L'implémentation demande explicitement QoS 1 pour:
- publication ack;
- publication heartbeat;
- publication state;
- publication telemetry;
- souscription command;
- souscription schedule.

Le Core DOMOSOLUCES et HardwareAdapter restent indépendants des GPIO et de la logique métier serveur.

## Validation matérielle restante

La conformité logicielle demande QoS 1 dans l'API esp-mqtt. La recette sur broker réel doit encore
prouver les échanges QoS 1 de bout en bout et les reconnexions avant validation prototype.

## Production

La cible production doit en plus activer TLS, authentification device/kit et ACL minimales selon IoT V1.
Le cycle de certificats/credentials n'est pas encore défini par un contrat de provisioning et ne doit
pas être inventé dans le firmware prototype.
