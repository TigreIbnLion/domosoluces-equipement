# Matrice de capacites EQUIPEMENT

Statut: document EQUIPEMENT non contractuel. Le contrat metier reste `.project/contracts/IOT_V1.md`.
Le KS5009 est uniquement un banc de simulation. Aucun GPIO, nom de composant ou detail Keyestudio ne doit remonter dans le protocole metier.
Les capteurs gaz et eau/pluie du prototype simulent des evenements: ils ne constituent pas des fonctions de securite certifiees.

| Capacite logique | Interface / fonction materielle | Banc KS5009 | Direction | Validation physique |
|---|---|---|---|---|
| Etat binaire principal (`switch`) | HardwareAdapter::setState/readState + capability `switch` | LED jaune / sortie prototype | actionneur | VALIDE E2E Web/Mobile + declare V2 |
| Mesures electriques V1 | HardwareAdapter::readTelemetry | non disponible | capteur | NON SUPPORTE - ne pas fabriquer |
| Mouvement | KeyestudioHome::inputs/poll | PIR | capteur | A VALIDER |
| Commande locale 1 | KeyestudioHome::poll | bouton 1 | capteur | A VALIDER |
| Commande locale 2 | KeyestudioHome::poll | bouton 2 | capteur | A VALIDER |
| Indice gaz prototype | KeyestudioHome::inputs/poll | capteur gaz | capteur | A VALIDER - simulation non certifiee |
| Indice eau/pluie prototype | KeyestudioHome::inputs/poll | capteur vapeur/eau | capteur | A VALIDER - simulation non certifiee |
| Temperature | KeyestudioHome::snapshot | DHT11 | capteur | A VALIDER |
| Humidite | KeyestudioHome::snapshot | DHT11 | capteur | A VALIDER |
| Ventilation | KeyestudioHome::fan | moteur/ventilateur | actionneur | A VALIDER |
| Signal sonore | KeyestudioHome::buzzer | buzzer | actionneur | A VALIDER |
| Ouverture porte | KeyestudioHome::door | servo porte | actionneur | A VALIDER |
| Ouverture fenetre | KeyestudioHome::window | servo fenetre | actionneur | A VALIDER |
| Indicateur local | KeyestudioHome::indicator | sortie RGB prototype | actionneur | A VALIDER |
| Provisioning Wi-Fi | ProvisioningPortal + DeviceConfig | ESP32 Wi-Fi/NVS | configuration locale | LOGICIEL CI PASS; physique A VALIDER |
| Persistance et recovery | DeviceConfig | NVS ESP32 | interne | LOGICIEL CI PASS; coupure/reboot A VALIDER |
| Reconnexion reseau | coeur firmware | Wi-Fi + MQTT ESP32 | interne | E2E de base VALIDE; endurance A VALIDER |

## Frontiere contractuelle

Aucune nouvelle commande metier n'est definie ici. Les noms, payloads, ACK et etats cloud des capacites avancees restent geles jusqu'a publication d'un contrat valide par le LEAD.

## Regles de validation

- Une capacite n'est marquee VALIDE physiquement qu'apres essai sur le banc reel.
- Une mesure absente ou invalide reste indisponible; aucune valeur artificielle n'est publiee.
- Les details du KS5009 restent dans l'adapter/banc de simulation.
- Le futur materiel industriel devra fournir les memes capacites logiques via un IndustrialHardwareAdapter sans exposer son cablage au protocole.


## Registre de declaration V2

Le firmware ne doit exposer dans son manifeste runtime que les lignes marquees `VALIDE ... + declare V2`.
Etat actuel: **switch uniquement**.

Les autres lignes restent des fonctions de banc disponibles pour recette. Leur presence dans le code, leur compilation ou leur diagnostic serie ne vaut pas validation physique et ne permet pas de les annoncer au serveur.
