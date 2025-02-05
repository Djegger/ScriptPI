/****************************************************************************
 * neighboragent.c
 *
 * Agent réseau persistant, à exécuter sur chaque machine.
 * Il écoute sur un port UDP (par exemple 9999) et répond aux requêtes
 * de découverte en renvoyant le nom de la machine. Il relaie la requête
 * si le nombre de sauts (TTL / hop) n’est pas nul.
 *
 * Compilation:
 *   gcc -o neighboragent neighboragent.c
 *
 * Exécution (en root ou avec les privilèges nécessaires si on utilise un port < 1024):
 *   ./neighboragent &
 *
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdbool.h>
#include <time.h>

/* Port UDP utilisé pour la découverte */
#define DISCOVERY_PORT 9999

/* On définit la taille de buffer max pour nos messages */
#define MAX_BUF 1024

/* Structure de la requête de découverte */
typedef struct {
    unsigned int request_id; // identifiant unique de la requête
    int hops;                // nombre de sauts (TTL)
} discovery_request_t;

/* Pour éviter les re-transmissions infinies, on garde en mémoire
 * les requêtes déjà traitées récemment (via leur request_id).
 * Dans une version réelle, on peut utiliser une table de hachage
 * ou un cache. Ici, on fera au plus simple: un petit tableau. */
#define CACHE_SIZE 100

typedef struct {
    unsigned int request_id;
    time_t timestamp; // pour éventuellement expirer les vieilles requêtes
} request_cache_entry_t;

request_cache_entry_t cache[CACHE_SIZE];

/* Ajoute un request_id au cache */
void add_to_cache(unsigned int request_id) {
    time_t now = time(NULL);
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].request_id == 0) {
            cache[i].request_id = request_id;
            cache[i].timestamp = now;
            return;
        }
    }
    /* Si le cache est plein, on écrase le plus ancien (simple heuristique) */
    int oldest_index = 0;
    time_t oldest_time = cache[0].timestamp;
    for (int i = 1; i < CACHE_SIZE; i++) {
        if (cache[i].timestamp < oldest_time) {
            oldest_time = cache[i].timestamp;
            oldest_index = i;
        }
    }
    cache[oldest_index].request_id = request_id;
    cache[oldest_index].timestamp = now;
}

/* Vérifie si request_id est déjà dans le cache */
bool is_in_cache(unsigned int request_id) {
    time_t now = time(NULL);
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].request_id == request_id) {
            /* Optionnel: on pourrait vérifier l'âge et supprimer si trop vieux */
            return true;
        }
    }
    return false;
}

int main() {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    /* Nettoyage du cache au démarrage */
    memset(cache, 0, sizeof(cache));

    /* Création d'un socket UDP */
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /* Préparation de l'adresse serveur (agent) */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;  /* écoute sur toutes les interfaces */
    server_addr.sin_port = htons(DISCOVERY_PORT);

    /* On associe le socket à l'adresse et au port */
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("neighboragent: démarré, en écoute sur le port %d\n", DISCOVERY_PORT);

    while (1) {
        /* On reçoit un message (potentiellement une requête de découverte) */
        discovery_request_t req;
        memset(&req, 0, sizeof(req));

        int recv_len = recvfrom(sockfd, &req, sizeof(req), 0,
                                (struct sockaddr *)&client_addr, &addr_len);
        if (recv_len < 0) {
            perror("recvfrom");
            continue;
        }

        /* Vérification basique : si c'est une requête valide */
        if (recv_len == sizeof(discovery_request_t)) {
            /* Vérifions si on a déjà traité cette requête (via son ID) */
            if (!is_in_cache(req.request_id)) {
                /* Si ce n'est pas dans le cache, on l'ajoute */
                add_to_cache(req.request_id);

                /* On répond au client en lui envoyant notre hostname */
                char hostname[128];
                gethostname(hostname, sizeof(hostname));

                /* Envoi de la réponse */
                sendto(sockfd, hostname, strlen(hostname) + 1, 0,
                       (struct sockaddr *)&client_addr, addr_len);

                /* On relaie (broadcast) la requête si hops > 1 */
                if (req.hops > 1) {
                    req.hops--;

                    /* Configuration d'adresse pour le broadcast */
                    struct sockaddr_in broadcast_addr;
                    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
                    broadcast_addr.sin_family = AF_INET;
                    broadcast_addr.sin_port = htons(DISCOVERY_PORT);
                    broadcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

                    /* On doit activer l'option broadcast sur le socket */
                    int broadcast_enable = 1;
                    setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST,
                               &broadcast_enable, sizeof(broadcast_enable));

                    /* On envoie la requête modifiée en broadcast */
                    sendto(sockfd, &req, sizeof(req), 0,
                           (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));

                    /* Optionnel: désactiver le broadcast à nouveau si besoin */
                    broadcast_enable = 0;
                    setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST,
                               &broadcast_enable, sizeof(broadcast_enable));
                }
            } 
            else {
                /* On ignore la requête car on l’a déjà traitée. */
            }
        } 
        else {
            /* On peut ignorer ou logguer tout message mal formé. */
        }
    }

    close(sockfd);
    return 0;
}
