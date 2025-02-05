/****************************************************************************
 * neighboragent.c
 *
 * Usage:
 *   gcc -o neighboragent neighboragent.c
 *   ./neighboragent &
 *
 * Agent d'écoute UDP sur le port 9999 :
 *  - Reçoit les requêtes (discovery_request_t).
 *  - Répond avec le hostname local.
 *  - Si req.hops > 1, retransmet la requête sur toutes les interfaces
 *    (broadcast) en décrémentant req.hops.
 ****************************************************************************/

#define _DEFAULT_SOURCE

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
#include <sys/ioctl.h>
#include <net/if.h>

/* Paramètres */
#define DISCOVERY_PORT 9999
#define MAX_BUF       1024

/* Structure de la requête */
typedef struct {
    unsigned int request_id; 
    int hops;
} discovery_request_t;

/* Cache pour éviter le traitement répété de la même requête */
#define CACHE_SIZE 100

typedef struct {
    unsigned int request_id;
    time_t       timestamp;
} request_cache_entry_t;

request_cache_entry_t cache[CACHE_SIZE];

/* Ajoute un request_id dans le cache */
void add_to_cache(unsigned int request_id) {
    time_t now = time(NULL);
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].request_id == 0) {
            cache[i].request_id = request_id;
            cache[i].timestamp  = now;
            return;
        }
    }
    /* Si plein, écrase la plus vieille entrée */
    int oldest_index = 0;
    time_t oldest_time = cache[0].timestamp;
    for (int i = 1; i < CACHE_SIZE; i++) {
        if (cache[i].timestamp < oldest_time) {
            oldest_time = cache[i].timestamp;
            oldest_index = i;
        }
    }
    cache[oldest_index].request_id = request_id;
    cache[oldest_index].timestamp  = now;
}

/* Vérifie si request_id est dans le cache */
bool is_in_cache(unsigned int request_id) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].request_id == request_id) {
            return true;
        }
    }
    return false;
}

/**
 * Re‐diffuse la requête en broadcast sur toutes les interfaces locales.
 * (Déconseillé sur un gros réseau pour éviter les tempêtes de broadcast.)
 */
static void rebroadcast_request(int sockfd, discovery_request_t *req)
{
    // Autoriser le broadcast
    int broadcast_enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST,
                   &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        perror("setsockopt - SO_BROADCAST");
        return;
    }

    // Récupère la liste des interfaces
    struct ifconf ifc;
    struct ifreq  ifr[20];
    memset(&ifc, 0, sizeof(ifc));
    ifc.ifc_req = ifr;
    ifc.ifc_len = sizeof(ifr);

    if (ioctl(sockfd, SIOCGIFCONF, &ifc) == -1) {
        perror("ioctl - SIOCGIFCONF");
        return;
    }

    int nifs = ifc.ifc_len / (int)sizeof(struct ifreq);
    for (int i = 0; i < nifs; i++) {
        // Vérifie si l’interface est UP et qu’elle supporte le broadcast
        struct ifreq ifr_flags;
        memset(&ifr_flags, 0, sizeof(ifr_flags));
        strncpy(ifr_flags.ifr_name, ifr[i].ifr_name, IFNAMSIZ - 1);

        if (ioctl(sockfd, SIOCGIFFLAGS, &ifr_flags) == -1) {
            continue;
        }
        short flags = ifr_flags.ifr_flags;
        if ((flags & IFF_UP) == 0)        continue;
        if ((flags & IFF_BROADCAST) == 0) continue;

        // Récupère l’adresse de broadcast
        struct ifreq ifr_brd;
        memset(&ifr_brd, 0, sizeof(ifr_brd));
        strncpy(ifr_brd.ifr_name, ifr[i].ifr_name, IFNAMSIZ - 1);

        if (ioctl(sockfd, SIOCGIFBRDADDR, &ifr_brd) == -1) {
            // Peut échouer sur interfaces sans broadcast
            continue;
        }

        struct sockaddr_in *baddr = (struct sockaddr_in *)&ifr_brd.ifr_broadaddr;

        if (sendto(sockfd, req, sizeof(*req), 0,
                   (struct sockaddr *)baddr, sizeof(*baddr)) < 0) {
            perror("sendto - rebroadcast");
        }
    }

    // Optionnel : désactiver de nouveau le broadcast
    broadcast_enable = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST,
               &broadcast_enable, sizeof(broadcast_enable));
}

int main() {
    /* Nettoyage du cache */
    memset(cache, 0, sizeof(cache));

    /* Création du socket UDP */
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /* Bind sur le port DISCOVERY_PORT */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(DISCOVERY_PORT);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("neighboragent: en écoute sur le port %d\n", DISCOVERY_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        discovery_request_t req;
        memset(&req, 0, sizeof(req));

        int recv_len = recvfrom(sockfd, &req, sizeof(req), 0,
                                (struct sockaddr *)&client_addr, &addr_len);
        if (recv_len < 0) {
            perror("recvfrom");
            continue;
        }

        if (recv_len == (int)sizeof(discovery_request_t)) {
            // Vérifie si la requête est déjà connue
            if (!is_in_cache(req.request_id)) {
                // Nouvelle requête
                add_to_cache(req.request_id);

                // Réponse : on envoie le nom d’hôte local
                char hostname[128];
                gethostname(hostname, sizeof(hostname));
                if (sendto(sockfd, hostname, strlen(hostname)+1, 0,
                           (struct sockaddr *)&client_addr, addr_len) < 0) {
                    perror("sendto - hostname");
                }

                // Si on doit relayer
                if (req.hops > 1) {
                    req.hops--;
                    rebroadcast_request(sockfd, &req);
                }
            }
            // sinon, on ignore (déjà traité)
        }
        else {
            // Paquet mal formé => on ignore
        }
    }

    close(sockfd);
    return 0;
}
