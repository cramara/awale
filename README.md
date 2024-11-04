# Awale - Jeu en réseau

## Compilation

Le projet utilise un Makefile pour faciliter la compilation. Dans le répertoire du projet, exécutez simplement :

```bash
make
```

Cette commande compilera automatiquement le serveur et le client.

Pour nettoyer les fichiers compilés :
```bash
make clean
```

## Lancement du serveur

Pour lancer le serveur :
```bash
./awale_server <port>
```
Exemple : `./awale_server 8080`

## Lancement du client

Pour lancer le client :
```bash
./awale_client <ip_serveur> <port>
```
Exemple : `./awale_client 127.0.0.1 8080`

## Comment jouer

### Connexion
1. Lancez le client
2. Entrez votre pseudo lorsque demandé

### Commandes disponibles
- `/help` - Affiche la liste des commandes
- `/list` - Affiche la liste des joueurs disponibles
- `/games` - Affiche la liste des parties en cours
- `/challenge <pseudo>` ou `/c <pseudo>` - Défie un joueur
- `/accept <pseudo>` ou `/a <pseudo>` - Accepte un défi
- `/observe <id_partie>` - Observe une partie en cours
- `/message <pseudo> <message>` - Envoie un message à un joueur
- `/message all <message>` - Envoie un message à tous les joueurs
- `/quit` - Quitte le jeu

### Règles du jeu
1. Le plateau contient 12 trous (6 par joueur) avec 4 graines dans chaque trou au début
2. Chaque joueur joue à son tour en choisissant un trou de son camp (1-6)
3. Les graines sont distribuées une par une dans les trous suivants dans le sens anti-horaire
4. Si la dernière graine distribuée fait que le trou contient 2 ou 3 graines, le joueur capture ces graines
5. Le but est de capturer plus de graines que l'adversaire
6. La partie se termine quand :
   - Un joueur a capturé 25 graines ou plus
   - Un joueur ne peut plus jouer (famine)

### Interface du plateau
```
   -------------------------------
   | 11 | 10 |  9 |  8 |  7 |  6 | (Adversaire)
   -------------------------------
   |  0 |  1 |  2 |  3 |  4 |  5 | (Vous)
   -------------------------------
      1    2    3    4    5    6
```

### Pour jouer un coup
- Pendant votre tour, entrez un nombre de 1 à 6 correspondant au trou que vous voulez jouer
- Le jeu affichera automatiquement le plateau mis à jour après chaque coup

## Notes
- Les trous de votre camp sont affichés en vert
- Le plateau s'adapte à la perspective de chaque joueur
- Les observateurs peuvent voir la partie mais ne peuvent pas jouer
- La partie se termine automatiquement quand un joueur gagne
