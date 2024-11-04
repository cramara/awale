// awale_client.c
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include "awale_v2.c"

#define BUFFER_SIZE 1024
#define MAX_PSEUDO_LENGTH 50
#define MAX_GAMES 25

void print_help() {
    printf("\nCommandes disponibles:\n");
    printf("/list - Liste des joueurs disponibles\n");
    printf("/games - Liste des parties en cours\n");
    printf("/challenge <pseudo> - Défier un joueur\n");
    printf("/observe <game_id> - Observer une partie\n");
    printf("/quit - Quitter le jeu\n");
    printf("1-6 - Jouer un coup (pendant une partie)\n\n");
}

typedef struct {
    char player1[MAX_PSEUDO_LENGTH];
    char player2[MAX_PSEUDO_LENGTH];
} GameInfo;

GameInfo games[MAX_GAMES];


// Structure pour les données partagées
typedef struct {
    int socket;
    int player_num;
    char pseudo[MAX_PSEUDO_LENGTH];
    Awale jeu;
} ClientData;


void *receive_messages(void *arg) {
    ClientData *data = (ClientData*)arg;
    char buffer[BUFFER_SIZE];

    while(1) {
        memset(buffer, 0, BUFFER_SIZE);
        int n = read(data->socket, buffer, BUFFER_SIZE);
        if(n <= 0) break;
        buffer[n] = 0;

        // Vérifier d'abord si le message contient "GAMESTATE" n'importe où dans la chaîne
        char *gamestate_pos = strstr(buffer, "GAMESTATE");
        if(gamestate_pos != NULL) {
            printf("Message reçu contenant GAMESTATE: %s\n", buffer);  // Debug
            
            // Si c'est le premier état de jeu reçu et que player_num n'est pas encore défini
            if(data->player_num == 0) {
                char player1[MAX_PSEUDO_LENGTH];
                char player2[MAX_PSEUDO_LENGTH];
                strncpy(data->jeu.pseudo1, player1, MAX_PSEUDO_LENGTH - 1);
                data->jeu.pseudo1[MAX_PSEUDO_LENGTH - 1] = '\0';
                strncpy(data->jeu.pseudo2, player2, MAX_PSEUDO_LENGTH - 1);
                data->jeu.pseudo2[MAX_PSEUDO_LENGTH - 1] = '\0';
                // Chercher les noms des joueurs avant GAMESTATE
                if(sscanf(buffer, "GAMESTATE %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %s %s", player1, player2) == 2) {
                    printf("Joueurs détectés: %s vs %s\n", player1, player2);  // Debug
                    if(strcmp(data->pseudo, player1) == 0) {
                        data->player_num = 1;
                    } else if(strcmp(data->pseudo, player2) == 0) {
                        data->player_num = 2;
                    } else {
                        data->player_num = 3; // Observateur
                    }
                    printf("Numéro de joueur assigné: %d\n", data->player_num);  // Debug
                }
            }
            
            deserialiser_jeu(&data->jeu, gamestate_pos);
            //system("clear");
            
            // Afficher le plateau avec la bonne perspective


            afficher_plateau(&data->jeu, data->player_num);
            
            if(data->jeu.fini) {
                if(data->jeu.gagnant == data->player_num) {
                    printf("\nFélicitations ! Vous avez gagné !\n");
                } else if(data->jeu.gagnant > 0) {
                    printf("\nVous avez perdu.\n");
                } else {
                    printf("\nMatch nul !\n");
                }
            } else if(data->jeu.joueurCourant == data->player_num) {
                printf("\nC'est votre tour ! Choisissez un trou (1-6):\n");
            } else {
                printf("\nEn attente du coup de l'adversaire...\n");
            }
        }
        else if(strncmp(buffer, "PLAYERS", 7) == 0) {
            printf("\nJoueurs disponibles:\n%s\n", buffer + 8);
        }
        else if(strncmp(buffer, "GAMES", 5) == 0) {
            printf("\nParties en cours:\n%s\n", buffer + 6);
        }
        else if(strncmp(buffer, "CHALLENGE_FROM", 13) == 0) {
            char challenger[MAX_PSEUDO_LENGTH];
            sscanf(buffer, "CHALLENGE_FROM %s", challenger);
            printf("\nDéfi reçu de %s! Tapez '/accept %s' pour accepter\n", challenger, challenger);
        }
    }
    return NULL;
}

int main(int argc, char** argv) {
    if(argc != 3) {
        printf("Usage: %s server_ip port\n", argv[0]);
        return 1;
    }

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(socket_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);
    server_addr.sin_port = htons(atoi(argv[2]));

    if(connect(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(socket_fd);
        return 1;
    }

    char pseudo[MAX_PSEUDO_LENGTH];
    printf("Entrez votre pseudo: ");
    if(fgets(pseudo, MAX_PSEUDO_LENGTH, stdin) == NULL) {
        printf("Erreur de lecture du pseudo\n");
        close(socket_fd);
        return 1;
    }
    pseudo[strcspn(pseudo, "\n")] = 0;  // Remove newline

    // Send login request
    char buffer[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE, "LOGIN %s", pseudo);
    if(send(socket_fd, buffer, strlen(buffer), 0) < 0) {
        perror("Send failed");
        close(socket_fd);
        return 1;
    }

    // Wait for server response
    memset(buffer, 0, BUFFER_SIZE);
    ssize_t bytes_read = recv(socket_fd, buffer, BUFFER_SIZE - 1, 0);
    if(bytes_read <= 0) {
        printf("Erreur de lecture de la réponse du serveur\n");
        close(socket_fd);
        return 1;
    }
    buffer[bytes_read] = '\0';

    if(strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);  // Skip "ERROR "
        close(socket_fd);
        return 1;
    } else if(strcmp(buffer, "LOGIN_OK") != 0) {
        printf("Réponse inattendue du serveur: %s\n", buffer);
        close(socket_fd);
        return 1;
    }

    // Après la connexion réussie et le LOGIN_OK
    printf("Connecté au serveur! Tapez /help pour la liste des commandes\n");

    // Initialisation de la structure ClientData
    ClientData *data = malloc(sizeof(ClientData));
    data->socket = socket_fd;
    strncpy(data->pseudo, pseudo, MAX_PSEUDO_LENGTH - 1);
    data->pseudo[MAX_PSEUDO_LENGTH - 1] = '\0';
    data->player_num = 0;  // Sera mis à jour quand une partie commencera
    
    // Initialisation du jeu
    init_awale(&data->jeu,"vide","vide");

    pthread_t receive_thread;
    pthread_create(&receive_thread, NULL, receive_messages, data);

    while(1) {
        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = 0;

        if(strcmp(buffer, "/help") == 0) {
            print_help();
        }
        else if(strcmp(buffer, "/list") == 0) {
            write(socket_fd, "LIST", 4);
        }
        else if(strcmp(buffer, "/games") == 0) {
            write(socket_fd, "GAMES", 5);
        }
        else if(strncmp(buffer, "/challenge", 10) == 0 || strncmp(buffer, "/c", 2) == 0) {
            char opponent[MAX_PSEUDO_LENGTH];
            if(sscanf(buffer, "/challenge %s", opponent) == 1) {
                snprintf(buffer, BUFFER_SIZE, "CHALLENGE %s", opponent);
                write(socket_fd, buffer, strlen(buffer));
                printf("Défi envoyé à %s\n", opponent);
            } else  if(sscanf(buffer, "/c %s", opponent) == 1) {
                snprintf(buffer, BUFFER_SIZE, "CHALLENGE %s", opponent);
                write(socket_fd, buffer, strlen(buffer));
                printf("Défi envoyé à %s\n", opponent);
            } 
            else {
                printf("Usage: /challenge <pseudo>\n");
            }
        }
        else if(strncmp(buffer, "/accept", 7) == 0, strncmp(buffer, "/a", 2) == 0) {
            char challenger[MAX_PSEUDO_LENGTH];
            if(sscanf(buffer, "/accept %s", challenger) == 1) {
                snprintf(buffer, BUFFER_SIZE, "ACCEPT %s", challenger);
                write(socket_fd, buffer, strlen(buffer));
                printf("Défi accepté! La partie va commencer...\n");
            } else if(sscanf(buffer, "/a %s", challenger) == 1) {
                snprintf(buffer, BUFFER_SIZE, "ACCEPT %s", challenger);
                write(socket_fd, buffer, strlen(buffer));
                printf("Défi accepté! La partie va commencer...\n");
            }
            else {
                printf("Usage: /accept <pseudo>\n");
            }
        }
        else if(strncmp(buffer, "/observe", 8) == 0) {
            int game_id;
            if(sscanf(buffer, "/observe %d", &game_id) == 1) {
                snprintf(buffer, BUFFER_SIZE, "OBSERVE %d", game_id);
                write(socket_fd, buffer, strlen(buffer));
                printf("Mode observation activé pour la partie %d\n", game_id);
            } else {
                printf("Usage: /observe <game_id>\n");
            }
        }
        else if(strcmp(buffer, "/quit") == 0) {
            printf("Au revoir!\n");
            break;
        }
        else if(buffer[0] >= '1' && buffer[0] <= '6') {
            int move = buffer[0] - '0';
            // Vérification locale de la validité du coup
            if(coup_valide(&data->jeu, move)) {
                snprintf(buffer, BUFFER_SIZE, "MOVE %d", move);
                write(socket_fd, buffer, strlen(buffer));
            } else {
                printf("Coup invalide, choisissez une case entre 1 et 6 contenant des graines\n");
            }
        }
        else {
            printf("Commande inconnue. Tapez /help pour la liste des commandes\n");
        }
    }

    close(socket_fd);
    // Avant de fermer la connexion
    pthread_cancel(receive_thread);
    free(data);
    close(socket_fd);
    return 0;
}