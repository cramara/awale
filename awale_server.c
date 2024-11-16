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

Client clients[MAX_CLIENTS];
Game games[MAX_GAMES];
int nb_clients = 0;
int nb_games = 0;
HistoriqueParties historique[MAX_HISTORIQUE];
int nb_historique = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t games_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t historique_mutex = PTHREAD_MUTEX_INITIALIZER;
Challenge pending_challenges[MAX_CLIENTS];
int nb_challenges = 0;
pthread_mutex_t challenges_mutex = PTHREAD_MUTEX_INITIALIZER;

void broadcast_game_state(Game *game) {
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

void send_free_players(int socket) {
  char buffer[BUFFER_SIZE] = "PLAYERS";
  pthread_mutex_lock(&clients_mutex);
  for (int i = 0; i < nb_clients; i++) {
    if (!clients[i].is_playing) {
      strcat(buffer, " ");
      strcat(buffer, clients[i].pseudo);
    }
  }
  pthread_mutex_unlock(&clients_mutex);
  write(socket, buffer, strlen(buffer));

  // Nettoyer le buffer
  memset(buffer, 0, BUFFER_SIZE);
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

void send_history(int socket) {
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

// Regarder la bio d'un joueur
void regarder_bio(int socket, const char *target_pseudo) {
  pthread_mutex_lock(&clients_mutex);
  for (int i = 0; i < nb_clients; i++) {
    if (strcmp(clients[i].pseudo, target_pseudo) == 0) {
      char response[BUFFER_SIZE];
      if (strlen(clients[i].bio) == 0) {
        snprintf(response, BUFFER_SIZE, "BIO %s n'a pas encore de bio.\n",
                 target_pseudo);
      } else {
        snprintf(response, BUFFER_SIZE, "BIO Bio de %s%s%s: %s\n", GREEN_TEXT,
                 target_pseudo, RESET_COLOR, clients[i].bio);
      }
      write(socket, response, strlen(response));
      pthread_mutex_unlock(&clients_mutex);
      return;
    }
  }
  pthread_mutex_unlock(&clients_mutex);

  char error_msg[BUFFER_SIZE];
  snprintf(error_msg, BUFFER_SIZE, "ERROR %sJoueur %s non trouvé%s\n", RED_TEXT,
           target_pseudo, RESET_COLOR);
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

void send_message(int socket, char *buffer) {
  // Format du buffer : "MESSAGE <destinataire> <message>"
  char destinataire[MAX_PSEUDO_LENGTH];
  char message[BUFFER_SIZE];
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
      strcpy(expediteur, clients[i].pseudo);
      break;
    }
  }

  // Trouver le socket du destinataire et envoyer le message
  int destinataireTrouve = 0;
  // Vérifier si le message est pour tous les clients (destinataire = "all")
  int allClients = (strcmp(destinataire, "all") == 0);

  for (int i = 0; i < nb_clients; i++) {
    if (strcmp(clients[i].pseudo, destinataire) == 0 || allClients) {

      // Formater le message avec le pseudo de l'expéditeur
      char formatted_message[BUFFER_SIZE];
      size_t message_len = strlen(message);
      if (message_len >
          BUFFER_SIZE - 100) { // 100 pour la marge des autres éléments
        message[BUFFER_SIZE - 100] = '\0'; // Tronquer le message si trop long
      }
      snprintf(formatted_message, BUFFER_SIZE,
               "MESSAGE message de %s%s%s: %s\n", GREEN_TEXT, expediteur,
               RESET_COLOR, message);

      // Envoyer le message (ecrire le message chez le destinataire)
      write(clients[i].socket, formatted_message, strlen(formatted_message));
      destinataireTrouve = 1;

      // On sort de la boucle si le destinataire est trouvé et que le message
      // n'est pas pour tous
      if (!allClients)
        break;
    }
  }
  pthread_mutex_unlock(&clients_mutex);

  // Si le destinataire n'est pas trouvé, envoyer une erreur à l'expéditeur
  if (destinataireTrouve == 0) {
    char error_msg[BUFFER_SIZE];
    snprintf(error_msg, BUFFER_SIZE,
             "MESSAGE %sErreur Le destinataire %s n'existe pas%s\n", RED_TEXT,
             destinataire, RESET_COLOR);
    write(socket, error_msg, strlen(error_msg));
  }

  // vider le buffer
  memset(buffer, 0, BUFFER_SIZE);
}

void send_active_games(int socket) {
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

void init_game(Game *game, char *player1, char *player2, int socket1,
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
void send_game_state_to_observer(Game *game, int observer_socket) {
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
int challenge_exists(const char *challenger, const char *challenged) {
  pthread_mutex_lock(&challenges_mutex);
  for (int i = 0; i < nb_challenges; i++) {
    if (strcmp(pending_challenges[i].challenger, challenger) == 0 &&
        strcmp(pending_challenges[i].challenged, challenged) == 0) {
      pthread_mutex_unlock(&challenges_mutex);
      return 1;
    }
  }
  pthread_mutex_unlock(&challenges_mutex);
  return 0;
}

// Ajoute un nouveau défi
void add_challenge(const char *challenger, const char *challenged) {
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
void remove_challenge(const char *challenger, const char *challenged) {
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

void *handle_client(void *arg) {
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
      send_free_players(socket);
    } else if (strncmp(buffer, "GAMES", 5) == 0) {
      send_active_games(socket);
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
    } else if (strncmp(buffer, "CHALLENGE", 9) == 0) {
      char opponent[MAX_PSEUDO_LENGTH];
      char challenger_pseudo[MAX_PSEUDO_LENGTH];
      sscanf(buffer, "CHALLENGE %s", opponent);
      printf("buffer %s\n", buffer);

      pthread_mutex_lock(&clients_mutex);
      int opponent_socket = -1;
      int challenger_is_playing = 0;

      // Trouver d'abord le pseudo du challenger
      for (int i = 0; i < nb_clients; i++) {
        if (clients[i].socket == socket) {
          strncpy(challenger_pseudo, clients[i].pseudo, MAX_PSEUDO_LENGTH - 1);
          challenger_pseudo[MAX_PSEUDO_LENGTH - 1] = '\0';
          if (clients[i].is_playing) {
            challenger_is_playing = 1;
          }
          break;
        }
      }

      // Vérifier si le joueur essaie de se défier lui-même
      if (strcmp(challenger_pseudo, opponent) == 0) {
        char error_buffer[BUFFER_SIZE];
        snprintf(error_buffer, BUFFER_SIZE,
                 "ERROR %sVous ne pouvez pas vous défier vous-même%s\n",
                 RED_TEXT, RESET_COLOR);
        write(socket, error_buffer, strlen(error_buffer));
        pthread_mutex_unlock(&clients_mutex);
        continue;
      }

      if (challenger_is_playing) {
        char error_buffer[BUFFER_SIZE];
        snprintf(error_buffer, BUFFER_SIZE,
                 "ERROR %sVous ne pouvez pas lancer de défi pendant une "
                 "partie%s\n",
                 RED_TEXT, RESET_COLOR);
        write(socket, error_buffer, strlen(error_buffer));
        pthread_mutex_unlock(&clients_mutex);
        continue;
      }

      // Chercher l'adversaire
      for (int i = 0; i < nb_clients; i++) {
        if (strcmp(clients[i].pseudo, opponent) == 0) {
          if (clients[i].is_playing) {
            char error_buffer[BUFFER_SIZE];
            snprintf(error_buffer, BUFFER_SIZE,
                     "ERROR %sCe joueur est déjà en partie%s\n", RED_TEXT,
                     RESET_COLOR);
            write(socket, error_buffer, strlen(error_buffer));
            opponent_socket = -2; // Pour indiquer qu'on a trouvé le joueur
                                  // mais qu'il est occupé
          } else {
            opponent_socket = clients[i].socket;
          }
          break;
        }
      }
      pthread_mutex_unlock(&clients_mutex);

      if (opponent_socket == -1) {
        char error_buffer[BUFFER_SIZE];
        snprintf(error_buffer, BUFFER_SIZE, "ERROR %sJoueur non trouvé%s\n",
                 RED_TEXT, RESET_COLOR);
        write(socket, error_buffer, strlen(error_buffer));
      } else if (opponent_socket >=
                 0) { // Si le joueur est trouvé et n'est pas en partie
        add_challenge(challenger_pseudo, opponent);
        char challenge[BUFFER_SIZE];
        snprintf(challenge, BUFFER_SIZE, "CHALLENGE_FROM %s",
                 challenger_pseudo);
        write(opponent_socket, challenge, strlen(challenge));
      }
    } else if (strncmp(buffer, "ACCEPT", 6) == 0) {
      char challenger[MAX_PSEUDO_LENGTH];
      char accepter_pseudo[MAX_PSEUDO_LENGTH];
      sscanf(buffer, "ACCEPT %s", challenger);

      // Trouver le pseudo de celui qui accepte
      pthread_mutex_lock(&clients_mutex);
      for (int i = 0; i < nb_clients; i++) {
        if (clients[i].socket == socket) {
          strncpy(accepter_pseudo, clients[i].pseudo, MAX_PSEUDO_LENGTH - 1);
          accepter_pseudo[MAX_PSEUDO_LENGTH - 1] = '\0';
          break;
        }
      }
      pthread_mutex_unlock(&clients_mutex);

      // Vérifier si le défi existe
      if (!challenge_exists(challenger, accepter_pseudo)) {
        char error_buffer[BUFFER_SIZE];
        snprintf(error_buffer, BUFFER_SIZE,
                 "ERROR %sAucun défi en attente de ce joueur%s\n", RED_TEXT,
                 RESET_COLOR);
        write(socket, error_buffer, strlen(error_buffer));
        continue;
      }

      // Créer la partie et initialiser le jeu
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
      init_game(game, challenger, accepter_pseudo, challenger_socket, socket);

      pthread_mutex_unlock(&games_mutex);
      pthread_mutex_unlock(&clients_mutex);

      // Supprimer le défi des défis en attente
      remove_challenge(challenger, accepter_pseudo);

      // Diffuser l'état initial du jeu
      broadcast_game_state(game);
    } else if (strncmp(buffer, "OBSERVE", 7) == 0) {
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
        continue;
      }

      pthread_mutex_lock(&games_mutex);
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
          send_game_state_to_observer(&games[game_id], socket);
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
    } else if (strncmp(buffer, "MESSAGE", 7) == 0) {
      send_message(socket, buffer);
      memset(buffer, 0, BUFFER_SIZE);
    } else if (strncmp(buffer, "MOVE", 4) == 0) {
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

        // Vérifier si c'est le bon joueur
        unsigned int player_num = (socket == game->socket1) ? 1 : 2;
        if (player_num == game->jeu.joueurCourant) {
          // Jouer le coup avec awale_v2.c
          if (jouer_coup(&game->jeu, move)) {
            broadcast_game_state(game);
          }
        }
        pthread_mutex_unlock(&games_mutex);
      }
    } else if (strncmp(buffer, "FORFEIT", 7) == 0) {
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

        // Déterminer qui abandonne et qui gagne
        if (socket == game->socket1) {
          game->jeu.scoreJ2 = 25; // Le joueur 2 gagne
          game->jeu.gagnant = 2;
        } else {
          game->jeu.scoreJ1 = 25; // Le joueur 1 gagne
          game->jeu.gagnant = 1;
        }

        game->jeu.fini = 1;
        broadcast_game_state(game);

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
    } else if (strncmp(buffer, "ADD_HISTORY", 11) == 0) {
      ajouter_historique(buffer);
    } else if (strncmp(buffer, "HISTORY", 7) == 0) {
      send_history(socket);
    }
  }

  // Clean up client
  pthread_mutex_lock(&clients_mutex);
  pthread_mutex_lock(&games_mutex);
  for (int i = 0; i < nb_clients; i++) {
    if (clients[i].socket == socket) {
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
    pthread_create(&thread, NULL, handle_client, client_socket);
    pthread_detach(thread);
  }

  return 0;
}
