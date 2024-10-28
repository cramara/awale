#include <stdio.h>
#include <stdlib.h> // hasard
#include <string.h>
#include <unistd.h> // sleep
#include <time.h>

// Codes ANSI pour changer la couleur
#define RESET_COLOR "\033[0m"
#define GREEN_TEXT "\033[32m"
#define RED_TEXT "\033[31m"

struct awale
{
    unsigned int plateau[12];      // les trous du plateau (0-5 pour J1 et 6-11 pour J2)
    unsigned int scoreJ1, scoreJ2; // les scores des joueurs
    char *j1, *j2;                 // les noms des joueurs
    unsigned int joueurCourant;    // le joueur qui doit jouer (1 ou 2)
    unsigned int timerJ1, timerJ2; // les temps de jeu des joueurs
    unsigned int fini;             // 1 si la partie est terminée, 0 sinon
    unsigned int gagnant;          // le joueur gagnant (0 : personne, 1 : J1, 2 : J2)
} awale;

// Fonction pour initialiser le jeu
void initialiserAwale(struct awale *a, char *j1, char *j2)
{
    srand(time(NULL));
    for (int i = 0; i < 12; i++)
    {
        a->plateau[i] = 4;
    }
    a->scoreJ1 = 0;
    a->scoreJ2 = 0;
    a->j1 = j1;
    a->j2 = j2;
    a->joueurCourant = rand() % 2 == 0 ? 1 : 2;
    a->timerJ1 = 300;
    a->timerJ2 = 300;
    a->fini = 0;
    a->gagnant = 0;
}

// Fonction pour sauter des lignes
void sautLignes(int nbLignes)
{
    for (int i = 0; i < nbLignes; i++)
    {
        printf("\n");
    }
}

// Fonction pour afficher le plateau
void afficherAwale(struct awale *a)
{
    printf("Joueur courant: %s\n\n", a->joueurCourant == 1 ? a->j1 : a->j2);

    printf("   -------------------------------\n");
    printf("   |");
    if (a->joueurCourant == 1)
    {
        for (int i = 11; i >= 6; i--)
        {
            printf(" %2d |", a->plateau[i]);
        }
        printf("\n   -------------------------------\n");

        printf("   |");
        for (int i = 0; i < 6; i++)
        {
            printf(GREEN_TEXT " %2d " RESET_COLOR "|", a->plateau[i]);
        }
        printf("\n");
    }
    else
    {
        for (int i = 5; i >= 0; i--)
        {
            printf(" %2d |", a->plateau[i]);
        }
        printf("\n   -------------------------------\n");

        printf("   |");
        for (int i = 6; i < 12; i++)
        {
            printf(GREEN_TEXT " %2d " RESET_COLOR "|", a->plateau[i]);
        }
        printf("\n");
    }

    printf("   -------------------------------\n");
    printf("      1    2    3    4    5    6\n");
    printf("\nScores: \n");
    printf("%s: %d\n", a->j1, a->scoreJ1);
    printf("%s: %d\n", a->j2, a->scoreJ2);
}

// Fonction pour capturer les graines chez l'adversaire
void capturerGraines(struct awale *a, unsigned int *indexCourant)
{
    unsigned int estChezAdversaire = a->joueurCourant == 1 ? *indexCourant >= 6 : *indexCourant < 6;
    if (estChezAdversaire)
    {
        unsigned int prendreGraines = a->plateau[*indexCourant] == 2 || a->plateau[*indexCourant] == 3;

        // Si le nombre de graines est 2 ou 3, on les capture
        while (prendreGraines)
        {
            // Mise à jour du score du joueur courant
            if (a->joueurCourant == 1)
            {
                a->scoreJ1 += a->plateau[*indexCourant];
            }
            else
            {
                a->scoreJ2 += a->plateau[*indexCourant];
            }

            // On vide le trou capturé
            a->plateau[*indexCourant] = 0;

            // On vérifie la case précédente
            *indexCourant = (*indexCourant - 1 + 12) % 12;
            estChezAdversaire = a->joueurCourant == 1 ? *indexCourant >= 6 : *indexCourant < 6;

            // Si c'est encore chez l'adversaire et qu'il y a 2 ou 3 graines, on continue
            if (estChezAdversaire)
            {
                prendreGraines = a->plateau[*indexCourant] == 2 || a->plateau[*indexCourant] == 3;
            }
            else
            {
                prendreGraines = 0; // Arrêter la capture si on sort du territoire adverse
            }
        }
    }
}

// Fonction pour vérifier si un joueur est en famine
int verifierFamine(struct awale *a, unsigned int joueur)
{
    unsigned int nbGraines = 0;

    // Compter les graines dans le camp du joueur
    for (int i = 0; i < 6; i++)
    {
        nbGraines += a->plateau[joueur == 1 ? i : i + 6];
    }

    // Vérifier si le joueur n'a plus de graines
    if (nbGraines == 0)
    {
        unsigned int adversaire = (joueur == 1) ? 2 : 1;
        // Vérifier si l'adversaire ne peut pas nourrir le joueur
        unsigned int peutNourrir = 0;
        for (int i = 0; i < 6; i++)
        {
            if (a->plateau[adversaire == 1 ? i + 6 : i] > 0)
            {
                peutNourrir = 1; // L'adversaire a des graines pour nourrir
                break;
            }
        }
        // Si l'adversaire ne peut pas nourrir, la famine est déclarée
        return !peutNourrir;
    }

    return 0; // Pas de famine
}

int main()
{
    struct awale a;
    char *j1 = (char *) malloc(50 * sizeof(char));
    char *j2 = (char *) malloc(50 * sizeof(char));

    printf("Lancement du jeu Awale\n");
    printf("Joueur 1, entrez votre nom: ");
    scanf("%s", j1);
    printf("Joueur 2, entrez votre nom: ");
    scanf("%s", j2);

    initialiserAwale(&a, j1, j2);

    while (!a.fini)
    {
        afficherAwale(&a);
        sautLignes(2);

        // Demande au joueur courant de choisir un trou parmi les siens
        unsigned int trou;
        printf("Joueur %s, choisissez un trou (1-6): ", a.joueurCourant == 1 ? a.j1 : a.j2);
        if (scanf("%d", &trou) != 1) {
            printf(RED_TEXT "Entrée invalide. Veuillez entrer un chiffre." RESET_COLOR "\n");
            // Clear the input buffer
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
            continue;
        }

        // On vérifie que le trou est bien dans les limites
        if (trou < 1 || trou > 6)
        {
            printf(RED_TEXT "Le trou doit être compris entre 1 et 6" RESET_COLOR "\n");
            continue;
        }

        unsigned int indexTrou = a.joueurCourant == 1 ? trou - 1 : trou + 5;
        unsigned int nbGrainesDansTrou = a.plateau[indexTrou];

        if (nbGrainesDansTrou == 0)
        {
            printf(RED_TEXT "Le trou choisi est vide" RESET_COLOR "\n");
            continue;
        }

        // On vide le trou
        a.plateau[indexTrou] = 0;

        // Distribution circulaire des graines
        unsigned int indexCourant = indexTrou;
        while (nbGrainesDansTrou > 0)
        {
            // Avancer d'une case en respectant le cycle circulaire
            indexCourant = (indexCourant + 1) % 12;

            // Ne pas remettre de graines dans le trou d'origine
            if (indexCourant == indexTrou)
                continue;

            // Déposer une graine dans le trou actuel
            a.plateau[indexCourant]++;
            nbGrainesDansTrou--;
        }

        // Garder en mémoire le dernier trou visité et capturer les graines chez l'adversaire
        capturerGraines(&a, &indexCourant);
        afficherAwale(&a);

        // Vérification des conditions de fin de partie
        if (verifierFamine(&a, a.joueurCourant))
        {
            // Si famine, mettre à jour le gagnant
            a.fini = 1;
            a.gagnant = (a.joueurCourant == 1) ? 2 : 1; // L'adversaire gagne
            break;                                      // fin de la partie
        }

        if (a.scoreJ1 >= 25)
        {
            a.fini = 1;
            a.gagnant = 1;
            break; // fin de la partie
        }
        else if (a.scoreJ2 >= 25)
        {
            a.fini = 1;
            a.gagnant = 2;
            break; // fin de la partie
        }

        // Attendre 2 secondes pour voir les changements avant de passer à l'autre joueur
        sleep(2);

        // Changer de joueur
        a.joueurCourant = a.joueurCourant == 1 ? 2 : 1;
    }

    // Afficher le gagnant
    if (a.gagnant == 1)
    {
        printf("Bravo %s, vous avez gagné!\n", a.j1);
    }
    else if (a.gagnant == 2)
    {
        printf("Bravo %s, vous avez gagné!\n", a.j2);
    }
    else
    {
        printf("Match nul!\n");
    }

    free(j1);
    free(j2);

    return 0;
}
