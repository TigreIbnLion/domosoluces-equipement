# DOMOSOLUCES IoT V1

Statut: NORMATIF V1.0
Toute rupture de compatibilite exige validation du LEAD.

## Identite et topics
Racine:
domosoluces/kits/{kit_serial}/devices/{device_uid}

Topics V1:
- command : serveur vers equipement
- ack : equipement vers serveur
- heartbeat : equipement vers serveur
- state : equipement vers serveur
- telemetry : equipement vers serveur
- schedule : serveur vers equipement

QoS V1: 1.

## Semantique
status = connectivite/sante: online, offline, error, updating.
state = etat fonctionnel confirme: on, off.
mode = provision, smart, normal, error.

La publication d'une commande ne confirme jamais state.

## Command
Payload minimal:
- command_id: UUID unique
- action: set_state
- state: on ou off
- device_uid
- kit_serial
- sent_at: ISO-8601

Le firmware doit memoriser les command_id recemment executes. Un doublon ne doit pas rejouer l'action physique; il peut republier le resultat ACK.

## ACK
Payload minimal:
- command_id
- status: executed ou failed
- state: on ou off si connu
- error: null ou message
- firmware_version
- uptime_ms

Le serveur n'accepte logiquement l'ACK que si command_id appartient au device du topic.

## Heartbeat
Payload minimal:
- state
- mode
- rssi
- ip_address
- firmware_version
- uptime_ms

Un heartbeat valide marque le device online et actualise last_seen_at.

## State
Payload minimal:
- state: on ou off
- reason: command, local, boot, reconnect ou recovery

A utiliser apres changement physique local, boot/reconnexion ou resynchronisation.

## Telemetry
Champs supportes selon materiel:
- current_power (W)
- energy_kwh
- voltage_v
- current_a
- power_factor

Une mesure non disponible reste absente/null; aucune fausse mesure en production.

## Reconnexion
Apres connexion Wi-Fi/MQTT: publier heartbeat puis state. Les commandes dupliquees restent idempotentes.

## Securite
Aucun secret de production dans Git.
Production finale: authentification device/kit, ACL MQTT minimales et TLS.
Le prototype peut utiliser des credentials de developpement injectes par configuration.
Le firmware source et les secrets DOMOSOLUCES ne sont pas des livrables usine.

## Abstraction materielle
Le protocole ne depend jamais des GPIO.
HardwareAdapter expose les capacites logiques; KeyestudioAdapter mappe le prototype; le produit final fournira un IndustrialHardwareAdapter.

## Compatibilite
Les ajouts optionnels sont permis dans V1. Suppression, renommage ou changement semantique d'un champ/topic impose une decision LEAD et une nouvelle version si incompatible.
