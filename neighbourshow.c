/****************************************************************************
 * neighborshow.c
 *
 * Programme client qui diffuse une requête de découverte sur le réseau
 * et attend les réponses des agents (neighboragent) pour afficher la
 * liste des noms de machines voisines. Par défaut, n=1 saut; si on
 * utilise "-hop n", on peut augmenter la portée des sauts.
 *
 * Compilation:
 *   gcc -o neighborshow neighborshow.c
 *
 * Exécution:
 *   ./neighborshow
 *   ./neighborshow -hop 2
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <stdbool.h>
#include <sys/select.h>

/* On réutilise la même structure que dans neighboragent */
typedef struct {
    unsigned int request_id;
    int hops;
} discovery_request_t;

/* Même port que l'agent */
#define DISCOVERY_PORT 9999

/* Durée d'écoute des réponses (en secondes) */
#define RESPONSE_WAIT_TIME 3

/* Taille max des buffers */
#define MAX_BUF 1024

/* Fonction utilitaire pour générer un ID de requête pseudo-aléatoire */
unsigned int generate_request_id() {
    srand(time(NULL) ^ getpid());
    return rand();
}

int main(int argc, char *argv[]) {
    int hops = 1; // par défaut, un seul saut
    if (argc == 3 && strcmp(argv[1], "-hop") == 0) {
        hops = atoi(argv[2]);
        if (hops < 1) {
            fprintf(stderr, "Nombre de sauts invalide, utilisation de 1 par défaut.\n");
            hops = 1;
        }
    }
    else if (argc != 1) {
        fprintf(stderr, "Usage : %s [-hop n]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /* Création d’un socket UDP */
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /* On prépare l'adresse de broadcast */
    struct sockaddr_in broadcast_addr;
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(DISCOVERY_PORT);
    broadcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    /* On active le broadcast sur notre socket */
    int broadcast_enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST,
                   &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        perror("setsockopt - SO_BROADCAST");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* Préparation de la requête */
    discovery_request_t req;
    req.request_id = generate_request_id();  // identifiant unique
    req.hops = hops;

    /* Envoi de la requête en broadcast */
    if (sendto(sockfd, &req, sizeof(req), 0,
               (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr)) < 0) {
        perror("sendto");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* On va maintenant écouter pendant un certain délai les réponses */
    printf("neighborshow: requête envoyée (hops = %d). Attente des réponses...\n", hops);

    /* Pour stocker les noms reçus sans doublons, on peut utiliser un tableau,
     * ou un set. Ici, par simplicité, on fera un petit tableau et on vérifie
     * manuellement. */
    #define MAX_HOSTS 100
    char hosts[MAX_HOSTS][MAX_BUF];
    int host_count = 0;

    /* Début de l'intervalle d'écoute */
    time_t start_time = time(NULL);

    while (1) {
        /* On calcule le temps écoulé */
        time_t now = time(NULL);
        if (difftime(now, start_time) > RESPONSE_WAIT_TIME) {
            /* On arrête d'écouter après RESPONSE_WAIT_TIME secondes */
            break;
        }

        /* On utilise select() pour attendre les données sans bloquer trop longtemps */
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = 1;        // on vérifie toutes les 1 seconde
        timeout.tv_usec = 0;

        int retval = select(sockfd+1, &read_fds, NULL, NULL, &timeout);
        if (retval < 0) {
            perror("select");
            break;
        }
        else if (retval == 0) {
            /* Pas de données, on boucle pour voir si on dépasse RESPONSE_WAIT_TIME */
            continue;
        }
        else {
            /* Il y a potentiellement des données à lire */
            if (FD_ISSET(sockfd, &read_fds)) {
                char buf[MAX_BUF];
                struct sockaddr_in src_addr;
                socklen_t src_len = sizeof(src_addr);

                memset(buf, 0, sizeof(buf));
                int len = recvfrom(sockfd, buf, sizeof(buf), 0,
                                   (struct sockaddr*)&src_addr, &src_len);
                if (len > 0) {
                    /* On a reçu une réponse (nom d’hôte) */
                    // Vérifier si on l’a déjà
                    bool found = false;
                    for (int i = 0; i < host_count; i++) {
                        if (strncmp(hosts[i], buf, MAX_BUF) == 0) {
                            found = true;
                            break;
                        }
                    }
                    if (!found && host_count < MAX_HOSTS) {
                        strncpy(hosts[host_count], buf, MAX_BUF-1);
                        host_count++;
                    }
                }
            }
        }
    }

    close(sockfd);

    /* Affichage du résultat */
    printf("neighborshow: liste des machines trouvées (hops=%d):\n", hops);
    for (int i = 0; i < host_count; i++) {
        printf(" - %s\n", hosts[i]);
    }

    return 0;
}

