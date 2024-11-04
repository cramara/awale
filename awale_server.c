// awale_server.c
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include "awale_v2.c"

#define MAX_CLIENTS 50
#define MAX_GAMES 25
#define MAX_PSEUDO_LENGTH 50
#define BUFFER_SIZE 1024

typedef struct {
    int socket;
    char pseudo[MAX_PSEUDO_LENGTH];
    int is_playing;
    int game_id;
} Client;

typedef struct {
    Awale jeu;                    // Ajout de la structure de jeu
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
    
    // Obtenir l'état sérialisé du jeu
    char *game_state = serialiser_jeu(&game->jeu);
    
    // Créer le message complet avec les pseudos des joueurs
    snprintf(full_state, BUFFER_SIZE, "GAMESTATE%s", game_state);
    
    // Envoyer aux joueurs
    write(game->socket1, full_state, strlen(full_state));
    write(game->socket2, full_state, strlen(full_state));
    
    // Envoyer aux observateurs
    for(int i = 0; i < game->nb_observers; i++) {
        write(game->observers[i], full_state, strlen(full_state));
    }
}

void send_free_players(int socket) {
    char buffer[BUFFER_SIZE] = "PLAYERS";
    pthread_mutex_lock(&clients_mutex);
    for(int i = 0; i < nb_clients; i++) {
        if(!clients[i].is_playing) {
            strcat(buffer, " ");
            strcat(buffer, clients[i].pseudo);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    write(socket, buffer, strlen(buffer));
}

void send_active_games(int socket) {
    char buffer[BUFFER_SIZE] = "GAMES";
    pthread_mutex_lock(&games_mutex);
    for(int i = 0; i < nb_games; i++) {
        if(!games[i].jeu.fini) {    
            char game_info[100];
            snprintf(game_info, 100, " [%d]%s-%s", i, games[i].player1, games[i].player2);
            strcat(buffer, game_info);
        }
    }
    pthread_mutex_unlock(&games_mutex);
    write(socket, buffer, strlen(buffer));
}

void init_game(Game *game, char *player1, char *player2, int socket1, int socket2) {
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
    int socket = *(int*)arg;
    char buffer[BUFFER_SIZE];
    free(arg);

    // Wait for pseudo
    memset(buffer, 0, BUFFER_SIZE);
    ssize_t bytes_read = recv(socket, buffer, BUFFER_SIZE - 1, 0);
    if(bytes_read <= 0) {
        close(socket);
        return NULL;
    }
    buffer[bytes_read] = '\0';

    char pseudo[MAX_PSEUDO_LENGTH];
    if(sscanf(buffer, "LOGIN %s", pseudo) != 1) {
        const char *error_msg = "ERROR Invalid login format";
        send(socket, error_msg, strlen(error_msg), 0);
        close(socket);
        return NULL;
    }

    // Check if pseudo already exists
    pthread_mutex_lock(&clients_mutex);
    for(int i = 0; i < nb_clients; i++) {
        if(strcmp(clients[i].pseudo, pseudo) == 0) {
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

    while(1) {
        int n = read(socket, buffer, BUFFER_SIZE);
        if(n <= 0) break;
        buffer[n] = 0;

        if(strncmp(buffer, "LIST", 4) == 0) {
            send_free_players(socket);
        }
        else if(strncmp(buffer, "GAMES", 5) == 0) {
            send_active_games(socket);
        }
        else if(strncmp(buffer, "CHALLENGE", 9) == 0) {
            char opponent[MAX_PSEUDO_LENGTH];
            sscanf(buffer, "CHALLENGE %s", opponent);
            
            // Find opponent
            pthread_mutex_lock(&clients_mutex);
            int opponent_socket = -1;
            for(int i = 0; i < nb_clients; i++) {
                if(strcmp(clients[i].pseudo, opponent) == 0 && !clients[i].is_playing) {
                    opponent_socket = clients[i].socket;
                    break;
                }
            }
            pthread_mutex_unlock(&clients_mutex);

            if(opponent_socket != -1) {
                char challenge[BUFFER_SIZE];
                snprintf(challenge, BUFFER_SIZE, "CHALLENGE_FROM %s", pseudo);
                write(opponent_socket, challenge, strlen(challenge));
            }
        }
        else if(strncmp(buffer, "ACCEPT", 6) == 0) {
            char challenger[MAX_PSEUDO_LENGTH];
            sscanf(buffer, "ACCEPT %s", challenger);
            
            // Create new game
            pthread_mutex_lock(&games_mutex);
            Game *game = &games[nb_games++];
            pthread_mutex_unlock(&games_mutex);
            
            // Find challenger socket
            pthread_mutex_lock(&clients_mutex);
            int challenger_socket = -1;
            for(int i = 0; i < nb_clients; i++) {
                if(strcmp(clients[i].pseudo, challenger) == 0) {
                    challenger_socket = clients[i].socket;
                    clients[i].is_playing = 1;
                    clients[i].game_id = nb_games - 1;
                    break;
                }
            }
            for(int i = 0; i < nb_clients; i++) {
                if(clients[i].socket == socket) {
                    clients[i].is_playing = 1;
                    clients[i].game_id = nb_games - 1;
                    break;
                }
            }
            pthread_mutex_unlock(&clients_mutex);

            init_game(game, challenger, pseudo, challenger_socket, socket);
            broadcast_game_state(game);
        }
        else if(strncmp(buffer, "OBSERVE", 7) == 0) {
    int game_id;
    sscanf(buffer, "OBSERVE %d", &game_id);
    
    pthread_mutex_lock(&games_mutex);
    if(game_id >= 0 && game_id < nb_games && !games[game_id].jeu.fini) {  // On utilise game->jeu.fini
        games[game_id].observers[games[game_id].nb_observers++] = socket;
        broadcast_game_state(&games[game_id]);
    }
    pthread_mutex_unlock(&games_mutex);
}
        else if(strncmp(buffer, "MOVE", 4) == 0) {
    int move;
    sscanf(buffer, "MOVE %d", &move);
    
    pthread_mutex_lock(&clients_mutex);
    int game_id = -1;
    for(int i = 0; i < nb_clients; i++) {
        if(clients[i].socket == socket && clients[i].is_playing) {
            game_id = clients[i].game_id;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    
    if(game_id != -1) {
        pthread_mutex_lock(&games_mutex);
        Game *game = &games[game_id];
        
        // Vérifier si c'est le bon joueur
        int player_num = (socket == game->socket1) ? 1 : 2;
        if(player_num == game->jeu.joueurCourant) {
            // Jouer le coup avec awale_v2.c
            if(jouer_coup(&game->jeu, move)) {
                broadcast_game_state(game);
            }
        }
        pthread_mutex_unlock(&games_mutex);
    }
}
    }

    // Clean up client
    pthread_mutex_lock(&clients_mutex);
    for(int i = 0; i < nb_clients; i++) {
        if(clients[i].socket == socket) {
            for(int j = i; j < nb_clients-1; j++) {
                clients[j] = clients[j+1];
            }
            nb_clients--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    close(socket);
    return NULL;
}

int main(int argc, char** argv) {
    if(argc != 2) {
        printf("Usage: %s port\n", argv[0]);
        return 1;
    }

    srand(time(NULL));

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(atoi(argv[1]));

    if(bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        return 1;
    }

    listen(server_socket, 5);
    printf("Server started on port %s\n", argv[1]);

    while(1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int *client_socket = malloc(sizeof(int));
        *client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, client_socket);
        pthread_detach(thread);
    }

    return 0;
}