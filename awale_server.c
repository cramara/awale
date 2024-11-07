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
#define MAX_MESSAGE_LENGTH 2048
#define RESET_COLOR "\033[0m"
#define GREEN_TEXT "\033[32m"
#define RED_TEXT "\033[31m"

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

Client clients[MAX_CLIENTS];
Game games[MAX_GAMES];
int nb_clients = 0;
int nb_games = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t games_mutex = PTHREAD_MUTEX_INITIALIZER;

void broadcast_game_state(Game *game) {
  // Buffer pour stocker l'état complet
  char full_state[BUFFER_SIZE];
  
  // Créer directement l'état du jeu dans le buffer
  snprintf(full_state, BUFFER_SIZE, "GAMESTATE %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %s %s ",
           game->jeu.plateau[0], game->jeu.plateau[1], game->jeu.plateau[2],
           game->jeu.plateau[3], game->jeu.plateau[4], game->jeu.plateau[5],
           game->jeu.plateau[6], game->jeu.plateau[7], game->jeu.plateau[8],
           game->jeu.plateau[9], game->jeu.plateau[10], game->jeu.plateau[11],
           game->jeu.scoreJ1, game->jeu.scoreJ2,
           game->jeu.joueurCourant, game->jeu.fini, game->jeu.gagnant,
           game->jeu.pseudo1, game->jeu.pseudo2);


  printf("Broadcasting game state: %s\n", full_state);
  printf("socket1: %d\n", game->socket1);
  printf("socket2: %d\n", game->socket2);

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
    
    snprintf(full_state, BUFFER_SIZE, "GAMESTATE %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %s %s ",
             game->jeu.plateau[0], game->jeu.plateau[1], game->jeu.plateau[2],
             game->jeu.plateau[3], game->jeu.plateau[4], game->jeu.plateau[5],
             game->jeu.plateau[6], game->jeu.plateau[7], game->jeu.plateau[8],
             game->jeu.plateau[9], game->jeu.plateau[10], game->jeu.plateau[11],
             game->jeu.scoreJ1, game->jeu.scoreJ2,
             game->jeu.joueurCourant, game->jeu.fini, game->jeu.gagnant,
             game->jeu.pseudo1, game->jeu.pseudo2);

    write(observer_socket, full_state, strlen(full_state));
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
      const char *error_msg = "ERROR Format de connexion invalide";
      send(socket, error_msg, strlen(error_msg), 0);
      continue;  // Redemander un pseudo
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
      const char *error_msg = "ERROR Pseudo déjà utilisé";
      send(socket, error_msg, strlen(error_msg), 0);
      continue;  // Redemander un pseudo
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
    break;  // Sortir de la boucle une fois le pseudo accepté
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
    } else if (strncmp(buffer, "CHALLENGE", 9) == 0) {
        char opponent[MAX_PSEUDO_LENGTH];
        char challenger_pseudo[MAX_PSEUDO_LENGTH];
        sscanf(buffer, "CHALLENGE %s", opponent);

        // Find opponent
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
            const char *error_msg = "ERROR Vous ne pouvez pas vous défier vous-même\n";
            write(socket, error_msg, strlen(error_msg));
            pthread_mutex_unlock(&clients_mutex);
            continue;
        }
        
        if (challenger_is_playing) {
            const char *error_msg = "ERROR Vous ne pouvez pas lancer de défi pendant une partie\n";
            write(socket, error_msg, strlen(error_msg));
            pthread_mutex_unlock(&clients_mutex);
            continue;
        }

        // Chercher l'adversaire
        for (int i = 0; i < nb_clients; i++) {
            if (strcmp(clients[i].pseudo, opponent) == 0) {
                if (clients[i].is_playing) {
                    const char *error_msg = "ERROR Ce joueur est déjà en partie\n";
                    write(socket, error_msg, strlen(error_msg));
                    opponent_socket = -2; // Pour indiquer qu'on a trouvé le joueur mais qu'il est occupé
                } else {
                    opponent_socket = clients[i].socket;
                }
                break;
            }
        }
        pthread_mutex_unlock(&clients_mutex);

        if (opponent_socket == -1) {
            const char *error_msg = "ERROR Joueur non trouvé\n";
            write(socket, error_msg, strlen(error_msg));
        } else if (opponent_socket >= 0) {  // Si le joueur est trouvé et n'est pas en partie
            char challenge[BUFFER_SIZE];
            snprintf(challenge, BUFFER_SIZE, "CHALLENGE_FROM %s", challenger_pseudo);
            write(opponent_socket, challenge, strlen(challenge));
        }
    }
    else if (strncmp(buffer, "ACCEPT", 6) == 0) {
      char challenger[MAX_PSEUDO_LENGTH];
      sscanf(buffer, "ACCEPT %s", challenger);

      // Vérifier si le joueur essaie d'accepter son propre défi
      pthread_mutex_lock(&clients_mutex);
      int is_self_accept = 0;
      for (int i = 0; i < nb_clients; i++) {
          if (clients[i].socket == socket) {
              if (strcmp(clients[i].pseudo, challenger) == 0) {
                  is_self_accept = 1;
                  const char *error_msg = "ERROR Vous ne pouvez pas accepter votre propre défi\n";
                  write(socket, error_msg, strlen(error_msg));
              }
              break;
          }
      }
      pthread_mutex_unlock(&clients_mutex);

      if (is_self_accept) {
          continue;
      }

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
          const char *error_msg = "ERROR Vous ne pouvez pas observer une partie pendant que vous jouez\n";
          write(socket, error_msg, strlen(error_msg));
          continue;
      }

      pthread_mutex_lock(&games_mutex);
      if (game_id >= 0 && game_id < nb_games && !games[game_id].jeu.fini) {
        games[game_id].observers[games[game_id].nb_observers++] = socket;
        // Envoyer l'état du jeu uniquement à l'observateur qui vient de se connecter
        send_game_state_to_observer(&games[game_id], socket);
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
                game->jeu.scoreJ2 = 25;  // Le joueur 2 gagne
                game->jeu.gagnant = 2;
            } else {
                game->jeu.scoreJ1 = 25;  // Le joueur 1 gagne
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
