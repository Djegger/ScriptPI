/****************************************************************************
 * neighborshow.c
 *
 * Usage:
 *   gcc -o neighborshow neighborshow.c
 *   ./neighborshow
 *   ./neighborshow -hop 2
 *
 * Envoie une requête de découverte en broadcast et écoute les réponses.
 * Le paramètre -hop n est passé à l'agent (neighboragent).
 ****************************************************************************/

#define _DEFAULT_SOURCE

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

/* Même structure que dans neighboragent */
typedef struct {
    unsigned int request_id;
    int hops;
} discovery_request_t;

#define DISCOVERY_PORT      9999
#define RESPONSE_WAIT_TIME  3
#define MAX_BUF            1024

/* Génération d’un ID unique (pseudo-aléatoire) */
unsigned int generate_request_id() {
    srand(time(NULL) ^ getpid());
    return rand();
}

int main(int argc, char *argv[]) {
    int hops = 1; // Valeur par défaut
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

    /* Création du socket UDP */
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /* Autoriser le broadcast sur ce socket */
    int broadcast_enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST,
                   &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        perror("setsockopt - SO_BROADCAST");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* Adresse de broadcast local (255.255.255.255) */
    struct sockaddr_in broadcast_addr;
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family      = AF_INET;
    broadcast_addr.sin_port        = htons(DISCOVERY_PORT);
    broadcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    /* Préparation de la requête */
    discovery_request_t req;
    req.request_id = generate_request_id();
    req.hops       = hops;

    /* Envoi de la requête */
    if (sendto(sockfd, &req, sizeof(req), 0,
               (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr)) < 0) {
        perror("sendto");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("neighborshow: requête envoyée (hops=%d). Attente des réponses...\n", hops);

    /* Récupération des réponses (noms d’hôte), en évitant les doublons */
    #define MAX_HOSTS 100
    char hosts[MAX_HOSTS][MAX_BUF];
    int  host_count = 0;

    time_t start_time = time(NULL);

    while (1) {
        if (time(NULL) - start_time > RESPONSE_WAIT_TIME) {
            break;  // On arrête après RESPONSE_WAIT_TIME secondes
        }

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);

        struct timeval timeout;
        timeout.tv_sec  = 1;
        timeout.tv_usec = 0;

        int retval = select(sockfd+1, &read_fds, NULL, NULL, &timeout);
        if (retval < 0) {
            perror("select");
            break;
        }
        else if (retval == 0) {
            // Rien reçu pendant 1 seconde, on reteste
            continue;
        }
        else {
            if (FD_ISSET(sockfd, &read_fds)) {
                char buf[MAX_BUF];
                struct sockaddr_in src_addr;
                socklen_t src_len = sizeof(src_addr);

                memset(buf, 0, sizeof(buf));
                int len = recvfrom(sockfd, buf, sizeof(buf), 0,
                                   (struct sockaddr*)&src_addr, &src_len);
                if (len > 0) {
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

    /* Affichage */
    printf("neighborshow: machines découvertes (hops=%d):\n", hops);
    for (int i = 0; i < host_count; i++) {
        printf(" - %s\n", hosts[i]);
    }

    return 0;
}
