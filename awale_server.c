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
#define BUFFER_SIZE 1024
#define MAX_MESSAGE_LENGTH 2048
#define RESET_COLOR "\033[0m"
#define GREEN_TEXT "\033[32m"
#define RED_TEXT "\033[31m"
#define MAX_HISTORIQUE 100

typedef struct {
  int socket;
  char pseudo[MAX_PSEUDO_LENGTH];
  int is_playing;
  int game_id;
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

Client clients[MAX_CLIENTS];
Game games[MAX_GAMES];
int nb_clients = 0;
int nb_games = 0;
HistoriqueParties historique[MAX_HISTORIQUE];
int nb_historique = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t games_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t historique_mutex = PTHREAD_MUTEX_INITIALIZER;

void init_historique_test() {
  pthread_mutex_lock(&historique_mutex);

  // Partie 1
  strncpy(historique[0].player1, "Pierre", MAX_PSEUDO_LENGTH - 1);
  strncpy(historique[0].player2, "Paul", MAX_PSEUDO_LENGTH - 1);
  strncpy(historique[0].winner, "Pierre", MAX_PSEUDO_LENGTH - 1);
  historique[0].score_player1 = 25;
  historique[0].score_player2 = 12;

  // Partie 2
  strncpy(historique[1].player1, "Marie", MAX_PSEUDO_LENGTH - 1);
  strncpy(historique[1].player2, "Jean", MAX_PSEUDO_LENGTH - 1);
  strncpy(historique[1].winner, "Marie", MAX_PSEUDO_LENGTH - 1);
  historique[1].score_player1 = 30;
  historique[1].score_player2 = 15;

  // Partie 3
  strncpy(historique[2].player1, "Sophie", MAX_PSEUDO_LENGTH - 1);
  strncpy(historique[2].player2, "Lucas", MAX_PSEUDO_LENGTH - 1);
  strncpy(historique[2].winner, "Sophie", MAX_PSEUDO_LENGTH - 1);
  historique[2].score_player1 = 28;
  historique[2].score_player2 = 20;

  // Partie 4
  strncpy(historique[3].player1, "Alice", MAX_PSEUDO_LENGTH - 1);
  strncpy(historique[3].player2, "Bob", MAX_PSEUDO_LENGTH - 1);
  strncpy(historique[3].winner, "Bob", MAX_PSEUDO_LENGTH - 1);
  historique[3].score_player1 = 18;
  historique[3].score_player2 = 24;

  // Partie 5
  strncpy(historique[4].player1, "Thomas", MAX_PSEUDO_LENGTH - 1);
  strncpy(historique[4].player2, "Julie", MAX_PSEUDO_LENGTH - 1);
  strncpy(historique[4].winner, "Julie", MAX_PSEUDO_LENGTH - 1);
  historique[4].score_player1 = 22;
  historique[4].score_player2 = 26;

  nb_historique = 5;

  pthread_mutex_unlock(&historique_mutex);
}

void broadcast_game_state(Game *game) {
  // Buffer pour stocker l'état complet
  char full_state[BUFFER_SIZE];

  // Obtenir l'état sérialisé du jeu
  char *game_state = serialiser_jeu(&game->jeu);

  // Créer le message complet avec les pseudos des joueurs
  snprintf(full_state, BUFFER_SIZE, "GAMESTATE%s", game_state);

  // Envoyer aux joueurs
  write(game->socket1, full_state, strlen(full_state));
  write(game->socket2, full_state, strlen(full_state));

  // Envoyer aux observateurs
  for (int i = 0; i < game->nb_observers; i++) {
    write(game->observers[i], full_state, strlen(full_state));
  }
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
        snprintf(line, sizeof(line), "%s %s %u %u|",
                historique[i].winner,
                (strcmp(historique[i].winner, historique[i].player1) == 0) 
                    ? historique[i].player2 
                    : historique[i].player1,
                historique[i].score_player1,
                historique[i].score_player2);
        
        strcat(history_message, line);
    }
    strcat(history_message, "\n");
    
    // Envoie le message complet
    write(socket, history_message, strlen(history_message));
    
    pthread_mutex_unlock(&historique_mutex);
}

void send_message(int socket, char *buffer) {
  // Format du buffer : "MESSAGE <destinataire> <message>"
  char destinataire[MAX_PSEUDO_LENGTH];
  char message[BUFFER_SIZE];
  char expediteur[MAX_PSEUDO_LENGTH];

  // Extraire le destinataire et le message
  if (sscanf(buffer, "MESSAGE %s %[^\n]", destinataire, message) != 2) {
    const char *error_msg = "ERREUR Format de message incorrect\n";
    write(socket, error_msg, strlen(error_msg));
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
      char game_info[100];
      snprintf(game_info, 100, " [%d]%s-%s", i, games[i].player1,
               games[i].player2);
      strcat(buffer, game_info);
    }
  }
  pthread_mutex_unlock(&games_mutex);
  write(socket, buffer, strlen(buffer));
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

  // Envoie immédiatement l'état initial aux deux joueurs
  broadcast_game_state(game);
}

void *handle_client(void *arg) {
  int socket = *(int *)arg;
  char buffer[BUFFER_SIZE];
  free(arg);

  // Wait for pseudo
  memset(buffer, 0, BUFFER_SIZE);
  ssize_t bytes_read = recv(socket, buffer, BUFFER_SIZE - 1, 0);
  if (bytes_read <= 0) {
    close(socket);
    return NULL;
  }
  buffer[bytes_read] = '\0';

  char pseudo[MAX_PSEUDO_LENGTH];
  if (sscanf(buffer, "LOGIN %s", pseudo) != 1) {
    const char *error_msg = "ERROR Invalid login format";
    send(socket, error_msg, strlen(error_msg), 0);
    close(socket);
    return NULL;
  }

  // Check if pseudo already exists
  pthread_mutex_lock(&clients_mutex);
  for (int i = 0; i < nb_clients; i++) {
    if (strcmp(clients[i].pseudo, pseudo) == 0) {
      pthread_mutex_unlock(&clients_mutex);
      const char *error_msg = "ERROR Pseudo already taken";
      send(socket, error_msg, strlen(error_msg), 0);
      close(socket);
      return NULL;
    }
  }

  // Add client to list
  Client *client = &clients[nb_clients++];
  client->socket = socket;
  strncpy(client->pseudo, pseudo, MAX_PSEUDO_LENGTH - 1);
  client->pseudo[MAX_PSEUDO_LENGTH - 1] = '\0';
  client->is_playing = 0;
  client->game_id = -1;
  pthread_mutex_unlock(&clients_mutex);

  // Send confirmation
  const char *success_msg = "LOGIN_OK";
  send(socket, success_msg, strlen(success_msg), 0);
  printf("New client connected: %s\n", pseudo);

  while (1) {
    int n = read(socket, buffer, BUFFER_SIZE);
    if (n <= 0)
      break;
    buffer[n] = 0;

    if (strncmp(buffer, "LIST", 4) == 0) {
      send_free_players(socket);
    } else if (strncmp(buffer, "GAMES", 5) == 0) {
      send_active_games(socket);
    } else if (strncmp(buffer, "CHALLENGE", 9) == 0) {
      char opponent[MAX_PSEUDO_LENGTH];
      sscanf(buffer, "CHALLENGE %s", opponent);

      // Find opponent
      pthread_mutex_lock(&clients_mutex);
      int opponent_socket = -1;
      for (int i = 0; i < nb_clients; i++) {
        if (strcmp(clients[i].pseudo, opponent) == 0 &&
            !clients[i].is_playing) {
          opponent_socket = clients[i].socket;
          break;
        }
      }
      pthread_mutex_unlock(&clients_mutex);

      if (opponent_socket != -1) {
        char challenge[BUFFER_SIZE];
        snprintf(challenge, BUFFER_SIZE, "CHALLENGE_FROM %s", pseudo);
        write(opponent_socket, challenge, strlen(challenge));
      }
    } else if (strncmp(buffer, "ACCEPT", 6) == 0) {
      char challenger[MAX_PSEUDO_LENGTH];
      sscanf(buffer, "ACCEPT %s", challenger);

      // Create new game
      pthread_mutex_lock(&games_mutex);
      Game *game = &games[nb_games++];
      pthread_mutex_unlock(&games_mutex);

      // Find challenger socket
      pthread_mutex_lock(&clients_mutex);
      int challenger_socket = -1;
      for (int i = 0; i < nb_clients; i++) {
        if (strcmp(clients[i].pseudo, challenger) == 0) {
          challenger_socket = clients[i].socket;
          clients[i].is_playing = 1;
          clients[i].game_id = nb_games - 1;
          break;
        }
      }
      for (int i = 0; i < nb_clients; i++) {
        if (clients[i].socket == socket) {
          clients[i].is_playing = 1;
          clients[i].game_id = nb_games - 1;
          break;
        }
      }
      pthread_mutex_unlock(&clients_mutex);

      init_game(game, challenger, pseudo, challenger_socket, socket);
      broadcast_game_state(game);
    } else if (strncmp(buffer, "OBSERVE", 7) == 0) {
      int game_id;
      sscanf(buffer, "OBSERVE %d", &game_id);

      pthread_mutex_lock(&games_mutex);
      if (game_id >= 0 && game_id < nb_games &&
          !games[game_id].jeu.fini) { // On utilise game->jeu.fini
        games[game_id].observers[games[game_id].nb_observers++] = socket;
        broadcast_game_state(&games[game_id]);
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
    } else if (strncmp(buffer, "ADD_HISTORY", 11) == 0) {
      ajouter_historique(buffer);
    } else if (strncmp(buffer, "HISTORY", 7) == 0) {
      send_history(socket);
    }
  }

  // Clean up client
  pthread_mutex_lock(&clients_mutex);
  for (int i = 0; i < nb_clients; i++) {
    if (clients[i].socket == socket) {
      for (int j = i; j < nb_clients - 1; j++) {
        clients[j] = clients[j + 1];
      }
      nb_clients--;
      break;
    }
  }
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
  init_historique_test();

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
