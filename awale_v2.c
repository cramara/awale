// awale_v2.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RESET_COLOR "\033[0m"
#define GREEN_TEXT "\033[32m"
#define MAX_PSEUDO_LENGTH 50

typedef struct {
  unsigned int
      plateau[12]; // les trous du plateau (0-5 pour J1 et 6-11 pour J2)
  unsigned int scoreJ1, scoreJ2; // les scores des joueurs
  unsigned int joueurCourant;    // le joueur qui doit jouer (1 ou 2)
  unsigned int fini;             // 1 si la partie est terminée, 0 sinon
  unsigned int gagnant; // le joueur gagnant (0 : personne, 1 : J1, 2 : J2)
  char pseudo1[50];
  char pseudo2[50];
} Awale;

void init_awale(Awale *jeu, char *pseudo1, char *pseudo2) {
  // Initialise chaque trou avec 4 graines
  for (int i = 0; i < 12; i++) {
    jeu->plateau[i] = 4;
  }

  // Réinitialise les scores à 0
  jeu->scoreJ1 = 0;
  jeu->scoreJ2 = 0;
  // Choisit aléatoirement le premier joueur (1 ou 2)
  jeu->joueurCourant = (rand() % 2) + 1;
  jeu->fini = 0;
  jeu->gagnant = 0;
  strcpy(jeu->pseudo1, pseudo1);
  strcpy(jeu->pseudo2, pseudo2);
}

// Vérifie si un coup est valide
int coup_valide(Awale *jeu, int trou) {
  if (trou < 1 || trou > 6)
    return 0;

  int index = jeu->joueurCourant == 1 ? trou - 1 : trou + 5;
  return jeu->plateau[index] > 0;
}

// Vérifie si le joueur est en famine
int est_en_famine(Awale *jeu, int joueur) {
  int debut = joueur == 1 ? 0 : 6;
  int graines_totales = 0;

  for (int i = debut; i < debut + 6; i++) {
    graines_totales += jeu->plateau[i];
  }

  return graines_totales == 0;
}

// Capture les graines si possible
void capturer_graines(Awale *jeu, int derniere_case) {
  int est_chez_adversaire =
      jeu->joueurCourant == 1 ? derniere_case >= 6 : derniere_case < 6;

  while (est_chez_adversaire) {
    if (jeu->plateau[derniere_case] == 2 || jeu->plateau[derniere_case] == 3) {
      if (jeu->joueurCourant == 1) {
        jeu->scoreJ1 += jeu->plateau[derniere_case];
      } else {
        jeu->scoreJ2 += jeu->plateau[derniere_case];
      }
      jeu->plateau[derniere_case] = 0;
      derniere_case = (derniere_case - 1 + 12) % 12;
      est_chez_adversaire =
          jeu->joueurCourant == 1 ? derniere_case >= 6 : derniere_case < 6;
    } else {
      break;
    }
  }
}

// Joue un coup
int jouer_coup(Awale *jeu, int trou) {
  if (!coup_valide(jeu, trou))
    return 0;

  // Calcul de l'index initial en fonction du joueur courant
  int index = jeu->joueurCourant == 1 ? trou - 1 : trou + 5;

  // Nombre de graines à distribuer
  int graines = jeu->plateau[index];
  jeu->plateau[index] = 0;

  // Distribution des graines
  int pos = index;
  while (graines > 0) {
    pos = (pos + 1) % 12;
    if (pos != index) {
      jeu->plateau[pos]++;
      graines--;
    }
  }

  // Capture des graines
  capturer_graines(jeu, pos);

  // Vérification de fin de partie
  if (jeu->scoreJ1 >= 25 || jeu->scoreJ2 >= 25) {
    jeu->fini = 1;
    jeu->gagnant = jeu->scoreJ1 >= 25 ? 1 : 2;
  } else if (est_en_famine(jeu, jeu->joueurCourant == 1 ? 2 : 1)) {
    jeu->fini = 1;
    jeu->gagnant = jeu->joueurCourant;
  }

  // Changement de joueur
  jeu->joueurCourant = jeu->joueurCourant == 1 ? 2 : 1;

  return 1;
}

// Convertit l'état du jeu en chaîne pour l'envoi réseau
char *serialiser_jeu(Awale *jeu) {
  static char buffer[256];
  char *ptr = buffer;

  // Format: "GAMESTATE p0 p1 ... p11 scoreJ1 scoreJ2 joueurCourant fini
  // gagnant"
  // ptr += sprintf(ptr, "GAMESTATE");
  for (int i = 0; i < 12; i++) {
    ptr += sprintf(ptr, " %d", jeu->plateau[i]);
  }
  sprintf(ptr, " %d %d %d %d %d %s %s", jeu->scoreJ1, jeu->scoreJ2,
          jeu->joueurCourant, jeu->fini, jeu->gagnant, jeu->pseudo1,
          jeu->pseudo2);

  return buffer;
}

// Reconstruit l'état du jeu depuis une chaîne réseau
void deserialiser_jeu(Awale *jeu, const char *buffer) {
  if (strncmp(buffer, "GAMESTATE", 9) != 0)
    return;

  const char *ptr = buffer + 10; // Skip "GAMESTATE "

  for (int i = 0; i < 12; i++) {
    jeu->plateau[i] = atoi(ptr);
    while (*ptr && *ptr != ' ')
      ptr++;
    ptr++;
  }

  jeu->scoreJ1 = atoi(ptr);
  while (*ptr && *ptr != ' ')
    ptr++;
  ptr++;

  jeu->scoreJ2 = atoi(ptr);
  while (*ptr && *ptr != ' ')
    ptr++;
  ptr++;

  jeu->joueurCourant = atoi(ptr);
  while (*ptr && *ptr != ' ')
    ptr++;
  ptr++;

  jeu->fini = atoi(ptr);
  while (*ptr && *ptr != ' ')
    ptr++;
  ptr++;

  jeu->gagnant = atoi(ptr);
  while (*ptr && *ptr != ' ')
    ptr++;
  ptr++;

  // Copier le premier pseudo jusqu'au premier espace
  const char *espace = memchr(ptr, ' ', MAX_PSEUDO_LENGTH);
  if (espace != NULL) {
    size_t len = espace - ptr;
    memcpy(jeu->pseudo1, ptr, len);
    jeu->pseudo1[len] = '\0';
    ptr = espace + 1;
  }

  // Pour le deuxième pseudo, aller jusqu'au prochain espace
  espace = memchr(ptr, ' ', MAX_PSEUDO_LENGTH);
  if (espace != NULL) {
    size_t len = espace - ptr;
    memcpy(jeu->pseudo2, ptr, len);
    jeu->pseudo2[len] = '\0';
    ptr = espace + 1;
  } else {
    // Si pas d'espace trouvé, c'est la fin du buffer
    strcpy(jeu->pseudo2, ptr);
  }
}

// Affiche le plateau
void afficher_plateau(Awale *jeu, int perspective_joueur) {
  printf("\n   -------------------------------\n");
  printf("   |");
  if (perspective_joueur == 1) {
    for (int i = 11; i >= 6; i--) {
      printf(" %2d |", jeu->plateau[i]);
    }
  } else {
    for (int i = 5; i >= 0; i--) {
      printf(" %2d |", jeu->plateau[i]);
    }
  }
  printf("\n   -------------------------------\n");
  printf("   |");
  if (perspective_joueur == 1) {
    for (int i = 0; i < 6; i++) {
      printf(GREEN_TEXT " %2d " RESET_COLOR "|", jeu->plateau[i]);
    }
  } else {
    for (int i = 6; i < 12; i++) {
      printf(GREEN_TEXT " %2d " RESET_COLOR "|", jeu->plateau[i]);
    }
  }
  printf("\n   -------------------------------\n");
  printf("      1    2    3    4    5    6\n");

  printf("\nScores:\n");
  printf("%s : %d\n", jeu->pseudo1, jeu->scoreJ1);
  printf("%s : %d\n", jeu->pseudo2, jeu->scoreJ2);
  printf("Tour du joueur %s\n",
         jeu->joueurCourant == 1 ? jeu->pseudo1 : jeu->pseudo2);
}
