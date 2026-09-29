# Transport MQTT — conformité IoT V1

## Décision technique

Le contrat IoT V1 impose QoS 1.

Le prototype initial utilise PubSubClient. Cette bibliothèque permet la souscription avec QoS 1,
mais son API de publication utilisée par le firmware ne fournit pas une publication QoS 1.

Conséquence: command/schedule entrants peuvent être souscrits en QoS 1, mais ACK, heartbeat, state
et telemetry sortants ne doivent pas être déclarés conformes QoS 1 avec cette implémentation.

## Action requise

Remplacer la couche transport par un client ESP32 supportant explicitement:
- publish QoS 1;
- subscribe QoS 1;
- reconnexion;
- TLS pour la cible production;
- authentification injectée;
- confirmation/gestion des publications QoS 1.

Le Core DOMOSOLUCES et HardwareAdapter doivent rester indépendants du client MQTT concret.

## Critère de sortie

Une capture/test broker doit prouver QoS 1 sur:
- command;
- ack;
- heartbeat;
- state;
- telemetry;
- schedule.

Aucune modification du contrat IoT V1 n'est nécessaire: il s'agit d'une correction d'implémentation.
