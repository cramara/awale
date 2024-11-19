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

## Commandes disponibles et leur utilisation

### Gestion du profil
1. **Créer/modifier votre biographie**
```bash
/create-bio <texte>
```
Exemple : `/create-bio J'adore jouer à l'Awalé !`

2. **Consulter la bio d'un joueur**
```bash
/check-bio <pseudo>
```
Exemple : `/check-bio Alice`

### Gestion de la visibilité des parties

3. **Ajouter un observateur privé**
```bash
/add-private-observer <pseudo>
```
Exemple : `/add-private-observer Bob`
- Permet à Bob d'observer vos parties même en mode privé

4. **Retirer un observateur privé**
```bash
/remove-private-observer <pseudo>
```
Exemple : `/remove-private-observer Bob`
- Retire à Bob le droit d'observer vos parties privées

5. **Lister vos observateurs privés**
```bash
/list-private-observers
```

6. **Changer le mode de visibilité**
```bash
/public    # Tout le monde peut observer vos parties
/private   # Seuls vos observateurs privés peuvent voir vos parties
```

### Commandes de jeu

7. **Voir les joueurs disponibles**
```bash
/list
```

8. **Voir les parties en cours**
```bash
/games
```

9. **Défier un joueur**
```bash
/challenge <pseudo>
```
ou
```bash
/c <pseudo>
```
Exemple : `/challenge Alice`

10. **Accepter un défi**
```bash
/accept <pseudo>
```
ou
```bash
/a <pseudo>
```
Exemple : `/accept Bob`

11. **Observer une partie**
```bash
/observe <id_partie>
```
Exemple : `/observe 1`
- L'ID de la partie est visible dans la liste des parties (/games)
- En mode privé, seuls les observateurs autorisés peuvent regarder

12. **Abandonner une partie**
```bash
/forfeit
```
ou
```bash
/ff
```

### Communication

13. **Envoyer un message privé**
```bash
/message <pseudo> <message>
```
Exemple : `/message Alice Bonne partie !`

14. **Envoyer un message à tous**
```bash
/message all <message>
```
Exemple : `/message all Qui veut faire une partie ?`

### Autres commandes

15. **Voir l'historique des parties**
```bash
/history
```
- Affiche les résultats des parties terminées avec les scores

16. **Aide**
```bash
/help
```
- Affiche la liste des commandes disponibles

17. **Quitter**
```bash
/quit
```
- Quitte le jeu (abandonne automatiquement si une partie est en cours)

### Pendant une partie

Pour jouer un coup, entrez simplement le numéro de la case (1-6) que vous souhaitez jouer :
```bash
3
```

## Interface du plateau
```
   -------------------------------
   | 11 | 10 |  9 |  8 |  7 |  6 | (Adversaire)
   -------------------------------
   |  0 |  1 |  2 |  3 |  4 |  5 | (Vous)
   -------------------------------
      1    2    3    4    5    6
```

## Notes importantes
- Les cases de votre camp sont affichées en vert
- Le plateau s'adapte à la perspective de chaque joueur
- Les parties peuvent être publiques ou privées
- Les observateurs doivent avoir la permission pour regarder les parties privées
- L'historique conserve uniquement les parties terminées normalement (pas les forfaits)
- Les joueurs en partie ne peuvent pas observer d'autres parties
- Les messages peuvent être envoyés à un joueur spécifique ou à tous les joueurs
- Le mode privé nécessite d'avoir préalablement ajouté des observateurs

## Exemple de session de jeu

1. Se connecter et créer son profil :
```bash
/create-bio Joueur passionné d'Awalé
/add-private-observer Amy
/private
```

2. Commencer une partie :
```bash
/list
/challenge Bob
# Attendre que Bob accepte avec /accept <votre_pseudo>
```

3. Pendant la partie :
- Jouer avec les numéros 1-6
- Utiliser /message pour communiquer
- /forfeit pour abandonner si nécessaire

4. Après la partie :
```bash
/history    # Pour voir les résultats
/games      # Pour voir d'autres parties à observer
```
