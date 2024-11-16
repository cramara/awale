#include "awale_v2.c"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define TAILLE_BUFFER 1024
#define TAILLE_MAX_BIO 200
#define TAILLE_MAX_PSEUDO 50
#define MAX_PARTIES 25
#define RESET_COLOR "\033[0m"
#define GREEN_TEXT "\033[32m"
#define RED_TEXT "\033[31m"

typedef struct {
  int socket;
  unsigned int numero_joueur;
  char pseudo[TAILLE_MAX_PSEUDO];
  Awale jeu;
} DonneesClient;

static DonneesClient *donnees_globales = NULL;
static int descripteur_socket_global = -1;

void afficher_aide() {
  printf("\nCommandes disponibles:\n");
  printf("/create-bio <texte> - Créer ou mettre à jour votre bio\n");
  printf("/check-bio <pseudo> - Voir la bio d'un joueur\n");
  printf("/list - Liste des joueurs disponibles\n");
  printf("/games - Liste des parties en cours\n");
  printf("/challenge <pseudo> - Défier un joueur\n");
  printf("/observe <id_partie> - Observer une partie\n");
  printf("/message <pseudo || all> <message> - Envoyer un message\n");
  printf("/history - Historique des parties\n");
  printf("/quit - Quitter le jeu\n");
  printf("/forfeit ou /ff - Abandonner la partie en cours\n");
  printf("1-6 - Jouer un coup (pendant une partie)\n\n");
}

typedef struct {
  char joueur1[TAILLE_MAX_PSEUDO];
  char joueur2[TAILLE_MAX_PSEUDO];
} InfoPartie;

InfoPartie parties[MAX_PARTIES];

void envoyer_historique_partie(int socket_fd, DonneesClient *donnees) {
  char buffer[TAILLE_BUFFER];
  // format de la commande: HISTORY <pseudo1 = gagnant> <pseudo2 = perdant>
  // <score joueur 1> <score joueur 2>
  snprintf(buffer, TAILLE_BUFFER, "ADD_HISTORY %s %s %d %d",
           donnees->jeu.pseudo1, donnees->jeu.pseudo2, donnees->jeu.scoreJ1,
           donnees->jeu.scoreJ2);

  write(socket_fd, buffer, strlen(buffer));
}

void afficher_historique(char *buffer) {
  // Si aucune partie n'a été jouée
  if (strstr(buffer, "HISTORY NONE") != NULL) {
    printf("Aucune partie n'a été jouée.\n");
    return;
  }

  // Ignorer le préfixe "HISTORY "
  char *parties = buffer + 8;
  char *partie;

  printf("\n=== Historique des parties ===\n");

  // Utiliser strtok pour séparer les parties (délimiteur '|')
  partie = strtok(parties, "|");
  while (partie != NULL) {
    char gagnant[TAILLE_MAX_PSEUDO];
    char perdant[TAILLE_MAX_PSEUDO];
    unsigned int score1, score2;

    if (sscanf(partie, "%s %s %u %u", gagnant, perdant, &score1, &score2) ==
        4) {
      printf("%s%s%s a gagné contre %s%s%s (%u - %u)\n", GREEN_TEXT, gagnant,
             RESET_COLOR,                    // Gagnant en vert
             RED_TEXT, perdant, RESET_COLOR, // Perdant en rouge
             score1, score2);
    }

    partie = strtok(NULL, "|");
  }
  printf("\n");
}

void *recevoir_messages(void *arg) {
  DonneesClient *donnees = (DonneesClient *)arg;
  char buffer[TAILLE_BUFFER];

  while (1) {
    memset(buffer, 0, TAILLE_BUFFER);
    int n = read(donnees->socket, buffer, TAILLE_BUFFER);
    if (n <= 0)
      break;
    buffer[n] = 0;

    char *pos_etat_jeu = strstr(buffer, "GAMESTATE");
    if (pos_etat_jeu != NULL) {
      printf("Message reçu contenant GAMESTATE: %s\n", buffer);

      if (donnees->numero_joueur == 0) {
        char joueur1[TAILLE_MAX_PSEUDO];
        char joueur2[TAILLE_MAX_PSEUDO];
        strncpy(donnees->jeu.pseudo1, joueur1, TAILLE_MAX_PSEUDO - 1);
        donnees->jeu.pseudo1[TAILLE_MAX_PSEUDO - 1] = '\0';
        strncpy(donnees->jeu.pseudo2, joueur2, TAILLE_MAX_PSEUDO - 1);
        donnees->jeu.pseudo2[TAILLE_MAX_PSEUDO - 1] = '\0';

        if (sscanf(buffer,
                   "GAMESTATE %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d "
                   "%*d %*d %*d %*d %*d %s %s",
                   joueur1, joueur2) == 2) {
          printf("Joueurs détectés: %s vs %s\n", joueur1, joueur2);
          if (strcmp(donnees->pseudo, joueur1) == 0) {
            donnees->numero_joueur = 1;
          } else if (strcmp(donnees->pseudo, joueur2) == 0) {
            donnees->numero_joueur = 2;
          } else {
            donnees->numero_joueur = 3;
          }
          printf("Numéro de joueur assigné: %d\n", donnees->numero_joueur);
        }
      }

      deserialiser_jeu(&donnees->jeu, pos_etat_jeu);
      afficher_plateau(&donnees->jeu, donnees->numero_joueur);

      if (donnees->jeu.fini) {
        if (donnees->jeu.gagnant == donnees->numero_joueur) {
          printf("\nFélicitations ! Vous avez gagné !\n");
          envoyer_historique_partie(donnees->socket, donnees);
        } else if (donnees->jeu.gagnant > 0) {
          printf("\nVous avez perdu.\n");
        } else {
          printf("\nMatch nul !\n");
        }
      } else if (donnees->jeu.joueurCourant == donnees->numero_joueur) {
        printf("\nC'est votre tour ! Choisissez un trou (1-6):\n");
      } else {
        printf("\nEn attente du coup de l'adversaire...\n");
      }
    } else if (strncmp(buffer, "PLAYERS", 7) == 0) {
      printf("\nJoueurs disponibles:\n%s\n", buffer + 8);
    } else if (strncmp(buffer, "GAMES", 5) == 0) {
      printf("\nParties en cours:\n%s\n", buffer + 6);
    } else if (strncmp(buffer, "CHALLENGE_FROM", 13) == 0) {
      char adversaire[TAILLE_MAX_PSEUDO];
      sscanf(buffer, "CHALLENGE_FROM %s", adversaire);
      printf("\nDéfi reçu de %s! Tapez '/accept %s' pour accepter\n",
             adversaire, adversaire);
    } else if (strncmp(buffer, "MESSAGE", 7) == 0) {
      printf("\n%s", buffer + 7);
    } else if (strncmp(buffer, "HISTORY", 7) == 0) {
      afficher_historique(buffer);
    } else if (strncmp(buffer, "BIO_UPDATE", 10) == 0) {
      printf("\n%s\n", buffer + 11);
    } else if (strncmp(buffer, "BIO", 3) == 0) {
      printf("\n%s\n", buffer + 4);
    } else if (strncmp(buffer, "ERROR", 5) == 0) {
      printf("\n%s\n", buffer + 6);
    }
  }
  return NULL;
}

void envoyer_commande_simple(int socket_fd, const char *commande) {
  write(socket_fd, commande, strlen(commande));
}

void gerer_defi(int socket_fd, char *buffer) {
  char adversaire[TAILLE_MAX_PSEUDO];
  if (sscanf(buffer, "/challenge %s", adversaire) == 1 ||
      sscanf(buffer, "/c %s", adversaire) == 1) {
    snprintf(buffer, TAILLE_BUFFER, "CHALLENGE %s", adversaire);
    envoyer_commande_simple(socket_fd, buffer);
  } else {
    printf("Usage: /challenge <pseudo>\n");
  }
}

void gerer_acceptation(int socket_fd, char *buffer) {
  char adversaire[TAILLE_MAX_PSEUDO];
  if (sscanf(buffer, "/accept %s", adversaire) == 1 ||
      sscanf(buffer, "/a %s", adversaire) == 1) {
    snprintf(buffer, TAILLE_BUFFER, "ACCEPT %s", adversaire);
    envoyer_commande_simple(socket_fd, buffer);
  } else {
    printf("Usage: /accept <pseudo>\n");
  }
}

void gerer_observation(int socket_fd, char *buffer) {
  int id_partie;
  if (sscanf(buffer, "/observe %d", &id_partie) == 1) {
    snprintf(buffer, TAILLE_BUFFER, "OBSERVE %d", id_partie);
    envoyer_commande_simple(socket_fd, buffer);
    printf("Mode observation activé pour la partie %d\n", id_partie);
  } else {
    printf("Usage: /observe <id_partie>\n");
  }
}

void gerer_message(int socket_fd, char *buffer) {
  char pseudo[TAILLE_MAX_PSEUDO];
  char message[TAILLE_BUFFER];

  if (sscanf(buffer, "/message %s %[^\n]", pseudo, message) == 2) {
    size_t message_len = strlen(message);
    if (message_len > TAILLE_BUFFER - 50) {
      message[TAILLE_BUFFER - 50] = '\0';
    }
    snprintf(buffer, TAILLE_BUFFER, "MESSAGE %s %s", pseudo, message);
    envoyer_commande_simple(socket_fd, buffer);
  } else {
    printf("Usage: /message <pseudo> <message>\n");
  }
}

void gerer_creation_bio(int socket_fd, char *buffer) {
  char bio_text[TAILLE_MAX_BIO];
  if (strlen(buffer) > 12) { // 11 + 1 pour l'espace
    snprintf(bio_text, sizeof(bio_text), "CREATE_BIO %s", buffer + 12);
    // format de la commande CREATE_BIO <bio>

    envoyer_commande_simple(socket_fd, bio_text);
  } else {
    printf("Usage: /create-bio <texte de votre bio>\n");
  }
}

void gerer_consultation_bio(int socket_fd, char *buffer) {
  char pseudo[MAX_PSEUDO_LENGTH];
  if (sscanf(buffer, "/check-bio %s", pseudo) == 1) {
    char commande[TAILLE_BUFFER];
    snprintf(commande, sizeof(commande), "CHECK_BIO %s", pseudo);

    // format de la commande CHECK_BIO <pseudo>
    envoyer_commande_simple(socket_fd, commande);
  } else {
    printf("Usage: /check-bio <pseudo>\n");
  }
}

void gerer_coup(int socket_fd, char *buffer, DonneesClient *donnees) {
  int coup = buffer[0] - '0';
  if (coup_valide(&donnees->jeu, coup)) {
    snprintf(buffer, TAILLE_BUFFER, "MOVE %d", coup);
    envoyer_commande_simple(socket_fd, buffer);
  } else {
    printf("Coup invalide, choisissez une case entre 1 et 6 contenant des "
           "graines\n");
  }
}

// Fonction pour vérifier si un joueur est en partie
int est_en_partie(DonneesClient *donnees) {
  return donnees->numero_joueur == 1 || donnees->numero_joueur == 2;
}

// Fonction de nettoyage
void cleanup() {
  // Libération des ressources si nécessaire
}

// Gestionnaire de signal
void gestionnaire_signal(int signum) {
  printf("\nSignal %d reçu. Nettoyage et fermeture...\n", signum);
  if (descripteur_socket_global != -1) {
    close(descripteur_socket_global);
  }
  if (donnees_globales != NULL) {
    free(donnees_globales);
  }
  exit(signum);
}

int main(int argc, char **argv) {
  if (argc != 3) {
    printf("Usage: %s ip_serveur port\n", argv[0]);
    return 1;
  }

  int descripteur_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (descripteur_socket < 0) {
    perror("Échec de création de la socket");
    return 1;
  }

  struct sockaddr_in adresse_serveur;
  memset(&adresse_serveur, 0, sizeof(adresse_serveur));
  adresse_serveur.sin_family = AF_INET;
  adresse_serveur.sin_addr.s_addr = inet_addr(argv[1]);
  adresse_serveur.sin_port = htons(atoi(argv[2]));

  if (connect(descripteur_socket, (struct sockaddr *)&adresse_serveur,
              sizeof(adresse_serveur)) < 0) {
    perror("Échec de connexion");
    close(descripteur_socket);
    return 1;
  }

  char pseudo[TAILLE_MAX_PSEUDO];
  char buffer[TAILLE_BUFFER];
  int login_successful = 0;

  while (!login_successful) {
    printf("Entrez votre pseudo: ");
    if (fgets(pseudo, TAILLE_MAX_PSEUDO, stdin) == NULL) {
      printf("Erreur de lecture du pseudo\n");
      close(descripteur_socket);
      return 1;
    }
    pseudo[strcspn(pseudo, "\n")] = 0;

    snprintf(buffer, TAILLE_BUFFER, "LOGIN %s", pseudo);
    if (send(descripteur_socket, buffer, strlen(buffer), 0) < 0) {
      perror("Échec d'envoi");
      close(descripteur_socket);
      return 1;
    }

    memset(buffer, 0, TAILLE_BUFFER);
    ssize_t octets_lus = recv(descripteur_socket, buffer, TAILLE_BUFFER - 1, 0);
    if (octets_lus <= 0) {
      printf("Erreur de lecture de la réponse du serveur\n");
      close(descripteur_socket);
      return 1;
    }
    buffer[octets_lus] = '\0';

    if (strncmp(buffer, "ERROR", 5) == 0) {
      printf("%s\n", buffer + 6);
      continue; // Redemander un pseudo
    } else if (strcmp(buffer, "LOGIN_OK") == 0) {
      login_successful = 1;
    } else {
      printf("Réponse inattendue du serveur: %s\n", buffer);
      close(descripteur_socket);
      return 1;
    }
  }

  printf("Connecté au serveur! Tapez /help pour la liste des commandes\n");

  DonneesClient *donnees = malloc(sizeof(DonneesClient));
  donnees->socket = descripteur_socket;
  strncpy(donnees->pseudo, pseudo, TAILLE_MAX_PSEUDO - 1);
  donnees->pseudo[TAILLE_MAX_PSEUDO - 1] = '\0';
  donnees->numero_joueur = 0;

  init_awale(&donnees->jeu, "vide", "vide");

  pthread_t thread_reception;
  pthread_create(&thread_reception, NULL, recevoir_messages, donnees);

  // Configuration du gestionnaire de signaux
  struct sigaction sa;
  sa.sa_handler = gestionnaire_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  // Intercepter tous les signaux qui peuvent causer un arrêt
  sigaction(SIGABRT, &sa, NULL); // Abort
  sigaction(SIGFPE, &sa, NULL);  // Floating point exception
  sigaction(SIGILL, &sa, NULL);  // Illegal instruction
  sigaction(SIGINT, &sa, NULL);  // Interrupt (Ctrl+C)
  sigaction(SIGSEGV, &sa, NULL); // Segmentation fault
  sigaction(SIGTERM, &sa, NULL); // Termination
  sigaction(SIGQUIT, &sa, NULL); // Quit
  sigaction(SIGTSTP, &sa, NULL); // Stop typed at terminal (Ctrl+Z)
  sigaction(SIGTTIN, &sa, NULL); // Terminal input
  sigaction(SIGTTOU, &sa, NULL); // Terminal output
  sigaction(SIGUSR1, &sa, NULL); // User-defined 1
  sigaction(SIGUSR2, &sa, NULL); // User-defined 2
  sigaction(SIGPIPE, &sa, NULL); // Broken pipe
  sigaction(SIGALRM, &sa, NULL); // Alarm clock
  sigaction(SIGCHLD, &sa, NULL); // Child status changed
  sigaction(SIGCONT, &sa, NULL); // Continue if stopped
  sigaction(SIGHUP, &sa, NULL);  // Hangup
  sigaction(SIGBUS, &sa, NULL);  // Bus error

  // Ignorer SIGPIPE pour éviter l'arrêt sur broken pipe
  signal(SIGPIPE, SIG_IGN);

  // Sauvegarder les références pour le gestionnaire de signaux
  descripteur_socket_global = descripteur_socket;
  donnees_globales = donnees;

  while (1) {
    fgets(buffer, TAILLE_BUFFER, stdin);
    buffer[strcspn(buffer, "\n")] = 0;

    if (strcmp(buffer, "/help") == 0) {
      afficher_aide();
    } else if (strcmp(buffer, "/list") == 0) {
      envoyer_commande_simple(descripteur_socket, "LIST");
    } else if (strncmp(buffer, "/create-bio", 11) == 0) {
      gerer_creation_bio(descripteur_socket, buffer);
    } else if (strncmp(buffer, "/check-bio", 10) == 0) {
      gerer_consultation_bio(descripteur_socket, buffer);
    } else if (strcmp(buffer, "/games") == 0) {
      envoyer_commande_simple(descripteur_socket, "GAMES");
    } else if (strncmp(buffer, "/challenge", 10) == 0 ||
               strncmp(buffer, "/c", 2) == 0) {
      gerer_defi(descripteur_socket, buffer);
    } else if (strncmp(buffer, "/accept", 7) == 0 ||
               strncmp(buffer, "/a", 2) == 0) {
      gerer_acceptation(descripteur_socket, buffer);
    } else if (strncmp(buffer, "/observe", 8) == 0) {
      gerer_observation(descripteur_socket, buffer);
    } else if (strncmp(buffer, "/message", 8) == 0) {
      gerer_message(descripteur_socket, buffer);
    } else if (strncmp(buffer, "/history", 9) == 0) {
      envoyer_commande_simple(descripteur_socket, "HISTORY");
    } else if (strcmp(buffer, "/quit") == 0) {
      if (est_en_partie(donnees)) {
        envoyer_commande_simple(descripteur_socket, "FORFEIT");
        printf("Abandon de la partie en cours...\n");
      }
      printf("Au revoir!\n");
      break;
    } else if (strcmp(buffer, "/forfeit") == 0 || strcmp(buffer, "/ff") == 0) {
      envoyer_commande_simple(descripteur_socket, "FORFEIT");
      printf("Vous avez abandonné la partie.\n");
    } else if (buffer[0] >= '1' && buffer[0] <= '6') {
      gerer_coup(descripteur_socket, buffer, donnees);
    } else {
      printf("Commande inconnue. Tapez /help pour la liste des commandes\n");
    }
  }

  pthread_cancel(thread_reception);
  free(donnees);
  close(descripteur_socket);
  cleanup();
  return 0;
}
