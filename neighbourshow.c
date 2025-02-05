/****************************************************
 * neighborshow.c
 *
 * Compilation :
 *    gcc neighborshow.c -o neighborshow
 *
 * Exécution (exemples) :
 *    ./neighborshow
 *    ./neighborshow -hop 1
 *    ./neighborshow -hop 2
 *
 * Explications :
 *  - Envoie un broadcast sur 255.255.255.255:9999
 *  - Message du type "NEIGHBOR_DISCOVERY message_id=XXX hop=N origin=YYY"
 *  - Attend 2s de réponses
 *  - Stocke et affiche les hostnames reçus
 ****************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>

#define AGENT_PORT 9999
#define BUFFER_SIZE 1024

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-hop n]\n", prog);
    exit(EXIT_FAILURE);
}

// Récupération du hostname local pour 'origin'
static void get_local_hostname(char *buf, size_t buflen) {
    if (gethostname(buf, buflen) != 0) {
        perror("gethostname");
        strncpy(buf, "UnknownHost", buflen);
        buf[buflen-1] = '\0';
    }
}

int main(int argc, char *argv[]) {
    int hop = 1; // par défaut
    // Lecture des arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-hop") == 0) {
            if (i+1 < argc) {
                hop = atoi(argv[++i]);
                if (hop < 1) {
                    fprintf(stderr, "Valeur de hop invalide.\n");
                    return 1;
                }
            } else {
                usage(argv[0]);
            }
        } else {
            usage(argv[0]);
        }
    }

    // Création du socket pour l'envoi et la réception
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    // Autoriser le broadcast
    int broadcastEnable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST,
                   &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        perror("setsockopt(SO_BROADCAST)");
        close(sockfd);
        return 1;
    }

    // Lier le socket à un port local (optionnel, pour recevoir les réponses)
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family      = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port        = 0; // port aléatoire
    if (bind(sockfd, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    // Mettre un timeout de 2 secondes pour la réception
    struct timeval tv;
    tv.tv_sec  = 2;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,
                   &tv, sizeof(tv)) < 0) {
        perror("setsockopt(SO_RCVTIMEO)");
        close(sockfd);
        return 1;
    }

    // Préparer l'adresse de broadcast (LAN local)
    struct sockaddr_in bcast_addr;
    memset(&bcast_addr, 0, sizeof(bcast_addr));
    bcast_addr.sin_family      = AF_INET;
    bcast_addr.sin_port        = htons(AGENT_PORT);
    bcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    // Générer un message_id aléatoire
    srand(time(NULL));
    int message_id = rand() % 100000; // pas ultra unique, mais suffisant pour démo

    // Récupérer le hostname local
    char myhostname[256];
    get_local_hostname(myhostname, sizeof(myhostname));

    // Construire la requête
    // Format : "NEIGHBOR_DISCOVERY message_id=1234 hop=2 origin=MonHost"
    char request[BUFFER_SIZE];
    snprintf(request, sizeof(request),
             "NEIGHBOR_DISCOVERY message_id=%d hop=%d origin=%s",
             message_id, hop, myhostname);

    // Envoi broadcast
    ssize_t sent = sendto(sockfd, request, strlen(request), 0,
                          (struct sockaddr*)&bcast_addr, sizeof(bcast_addr));
    if (sent < 0) {
        perror("sendto");
        close(sockfd);
        return 1;
    }

    // Pour éviter les doublons, on stocke dans un tableau (simple)
    #define MAX_NEIGHBORS 1000
    char neighbors[MAX_NEIGHBORS][256];
    int neighbor_count = 0;

    // Réception de réponses
    while (1) {
        struct sockaddr_in sender_addr;
        socklen_t sender_len = sizeof(sender_addr);
        char buffer[BUFFER_SIZE];

        ssize_t recvlen = recvfrom(sockfd, buffer, sizeof(buffer)-1, 0,
                                   (struct sockaddr*)&sender_addr, &sender_len);
        if (recvlen < 0) {
            // Timeout ou erreur
            break;
        }
        buffer[recvlen] = '\0';

        // Le message reçu est censé être un hostname
        // On vérifie simplement s'il est déjà connu
        int known = 0;
        for (int i = 0; i < neighbor_count; i++) {
            if (strcmp(neighbors[i], buffer) == 0) {
                known = 1;
                break;
            }
        }
        if (!known && neighbor_count < MAX_NEIGHBORS) {
            strncpy(neighbors[neighbor_count], buffer, sizeof(neighbors[neighbor_count]) - 1);
            neighbors[neighbor_count][sizeof(neighbors[neighbor_count]) - 1] = '\0';
            neighbor_count++;
        }
    }

    close(sockfd);

    // Affichage des résultats
    printf("=== Neighbors trouvés (hop=%d) ===\n", hop);
    if (neighbor_count == 0) {
        printf("Aucun voisin détecté.\n");
    } else {
        for (int i = 0; i < neighbor_count; i++) {
            printf("- %s\n", neighbors[i]);
        }
    }

    return 0;
}
