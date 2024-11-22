// awale_server.c
#include "awale_v2.c"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <math.h>  // Pour pow()

#define MAX_CLIENTS 50
#define MAX_GAMES 25
#define MAX_PSEUDO_LENGTH 50
#define BUFFER_SIZE 2048
#define MAX_MESSAGE_LENGTH 4096
#define RESET_COLOR "\033[0m"
#define GREEN_TEXT "\033[32m"
#define RED_TEXT "\033[31m"
#define MAX_HISTORIQUE 100
#define MAX_PRIVATE_OBSERVERS 10
#define ELO_INITIAL 1200
#define K_FACTOR 32

typedef struct {
  int socket;
  char pseudo[MAX_PSEUDO_LENGTH];
  char bio[MAX_MESSAGE_LENGTH];
  int is_playing;
  int game_id;
  char private_observers[MAX_PRIVATE_OBSERVERS][MAX_PSEUDO_LENGTH];
  int nb_private_observers;
  int is_private; // 1 si le client est privé, 0 sinon (si privé, que les
                  // observateurs de la liste privée peuvent voir la partie)
  int elo;  // Nouveau champ pour le score ELO
} Client;

typedef struct {
  Awale jeu; // Ajout de la structure de jeu
  char player1[MAX_PSEUDO_LENGTH];
  char player2[MAX_PSEUDO_LENGTH];
  int socket1;
  int socket2;
  int observers[MAX_CLIENTS];
  int nb_observers;
} Game;

typedef struct {
  char player1[MAX_PSEUDO_LENGTH];
  char player2[MAX_PSEUDO_LENGTH];
  char winner[MAX_PSEUDO_LENGTH];
  unsigned int score_player1;
  unsigned int score_player2;
} HistoriqueParties;

typedef struct {
  char challenger[MAX_PSEUDO_LENGTH];
  char challenged[MAX_PSEUDO_LENGTH];
} Challenge;

// Variables globales
Client clients[MAX_CLIENTS];
Game games[MAX_GAMES];
Challenge pending_challenges[MAX_CLIENTS];
HistoriqueParties historique[MAX_HISTORIQUE];
int nb_clients = 0;
int nb_games = 0;
int nb_challenges = 0;
int nb_historique = 0;

// Mutex
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t games_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t historique_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t challenges_mutex = PTHREAD_MUTEX_INITIALIZER;

void mettre_a_jour_elo(const char *gagnant, const char *perdant);

void diffuser_etat_partie(Game *game) {
  // Buffer pour stocker l'état complet
  char full_state[BUFFER_SIZE];

  // Créer directement l'état du jeu dans le buffer
  snprintf(
      full_state, BUFFER_SIZE,
      "GAMESTATE %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %s %s ",
      game->jeu.plateau[0], game->jeu.plateau[1], game->jeu.plateau[2],
      game->jeu.plateau[3], game->jeu.plateau[4], game->jeu.plateau[5],
      game->jeu.plateau[6], game->jeu.plateau[7], game->jeu.plateau[8],
      game->jeu.plateau[9], game->jeu.plateau[10], game->jeu.plateau[11],
      game->jeu.scoreJ1, game->jeu.scoreJ2, game->jeu.joueurCourant,
      game->jeu.fini, game->jeu.gagnant, game->jeu.pseudo1, game->jeu.pseudo2);

  // Envoyer aux joueurs
  write(game->socket1, full_state, strlen(full_state));
  write(game->socket2, full_state, strlen(full_state));

  // Envoyer aux observateurs
  for (int i = 0; i < game->nb_observers; i++) {
    write(game->observers[i], full_state, strlen(full_state));
  }

  // Nettoyer le buffer
  memset(full_state, 0, BUFFER_SIZE);
}

void envoyer_liste_joueurs(int socket) {
    char buffer[BUFFER_SIZE] = "PLAYERS";
    pthread_mutex_lock(&clients_mutex);

    char presentation[200];
    snprintf(presentation, 200, "Liste des joueurs %sdisponibles%s et %sindisponibles%s\n\n",
         GREEN_TEXT, RESET_COLOR, RED_TEXT, RESET_COLOR );
    strcat(buffer,presentation);
    
    // Parcourir tous les joueurs
    for (int i = 0; i < nb_clients; i++) {
        char player_info[200];
        if (clients[i].is_playing) {
            // Joueur en partie - en rouge
            snprintf(player_info, 200, " %s%s(ELO: %d)%s", 
                    RED_TEXT, 
                    clients[i].pseudo, 
                    clients[i].elo,
                    RESET_COLOR);
        } else {
            // Joueur libre - en vert
            snprintf(player_info, 200, " %s%s(ELO: %d)%s", 
                    GREEN_TEXT, 
                    clients[i].pseudo, 
                    clients[i].elo,
                    RESET_COLOR);
        }
        strcat(buffer, player_info);
    }
    
    pthread_mutex_unlock(&clients_mutex);
    strcat(buffer, "\n");
    write(socket, buffer, strlen(buffer));
}

void ajouter_historique(const char *buffer) {
  char winner[MAX_PSEUDO_LENGTH];
  char loser[MAX_PSEUDO_LENGTH];
  unsigned int score1, score2;

  // Parse le buffer pour extraire les informations
  // Format attendu: "ADD_HISTORY <gagnant> <perdant> <score1> <score2>"
  if (sscanf(buffer, "ADD_HISTORY %s %s %u %u", winner, loser, &score1,
             &score2) == 4) {

    pthread_mutex_lock(&historique_mutex);
    if (nb_historique < MAX_HISTORIQUE) {
      HistoriqueParties *h = &historique[nb_historique];

      // Stocke le gagnant et le perdant
      strncpy(h->winner, winner, MAX_PSEUDO_LENGTH - 1);
      h->winner[MAX_PSEUDO_LENGTH - 1] = '\0';

      // Détermine qui est player1 et player2 (selon l'ordre du jeu)
      strncpy(h->player1, winner, MAX_PSEUDO_LENGTH - 1);
      h->player1[MAX_PSEUDO_LENGTH - 1] = '\0';
      strncpy(h->player2, loser, MAX_PSEUDO_LENGTH - 1);
      h->player2[MAX_PSEUDO_LENGTH - 1] = '\0';

      // Stocke les scores
      h->score_player1 = score1;
      h->score_player2 = score2;

      nb_historique++;
    }
    pthread_mutex_unlock(&historique_mutex);
  }
}

void envoyer_historique_parties(int socket) {
  char history_message[BUFFER_SIZE] = "HISTORY ";

  pthread_mutex_lock(&historique_mutex);

  // Si aucune partie n'a été jouée
  if (nb_historique == 0) {
    strcat(history_message, "NONE\n");
    write(socket, history_message, strlen(history_message));
    pthread_mutex_unlock(&historique_mutex);
    return;
  }

  // Pour chaque partie dans l'historique
  for (int i = 0; i < nb_historique; i++) {
    char line[256];
    // Format: <gagnant> <perdant> <score1> <score2>|
    snprintf(line, sizeof(line), "%s %s %u %u|", historique[i].winner,
             (strcmp(historique[i].winner, historique[i].player1) == 0)
                 ? historique[i].player2
                 : historique[i].player1,
             historique[i].score_player1, historique[i].score_player2);

    strcat(history_message, line);
  }
  strcat(history_message, "\n");

  // Envoie le message complet
  write(socket, history_message, strlen(history_message));

  pthread_mutex_unlock(&historique_mutex);
}

void changer_bio(int socket, const char *bio_text) {
  pthread_mutex_lock(&clients_mutex);
  for (int i = 0; i < nb_clients; i++) {
    if (clients[i].socket == socket) {
      // Mettre à jour la bio
      strncpy(clients[i].bio, bio_text, MAX_MESSAGE_LENGTH - 1);
      clients[i].bio[MAX_MESSAGE_LENGTH - 1] = '\0';
      char response[BUFFER_SIZE];
      snprintf(response, BUFFER_SIZE,
               "BIO_UPDATE %sBio mise à jour avec succès!%s\n", GREEN_TEXT,
               RESET_COLOR);
      write(socket, response, strlen(response));
      pthread_mutex_unlock(&clients_mutex);
      return;
    }
  }
  pthread_mutex_unlock(&clients_mutex);

  char error_buffer[BUFFER_SIZE];
  snprintf(error_buffer, BUFFER_SIZE,
           "ERROR %sErreur lors de la mise à jour de la bio%s\n", RED_TEXT,
           RESET_COLOR);
  write(socket, error_buffer, strlen(error_buffer));
}

void regarder_bio(int socket, const char *target_pseudo) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < nb_clients; i++) {
        if (strcmp(clients[i].pseudo, target_pseudo) == 0) {
            char response[BUFFER_SIZE];
            if (strlen(clients[i].bio) == 0) {
                snprintf(response, BUFFER_SIZE, "BIO %s n'a pas encore de bio.\n",
                        target_pseudo);
            } else {
                // Limiter la taille de la bio pour éviter la troncature
                char bio_tronquee[BUFFER_SIZE - 100]; // Réserver de l'espace pour le reste du message
                strncpy(bio_tronquee, clients[i].bio, sizeof(bio_tronquee) - 1);
                bio_tronquee[sizeof(bio_tronquee) - 1] = '\0';
                
                snprintf(response, BUFFER_SIZE, "BIO Bio de %s%s%s: %s\n", 
                        GREEN_TEXT, target_pseudo, RESET_COLOR, bio_tronquee);
            }
            write(socket, response, strlen(response));
            pthread_mutex_unlock(&clients_mutex);
            return;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    char error_msg[BUFFER_SIZE];
    snprintf(error_msg, BUFFER_SIZE, "ERROR %sJoueur %s non trouvé%s\n", 
             RED_TEXT, target_pseudo, RESET_COLOR);
    write(socket, error_msg, strlen(error_msg));
}

void ajouter_observateur_prive(int socket, const char *target_pseudo) {
  pthread_mutex_lock(&clients_mutex);
  Client *owner = NULL;
  Client *target = NULL;

  // Trouver le propriétaire et la cible
  for (int i = 0; i < nb_clients; i++) {
    if (clients[i].socket == socket) {
      owner = &clients[i];
    }
    if (strcmp(clients[i].pseudo, target_pseudo) == 0) { // Retiré le else
      target = &clients[i];
    }
  }

  if (!owner || !target) {
    char error_msg[BUFFER_SIZE];
    snprintf(error_msg, BUFFER_SIZE, "ERROR %sJoueur non trouvé%s\n", RED_TEXT,
             RESET_COLOR);
    write(socket, error_msg, strlen(error_msg));
    pthread_mutex_unlock(&clients_mutex);
    return;
  }

  // Verifier que la cible n'est pas le proprietaire
  if (strcmp(owner->pseudo, target_pseudo) == 0) {
    char error_msg[BUFFER_SIZE];
    snprintf(
        error_msg, BUFFER_SIZE,
        "ERROR %sVous ne pouvez pas vous ajouter comme observateur privé%s\n",
        RED_TEXT, RESET_COLOR);
    write(socket, error_msg, strlen(error_msg));
    pthread_mutex_unlock(&clients_mutex);
    return;
  }

  // Vérifier si l'observateur est déjà dans la liste
  for (int i = 0; i < owner->nb_private_observers; i++) {
    if (strcmp(owner->private_observers[i], target_pseudo) == 0) {
      char error_msg[BUFFER_SIZE];
      snprintf(error_msg, BUFFER_SIZE,
               "ERROR %s%s est déjà dans votre liste d'observateurs privés%s\n",
               RED_TEXT, target_pseudo, RESET_COLOR);
      write(socket, error_msg, strlen(error_msg));
      pthread_mutex_unlock(&clients_mutex);
      return;
    }
  }

  // Vérifier si la liste n'est pas pleine
  if (owner->nb_private_observers >= MAX_PRIVATE_OBSERVERS) {
    char error_msg[BUFFER_SIZE];
    snprintf(error_msg, BUFFER_SIZE,
             "ERROR %sListe d'observateurs privés pleine%s\n", RED_TEXT,
             RESET_COLOR);
    write(socket, error_msg, strlen(error_msg));
    pthread_mutex_unlock(&clients_mutex);
    return;
  }

  // Ajouter l'observateur
  strncpy(owner->private_observers[owner->nb_private_observers], target_pseudo,
          MAX_PSEUDO_LENGTH - 1);
  owner->private_observers[owner->nb_private_observers][MAX_PSEUDO_LENGTH - 1] =
      '\0';
  owner->nb_private_observers++;

  char success_msg[BUFFER_SIZE];
  snprintf(success_msg, BUFFER_SIZE,
           "PRIVATE_OBSERVER_ADDED %s%s a été ajouté à votre liste "
           "d'observateurs privés%s\n",
           GREEN_TEXT, target_pseudo, RESET_COLOR);
  write(socket, success_msg, strlen(success_msg));

  pthread_mutex_unlock(&clients_mutex);
}

void retirer_observateur_prive(int socket, const char *target_pseudo) {
  pthread_mutex_lock(&clients_mutex);
  Client *owner = NULL;
  int found = 0;

  // Trouver le propriétaire
  for (int i = 0; i < nb_clients; i++) {
    if (clients[i].socket == socket) {
      owner = &clients[i];
      break;
    }
  }

  if (!owner) {
    char error_msg[BUFFER_SIZE];
    snprintf(error_msg, BUFFER_SIZE, "ERROR %sErreur interne%s\n", RED_TEXT,
             RESET_COLOR);
    write(socket, error_msg, strlen(error_msg));
    pthread_mutex_unlock(&clients_mutex);
    return;
  }

  // Chercher et retirer l'observateur
  for (int i = 0; i < owner->nb_private_observers; i++) {
    if (strcmp(owner->private_observers[i], target_pseudo) == 0) {
      // Décaler les observateurs suivants
      for (int j = i; j < owner->nb_private_observers - 1; j++) {
        strncpy(owner->private_observers[j], owner->private_observers[j + 1],
                MAX_PSEUDO_LENGTH);
      }
      owner->nb_private_observers--;
      found = 1;
      break;
    }
  }

  if (!found) {
    char error_msg[BUFFER_SIZE];
    snprintf(error_msg, BUFFER_SIZE,
             "ERROR %s%s n'est pas dans votre liste d'observateurs privés%s\n",
             RED_TEXT, target_pseudo, RESET_COLOR);
    write(socket, error_msg, strlen(error_msg));
  } else {
    char success_msg[BUFFER_SIZE];
    snprintf(success_msg, BUFFER_SIZE,
             "PRIVATE_OBSERVER_REMOVED %s%s a été retiré de votre liste "
             "d'observateurs privés%s\n",
             GREEN_TEXT, target_pseudo, RESET_COLOR);
    write(socket, success_msg, strlen(success_msg));
  }

  pthread_mutex_unlock(&clients_mutex);
}

void lister_observateurs_prives(int socket) {
  pthread_mutex_lock(&clients_mutex);
  int owner_index = -1;

  // Trouver le propriétaire
  for (int i = 0; i < nb_clients; i++) {
    if (clients[i].socket == socket) {
      owner_index = i;
      break;
    }
  }

  if (owner_index == -1) {
    pthread_mutex_unlock(&clients_mutex);
    char error_msg[BUFFER_SIZE];
    snprintf(error_msg, BUFFER_SIZE, "ERROR %sErreur interne%s\n", RED_TEXT,
             RESET_COLOR);
    write(socket, error_msg, strlen(error_msg));
    return;
  }

  // Préparer la liste
  char response[BUFFER_SIZE] = "PRIVATE_OBSERVERS ";
  if (clients[owner_index].nb_private_observers == 0) {
    strcat(response, "Aucun observateur privé dans votre liste, vous pouvez en "
                     "ajouter avec /add-private-observer <pseudo>\n");
  } else {
    printf("je suis dans le cas où il y a des observateurs privés\n");
    for (int i = 0; i < clients[owner_index].nb_private_observers; i++) {
      strcat(response, clients[owner_index].private_observers[i]);
      if (i < clients[owner_index].nb_private_observers - 1) {
        strcat(response, ", ");
      }
    }
  }
  strcat(response, "\n");
  printf("response : %s\n", response);
  write(socket, response, strlen(response));

  pthread_mutex_unlock(&clients_mutex);
}

// mode 0 = public, mode 1 = privé
void changer_mode_visibilite(int socket, int mode) {
  pthread_mutex_lock(&clients_mutex);

  // Trouver le client
  for (int i = 0; i < nb_clients; i++) {
    if (clients[i].socket == socket) {
      clients[i].is_private = mode;

      char success_msg[BUFFER_SIZE];
      if (mode == 0) {
        snprintf(
            success_msg, BUFFER_SIZE,
            "VISIBILITY_CHANGED Votre mode de visibilité a été changé en %s"
            "publique%s\n",
            GREEN_TEXT, RESET_COLOR);
      } else if (mode == 1) {
        snprintf(
            success_msg, BUFFER_SIZE,
            "VISIBILITY_CHANGED Votre mode de visibilité a été changé en %s"
            "privé%s\n",
            RED_TEXT, RESET_COLOR);
      }

      pthread_mutex_unlock(&clients_mutex);
      write(socket, success_msg, strlen(success_msg));
      return;
    }
  }

  // Si on arrive ici, le client n'a pas été trouvé
  char error_msg[BUFFER_SIZE];
  snprintf(error_msg, BUFFER_SIZE,
           "ERROR %sErreur lors du changement de mode%s\n", RED_TEXT,
           RESET_COLOR);
  write(socket, error_msg, strlen(error_msg));

  pthread_mutex_unlock(&clients_mutex);
}

void envoyer_message(int socket, char *buffer) {
    char destinataire[MAX_PSEUDO_LENGTH];
    char message[BUFFER_SIZE - MAX_PSEUDO_LENGTH - 100]; // Réserver de l'espace pour le formatage
    char expediteur[MAX_PSEUDO_LENGTH];

    // Extraire le destinataire et le message
    if (sscanf(buffer, "MESSAGE %s %[^\n]", destinataire, message) != 2) {
        char error_buffer[BUFFER_SIZE];
        snprintf(error_buffer, BUFFER_SIZE,
                "ERROR %sFormat de message incorrect%s\n", RED_TEXT, RESET_COLOR);
        write(socket, error_buffer, strlen(error_buffer));
        return;
    }

    // Trouver le pseudo de l'expéditeur
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < nb_clients; i++) {
        if (clients[i].socket == socket) {
            strncpy(expediteur, clients[i].pseudo, MAX_PSEUDO_LENGTH - 1);
            expediteur[MAX_PSEUDO_LENGTH - 1] = '\0';
            break;
        }
    }

    // Vérifier si le message est pour tous les clients
    int allClients = (strcmp(destinataire, "all") == 0);
    int destinataireTrouve = 0;

    for (int i = 0; i < nb_clients; i++) {
        if (strcmp(clients[i].pseudo, destinataire) == 0 || allClients) {
            char formatted_message[BUFFER_SIZE];
            snprintf(formatted_message, BUFFER_SIZE,
                    "MESSAGE message de %s%s%s: %.*s\n", 
                    GREEN_TEXT, expediteur, RESET_COLOR,
                    (int)(BUFFER_SIZE - 100), message); // Limiter la taille du message

            write(clients[i].socket, formatted_message, strlen(formatted_message));
            destinataireTrouve = 1;

            if (!allClients) break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    if (!destinataireTrouve) {
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE,
                "MESSAGE %sErreur: Le destinataire %s n'existe pas%s\n", 
                RED_TEXT, destinataire, RESET_COLOR);
        write(socket, error_msg, strlen(error_msg));
    }
}

void envoyer_parties_actives(int socket) {
  char buffer[BUFFER_SIZE] = "GAMES";
  pthread_mutex_lock(&games_mutex);
  for (int i = 0; i < nb_games; i++) {
    if (!games[i].jeu.fini) {
      char game_info[200];
      snprintf(game_info, 200, " [%d]%s-%s", i, games[i].player1,
               games[i].player2);
      strcat(buffer, game_info);
      // Nettoyer le buffer temporaire
      memset(game_info, 0, 200);
    }
  }
  pthread_mutex_unlock(&games_mutex);
  write(socket, buffer, strlen(buffer));

  // Nettoyer le buffer principal
  memset(buffer, 0, BUFFER_SIZE);
}

void creation_partie(Game *game, char *player1, char *player2, int socket1,
                     int socket2) {
  // Copie les noms des joueurs
  strncpy(game->player1, player1, MAX_PSEUDO_LENGTH - 1);
  strncpy(game->player2, player2, MAX_PSEUDO_LENGTH - 1);
  game->player1[MAX_PSEUDO_LENGTH - 1] = '\0';
  game->player2[MAX_PSEUDO_LENGTH - 1] = '\0';

  // Enregistre les sockets
  game->socket1 = socket1;
  game->socket2 = socket2;

  // Réinitialise la liste des observateurs
  game->nb_observers = 0;

  // Initialise le jeu
  init_awale(&game->jeu, player1, player2);
}

// Ajouter cette fonction pour retirer un observateur d'une partie
void retirer_observateur(Game *game, int socket) {
  for (int i = 0; i < game->nb_observers; i++) {
    if (game->observers[i] == socket) {
      // Décaler tous les observateurs suivants
      for (int j = i; j < game->nb_observers - 1; j++) {
        game->observers[j] = game->observers[j + 1];
      }
      game->nb_observers--;
      break;
    }
  }
}

// Ajouter cette fonction pour envoyer l'état du jeu uniquement à un observateur
void envoyer_partie_observeurs(Game *game, int observer_socket) {
  char full_state[BUFFER_SIZE];

  snprintf(
      full_state, BUFFER_SIZE,
      "GAMESTATE %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %s %s ",
      game->jeu.plateau[0], game->jeu.plateau[1], game->jeu.plateau[2],
      game->jeu.plateau[3], game->jeu.plateau[4], game->jeu.plateau[5],
      game->jeu.plateau[6], game->jeu.plateau[7], game->jeu.plateau[8],
      game->jeu.plateau[9], game->jeu.plateau[10], game->jeu.plateau[11],
      game->jeu.scoreJ1, game->jeu.scoreJ2, game->jeu.joueurCourant,
      game->jeu.fini, game->jeu.gagnant, game->jeu.pseudo1, game->jeu.pseudo2);

  write(observer_socket, full_state, strlen(full_state));
}

// Vérifie si un défi est en attente entre deux joueurs
int defi_existe(const char *challenger, const char *challenged) {
  pthread_mutex_lock(&challenges_mutex);
  for (int i = 0; i < nb_challenges; i++) {
    // Vérifie dans les deux sens
    if ((strcmp(pending_challenges[i].challenger, challenger) == 0 &&
         strcmp(pending_challenges[i].challenged, challenged) == 0) ||
        (strcmp(pending_challenges[i].challenger, challenged) == 0 &&
         strcmp(pending_challenges[i].challenged, challenger) == 0)) {
      pthread_mutex_unlock(&challenges_mutex);
      return 1;
    }
  }
  pthread_mutex_unlock(&challenges_mutex);
  return 0;
}

// Ajoute un nouveau défi
void ajouter_defi(const char *challenger, const char *challenged) {
  pthread_mutex_lock(&challenges_mutex);
  if (nb_challenges < MAX_CLIENTS) {
    strncpy(pending_challenges[nb_challenges].challenger, challenger,
            MAX_PSEUDO_LENGTH - 1);
    strncpy(pending_challenges[nb_challenges].challenged, challenged,
            MAX_PSEUDO_LENGTH - 1);
    pending_challenges[nb_challenges].challenger[MAX_PSEUDO_LENGTH - 1] = '\0';
    pending_challenges[nb_challenges].challenged[MAX_PSEUDO_LENGTH - 1] = '\0';
    nb_challenges++;
  }
  pthread_mutex_unlock(&challenges_mutex);
}

// Supprime un défi
void supprimer_defi(const char *challenger, const char *challenged) {
  pthread_mutex_lock(&challenges_mutex);
  for (int i = 0; i < nb_challenges; i++) {
    if (strcmp(pending_challenges[i].challenger, challenger) == 0 &&
        strcmp(pending_challenges[i].challenged, challenged) == 0) {
      // Déplacer le dernier défi à cette position
      if (i < nb_challenges - 1) {
        pending_challenges[i] = pending_challenges[nb_challenges - 1];
      }
      nb_challenges--;
      break;
    }
  }
  pthread_mutex_unlock(&challenges_mutex);
}

// Nouvelle fonction pour retirer un joueur de toutes les parties qu'il observe
void retirer_observateur_toutes_parties(int socket) {
    pthread_mutex_lock(&games_mutex);
    for (int i = 0; i < nb_games; i++) {
        if (!games[i].jeu.fini) { // On ne vérifie que les parties en cours
            retirer_observateur(&games[i], socket);
        }
    }
    pthread_mutex_unlock(&games_mutex);
}

void accepter_partie(int socket, const char *buffer) {
  char challenger[MAX_PSEUDO_LENGTH];
  char accepter_pseudo[MAX_PSEUDO_LENGTH];
  sscanf(buffer, "ACCEPT %s", challenger);

  pthread_mutex_lock(&clients_mutex);
  
  // Trouver le joueur qui accepte et vérifier s'il est déjà en jeu
  int is_playing = 0;
  for (int i = 0; i < nb_clients; i++) {
    if (clients[i].socket == socket) {
      strncpy(accepter_pseudo, clients[i].pseudo, MAX_PSEUDO_LENGTH - 1);
      accepter_pseudo[MAX_PSEUDO_LENGTH - 1] = '\0';
      is_playing = clients[i].is_playing;
      break;
    }
  }

  // Si le joueur est déjà en partie, on refuse l'acceptation
  if (is_playing) {
    char error_buffer[BUFFER_SIZE];
    snprintf(error_buffer, BUFFER_SIZE,
             "ERROR %sVous ne pouvez pas accepter de défi pendant une partie%s\n", 
             RED_TEXT, RESET_COLOR);
    write(socket, error_buffer, strlen(error_buffer));
    pthread_mutex_unlock(&clients_mutex);
    return;
  }

  // Vérifier si le challenger est en jeu
  for (int i = 0; i < nb_clients; i++) {
    if (strcmp(clients[i].pseudo, challenger) == 0 && clients[i].is_playing) {
      char error_buffer[BUFFER_SIZE];
      snprintf(error_buffer, BUFFER_SIZE,
               "ERROR %s%s est déjà en partie%s\n", 
               RED_TEXT, challenger, RESET_COLOR);
      write(socket, error_buffer, strlen(error_buffer));
      pthread_mutex_unlock(&clients_mutex);
      return;
    }
  }
  pthread_mutex_unlock(&clients_mutex);

  if (!defi_existe(challenger, accepter_pseudo)) {
    char error_buffer[BUFFER_SIZE];
    snprintf(error_buffer, BUFFER_SIZE,
             "ERROR %sAucun défi en attente de ce joueur%s\n", 
             RED_TEXT, RESET_COLOR);
    write(socket, error_buffer, strlen(error_buffer));
    return;
  }

    // Retirer le joueur de toutes les parties qu'il observe
    retirer_observateur_toutes_parties(socket);

    pthread_mutex_lock(&clients_mutex);
    pthread_mutex_lock(&games_mutex);

  int challenger_socket = -1;
  for (int i = 0; i < nb_clients; i++) {
    if (strcmp(clients[i].pseudo, challenger) == 0) {
      challenger_socket = clients[i].socket;
      clients[i].is_playing = 1;
      clients[i].game_id = nb_games;
      break;
    }
  }

  for (int i = 0; i < nb_clients; i++) {
    if (clients[i].socket == socket) {
      clients[i].is_playing = 1;
      clients[i].game_id = nb_games;
      break;
    }
  }

  Game *game = &games[nb_games++];
  creation_partie(game, challenger, accepter_pseudo, challenger_socket, socket);
  pthread_mutex_unlock(&games_mutex);
  pthread_mutex_unlock(&clients_mutex);

  supprimer_defi(challenger, accepter_pseudo);
  diffuser_etat_partie(game);
}

void gerer_forfait(int socket) {
  pthread_mutex_lock(&clients_mutex);
  int game_id = -1;
  char forfeiteur[MAX_PSEUDO_LENGTH];
  // Trouver le joueur qui abandonne
  for (int i = 0; i < nb_clients; i++) {
    if (clients[i].socket == socket && clients[i].is_playing) {
      game_id = clients[i].game_id;
      strncpy(forfeiteur, clients[i].pseudo, MAX_PSEUDO_LENGTH - 1);
      forfeiteur[MAX_PSEUDO_LENGTH - 1] = '\0';
      break;
    }
  }
  pthread_mutex_unlock(&clients_mutex);

  if (game_id != -1) {
    pthread_mutex_lock(&games_mutex);
    Game *game = &games[game_id];

    // Déterminer qui abandonne et qui gagne
    if (socket == game->socket1) {
      game->jeu.scoreJ2 = 25; // Le joueur 2 gagne
      game->jeu.scoreJ1 = 0;  // Le joueur 1 perd
      game->jeu.gagnant = 2;
      mettre_a_jour_elo(game->player2, game->player1);  // Mettre à jour les scores ELO
      // Ajouter à l'historique avec le bon gagnant
      char history_buffer[BUFFER_SIZE];
      snprintf(history_buffer, BUFFER_SIZE, "ADD_HISTORY %s %s %d %d",
               game->player2, game->player1, // Le joueur 2 est le gagnant
               game->jeu.scoreJ2, game->jeu.scoreJ1);
      ajouter_historique(history_buffer);
    } else {
      game->jeu.scoreJ1 = 25; // Le joueur 1 gagne
      game->jeu.scoreJ2 = 0;  // Le joueur 2 perd
      game->jeu.gagnant = 1;
      mettre_a_jour_elo(game->player1, game->player2);  // Mettre à jour les scores ELO
      // Ajouter à l'historique avec le bon gagnant
      char history_buffer[BUFFER_SIZE];
      snprintf(history_buffer, BUFFER_SIZE, "ADD_HISTORY %s %s %d %d",
               game->player1, game->player2, // Le joueur 1 est le gagnant
               game->jeu.scoreJ1, game->jeu.scoreJ2);
      ajouter_historique(history_buffer);
    }

    game->jeu.fini = 1;
    diffuser_etat_partie(game);

    // Mettre à jour le statut des joueurs
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < nb_clients; i++) {
      if (clients[i].game_id == game_id) {
        clients[i].is_playing = 0;
        clients[i].game_id = -1;
      }
    }
    pthread_mutex_unlock(&clients_mutex);

    pthread_mutex_unlock(&games_mutex);
  }
}

void gerer_move(int socket, const char *buffer) {
  int move;
  sscanf(buffer, "MOVE %d", &move);

  pthread_mutex_lock(&clients_mutex);
  int game_id = -1;
  for (int i = 0; i < nb_clients; i++) {
    if (clients[i].socket == socket && clients[i].is_playing) {
      game_id = clients[i].game_id;
      break;
    }
  }
  pthread_mutex_unlock(&clients_mutex);

  if (game_id != -1) {
    pthread_mutex_lock(&games_mutex);
    Game *game = &games[game_id];

    if (!game->jeu.fini) { // Vérifier si la partie n'est pas déjà finie
      // Vérifier si c'est le bon joueur
      unsigned int player_num = (socket == game->socket1) ? 1 : 2;
      if (player_num == game->jeu.joueurCourant) {
        // Jouer le coup avec awale_v2.c
        if (jouer_coup(&game->jeu, move)) {
          diffuser_etat_partie(game);
        } else {
          char error_msg[BUFFER_SIZE];
          snprintf(error_msg, BUFFER_SIZE, "ERROR %sCoup invalide%s\n",
                   RED_TEXT, RESET_COLOR);
          write(socket, error_msg, strlen(error_msg));
        }
      } else {
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "ERROR %sCe n'est pas votre tour%s\n",
                 RED_TEXT, RESET_COLOR);
        write(socket, error_msg, strlen(error_msg));
      }
    }
    pthread_mutex_unlock(&games_mutex);
  }
}

void defier(int socket, char *buffer) {
    char challenged_pseudo[MAX_PSEUDO_LENGTH];
    if (sscanf(buffer, "CHALLENGE %s", challenged_pseudo) != 1) {
        return;
    }

    pthread_mutex_lock(&clients_mutex);
    pthread_mutex_lock(&challenges_mutex);

    // Trouver le challenger
    char challenger_pseudo[MAX_PSEUDO_LENGTH];
    int challenger_found = 0;
    for (int i = 0; i < nb_clients; i++) {
        if (clients[i].socket == socket) {
            strncpy(challenger_pseudo, clients[i].pseudo, MAX_PSEUDO_LENGTH - 1);
            challenger_pseudo[MAX_PSEUDO_LENGTH - 1] = '\0';
            challenger_found = 1;
            break;
        }
    }

    if (!challenger_found) {
        pthread_mutex_unlock(&challenges_mutex);
        pthread_mutex_unlock(&clients_mutex);
        return;
    }

    // Vérifier si le joueur essaie de se défier lui-même
    if (strcmp(challenger_pseudo, challenged_pseudo) == 0) {
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, 
                "ERROR %sVous ne pouvez pas vous défier vous-même%s\n", 
                RED_TEXT, RESET_COLOR);
        write(socket, error_msg, strlen(error_msg));
        pthread_mutex_unlock(&challenges_mutex);
        pthread_mutex_unlock(&clients_mutex);
        return;
    }

    // Vérifier si le joueur défié existe et s'il est disponible
    int challenged_found = 0;
    int challenged_socket = -1;
    int is_playing = 0;
    
    // Chercher le joueur défié
    for (int i = 0; i < nb_clients; i++) {
        if (strcmp(clients[i].pseudo, challenged_pseudo) == 0) {
            challenged_found = 1;
            challenged_socket = clients[i].socket;
            is_playing = clients[i].is_playing;
            break;
        }
    }

    

    // Si le joueur n'est pas trouvé
    if (!challenged_found) {
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "ERROR %sJoueur non trouvé%s\n", 
                RED_TEXT, RESET_COLOR);
        write(socket, error_msg, strlen(error_msg));
        pthread_mutex_unlock(&challenges_mutex);
        pthread_mutex_unlock(&clients_mutex);
        return;
    }

    // Si le joueur est déjà en partie
    if (is_playing) {
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "ERROR %s%s est déjà en partie%s\n", 
                RED_TEXT, challenged_pseudo, RESET_COLOR);
        write(socket, error_msg, strlen(error_msg));
        pthread_mutex_unlock(&challenges_mutex);
        pthread_mutex_unlock(&clients_mutex);
        return;
    }

    // Envoyer le défi
    char challenge_msg[BUFFER_SIZE];
    snprintf(challenge_msg, BUFFER_SIZE, "CHALLENGE_FROM %s", challenger_pseudo);
    write(challenged_socket, challenge_msg, strlen(challenge_msg));

    // Ajouter le défi à la liste des défis en attente
    if (nb_challenges < MAX_CLIENTS) {
        strncpy(pending_challenges[nb_challenges].challenger, challenger_pseudo, MAX_PSEUDO_LENGTH - 1);
        strncpy(pending_challenges[nb_challenges].challenged, challenged_pseudo, MAX_PSEUDO_LENGTH - 1);
        pending_challenges[nb_challenges].challenger[MAX_PSEUDO_LENGTH - 1] = '\0';
        pending_challenges[nb_challenges].challenged[MAX_PSEUDO_LENGTH - 1] = '\0';
        nb_challenges++;
    }

    pthread_mutex_unlock(&challenges_mutex);
    pthread_mutex_unlock(&clients_mutex);
}

void observer_partie(int socket, const char *buffer) {
  int game_id;
  sscanf(buffer, "OBSERVE %d", &game_id);

  // Vérifier si le joueur est déjà en partie
  pthread_mutex_lock(&clients_mutex);
  int is_playing = 0;
  for (int i = 0; i < nb_clients; i++) {
    if (clients[i].socket == socket && clients[i].is_playing) {
      is_playing = 1;
      break;
    }
  }
  pthread_mutex_unlock(&clients_mutex);

  if (is_playing) {
    char error_buffer[BUFFER_SIZE];
    snprintf(error_buffer, BUFFER_SIZE,
             "ERROR %sVous ne pouvez pas observer une partie pendant que "
             "vous jouez%s\n",
             RED_TEXT, RESET_COLOR);
    write(socket, error_buffer, strlen(error_buffer));
    return;
  }

  pthread_mutex_lock(&games_mutex);

  // Vérifier si l'ID de la partie est valide
  if (game_id < 0 || game_id >= nb_games) {
    pthread_mutex_unlock(&games_mutex);
    char error_msg[BUFFER_SIZE];
    snprintf(error_msg, BUFFER_SIZE,
             "ERROR %sLa partie que vous essayez d'observer n'existe pas%s\n",
             RED_TEXT, RESET_COLOR);
    write(socket, error_msg, strlen(error_msg));
    return;
  }

  // Vérifier si la partie est terminée
  if (games[game_id].jeu.fini) {
    pthread_mutex_unlock(&games_mutex);
    char error_msg[BUFFER_SIZE];
    snprintf(error_msg, BUFFER_SIZE,
             "ERROR %sLa partie que vous essayez d'observer est terminée%s\n",
             RED_TEXT, RESET_COLOR);
    write(socket, error_msg, strlen(error_msg));
    return;
  }

  if (game_id >= 0 && game_id < nb_games && !games[game_id].jeu.fini) {
    // Trouver les clients correspondant aux joueurs de la partie
    pthread_mutex_lock(&clients_mutex);
    Client *player1 = NULL;
    Client *player2 = NULL;
    Client *observer = NULL;

    // Trouver l'observateur et les joueurs
    for (int i = 0; i < nb_clients; i++) {
      if (clients[i].socket == socket) {
        observer = &clients[i];
      }
      if (strcmp(clients[i].pseudo, games[game_id].player1) == 0) {
        player1 = &clients[i];
      }
      if (strcmp(clients[i].pseudo, games[game_id].player2) == 0) {
        player2 = &clients[i];
      }
    }

    int can_observe = 1;
    if (player1 && player1->is_private) {
      // Vérifier si l'observateur est dans la liste des observateurs
      // privés du joueur 1
      int found = 0;
      for (int i = 0; i < player1->nb_private_observers; i++) {
        if (strcmp(player1->private_observers[i], observer->pseudo) == 0) {
          found = 1;
          break;
        }
      }
      if (!found) {
        can_observe = 0;
      }
    }

    if (can_observe && player2 && player2->is_private) {
      // Vérifier si l'observateur est dans la liste des observateurs privés
      // du joueur 2
      int found = 0;
      for (int i = 0; i < player2->nb_private_observers; i++) {
        if (strcmp(player2->private_observers[i], observer->pseudo) == 0) {
          found = 1;
          break;
        }
      }
      if (!found) {
        can_observe = 0;
      }
    }

    if (can_observe) {
      games[game_id].observers[games[game_id].nb_observers++] = socket;
      envoyer_partie_observeurs(&games[game_id], socket);
      char success_msg[BUFFER_SIZE];
      snprintf(success_msg, BUFFER_SIZE,
               "OBSERVE_OK %sVous observez maintenant la partie %d%s\n",
               GREEN_TEXT, game_id, RESET_COLOR);
      write(socket, success_msg, strlen(success_msg));
    } else {
      char error_msg[BUFFER_SIZE];
      snprintf(error_msg, BUFFER_SIZE,
               "ERROR %sVous n'avez pas la permission d'observer cette "
               "partie privée%s\n",
               RED_TEXT, RESET_COLOR);
      write(socket, error_msg, strlen(error_msg));
    }

    pthread_mutex_unlock(&clients_mutex);
  }
  pthread_mutex_unlock(&games_mutex);
}

void mettre_a_jour_elo(const char *gagnant, const char *perdant) {
    pthread_mutex_lock(&clients_mutex);
    
    Client *winner = NULL;
    Client *loser = NULL;
    
    // Trouver les deux joueurs
    for (int i = 0; i < nb_clients; i++) {
        if (strcmp(clients[i].pseudo, gagnant) == 0) {
            winner = &clients[i];
        }
        if (strcmp(clients[i].pseudo, perdant) == 0) {
            loser = &clients[i];
        }
    }
    
    if (winner && loser) {
        // Calculer la probabilité de victoire attendue
        double expected_winner = 1.0 / (1.0 + pow(10, (loser->elo - winner->elo) / 400.0));
        double expected_loser = 1.0 / (1.0 + pow(10, (winner->elo - loser->elo) / 400.0));
        
        // Mettre à jour les scores ELO
        winner->elo += (int)(K_FACTOR * (1 - expected_winner));
        loser->elo += (int)(K_FACTOR * (0 - expected_loser));
        
        // S'assurer que les scores ne descendent pas en dessous de 0
        if (winner->elo < 0) winner->elo = 0;
        if (loser->elo < 0) loser->elo = 0;
    }
    
    pthread_mutex_unlock(&clients_mutex);
}

// Nouvelle fonction pour supprimer tous les défis d'un joueur
void supprimer_defis_joueur(const char *pseudo) {
    pthread_mutex_lock(&challenges_mutex);
    
    // Parcourir tous les défis et supprimer ceux où le joueur est impliqué
    for (int i = 0; i < nb_challenges; i++) {
        if (strcmp(pending_challenges[i].challenger, pseudo) == 0 || 
            strcmp(pending_challenges[i].challenged, pseudo) == 0) {
            // Déplacer le dernier défi à cette position
            if (i < nb_challenges - 1) {
                pending_challenges[i] = pending_challenges[nb_challenges - 1];
                i--; // Pour revérifier cette position au prochain tour
            }
            nb_challenges--;
        }
    }
    
    pthread_mutex_unlock(&challenges_mutex);
}

void *gerer_client(void *arg) {
  int socket = *(int *)arg;
  char buffer[BUFFER_SIZE];
  char pseudo[MAX_PSEUDO_LENGTH];
  free(arg);

  // Boucle pour la saisie du pseudo
  while (1) {
    // Wait for pseudo
    memset(buffer, 0, BUFFER_SIZE);
    ssize_t bytes_read = recv(socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_read <= 0) {
      close(socket);
      return NULL;
    }
    buffer[bytes_read] = '\0';

    if (sscanf(buffer, "LOGIN %s", pseudo) != 1) {
      char error_buffer[BUFFER_SIZE];
      snprintf(error_buffer, BUFFER_SIZE,
               "ERROR %sFormat de connexion invalide%s\n", RED_TEXT,
               RESET_COLOR);
      send(socket, error_buffer, strlen(error_buffer), 0);
      continue; // Redemander un pseudo
    }

    // Check if pseudo already exists
    pthread_mutex_lock(&clients_mutex);
    int pseudo_exists = 0;
    for (int i = 0; i < nb_clients; i++) {
      if (strcmp(clients[i].pseudo, pseudo) == 0) {
        pseudo_exists = 1;
        break;
      }
    }

    if (pseudo_exists) {
      pthread_mutex_unlock(&clients_mutex);
      char error_buffer[BUFFER_SIZE];
      snprintf(error_buffer, BUFFER_SIZE, "ERROR %sPseudo déjà utilisé%s\n",
               RED_TEXT, RESET_COLOR);
      send(socket, error_buffer, strlen(error_buffer), 0);
      continue; // Redemander un pseudo
    }

    // Add client to list
    Client *client = &clients[nb_clients++];
    client->socket = socket;
    strncpy(client->pseudo, pseudo, MAX_PSEUDO_LENGTH - 1);
    client->pseudo[MAX_PSEUDO_LENGTH - 1] = '\0';
    client->nb_private_observers = 0;
    client->elo = ELO_INITIAL;  // Initialiser le score ELO

    for (int i = 0; i < MAX_PRIVATE_OBSERVERS; i++) {
      client->private_observers[i][0] =
          '\0'; // Initialise chaque chaîne comme vide
    }

    client->is_private = 0; // de base en public

    client->bio[0] = '\0';
    client->is_playing = 0;
    client->game_id = -1;
    pthread_mutex_unlock(&clients_mutex);

    // Send confirmation
    const char *success_msg = "LOGIN_OK";
    send(socket, success_msg, strlen(success_msg), 0);
    printf("New client connected: %s\n", pseudo);
    break; // Sortir de la boucle une fois le pseudo accepté
  }

  while (1) {
    int n = read(socket, buffer, BUFFER_SIZE);
    if (n <= 0)
      break;
    buffer[n] = 0;

    if (strncmp(buffer, "LIST", 4) == 0) {
      envoyer_liste_joueurs(socket);
    } else if (strncmp(buffer, "GAMES", 5) == 0) {
      envoyer_parties_actives(socket);
    } else if (strncmp(buffer, "ADD_PRIVATE_OBSERVER", 19) == 0) {
      char target_pseudo[MAX_PSEUDO_LENGTH];
      if (sscanf(buffer, "ADD_PRIVATE_OBSERVER %s", target_pseudo) == 1) {
        ajouter_observateur_prive(socket, target_pseudo);
      }
    } else if (strncmp(buffer, "REMOVE_PRIVATE_OBSERVER", 22) == 0) {
      char target_pseudo[MAX_PSEUDO_LENGTH];
      if (sscanf(buffer, "REMOVE_PRIVATE_OBSERVER %s", target_pseudo) == 1) {
        retirer_observateur_prive(socket, target_pseudo);
      }
    } else if (strncmp(buffer, "PRIVATE_OBSERVERS", 17) == 0) {
      lister_observateurs_prives(socket);
    } else if (strncmp(buffer, "MODE_PUBLIC", 11) == 0) {
      changer_mode_visibilite(socket, 0);
    } else if (strncmp(buffer, "MODE_PRIVATE", 12) == 0) {
      changer_mode_visibilite(socket, 1);
    } else if (strncmp(buffer, "CREATE_BIO", 10) == 0) {
      char bio_text[MAX_MESSAGE_LENGTH];
      if (sscanf(buffer, "CREATE_BIO %[^\n]", bio_text) == 1) {
        changer_bio(socket, bio_text);
      }
    } else if (strncmp(buffer, "CHECK_BIO", 9) == 0) {
      char target_pseudo[MAX_PSEUDO_LENGTH];
      if (sscanf(buffer, "CHECK_BIO %s", target_pseudo) == 1) {
        regarder_bio(socket, target_pseudo);
      }
    }
    if (strncmp(buffer, "CHALLENGE", 9) == 0) {
      defier(socket, buffer);
    } else if (strncmp(buffer, "ACCEPT", 6) == 0) {
      accepter_partie(socket, buffer);
    } else if (strncmp(buffer, "OBSERVE", 7) == 0) {
      observer_partie(socket, buffer);
    } else if (strncmp(buffer, "MESSAGE", 7) == 0) {
      envoyer_message(socket, buffer);
      memset(buffer, 0, BUFFER_SIZE);
    } else if (strncmp(buffer, "MOVE", 4) == 0) {
      gerer_move(socket, buffer);
    } else if (strncmp(buffer, "FORFEIT", 7) == 0) {
      gerer_forfait(socket);
    } else if (strncmp(buffer, "ADD_HISTORY", 11) == 0) {
      ajouter_historique(buffer);
    } else if (strncmp(buffer, "HISTORY", 7) == 0) {
      envoyer_historique_parties(socket);
    }
  }

  // Clean up client
  pthread_mutex_lock(&clients_mutex);
  pthread_mutex_lock(&games_mutex);
  for (int i = 0; i < nb_clients; i++) {
    if (clients[i].socket == socket) {
      // Supprimer tous les défis du joueur
      supprimer_defis_joueur(clients[i].pseudo);
      
      // Si le client était en train d'observer une partie
      if (clients[i].game_id >= 0 && !clients[i].is_playing) {
        retirer_observateur(&games[clients[i].game_id], socket);
      }

      // Supprimer le client de la liste
      for (int j = i; j < nb_clients - 1; j++) {
        clients[j] = clients[j + 1];
      }
      nb_clients--;
      break;
    }
  }
  pthread_mutex_unlock(&games_mutex);
  pthread_mutex_unlock(&clients_mutex);

  close(socket);
  return NULL;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: %s port\n", argv[0]);
    return 1;
  }

  srand(time(NULL));

  int server_socket = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in server_addr;

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(atoi(argv[1]));

  if (bind(server_socket, (struct sockaddr *)&server_addr,
           sizeof(server_addr)) < 0) {
    perror("Bind failed");
    return 1;
  }

  listen(server_socket, 5);
  printf("Server started on port %s\n", argv[1]);

  while (1) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int *client_socket = malloc(sizeof(int));
    *client_socket =
        accept(server_socket, (struct sockaddr *)&client_addr, &client_len);

    pthread_t thread;
    pthread_create(&thread, NULL, gerer_client, client_socket);
    pthread_detach(thread);
  }

  return 0;
}
