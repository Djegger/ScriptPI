/****************************************************************************
 * neighboragent.c
 *
 *  - Écoute les requêtes de découverte sur le port 9999
 *  - Répond avec le nom d’hôte
 *  - Si req.hops > 1, relaie la requête sur toutes les interfaces (broadcast)
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
#include <sys/ioctl.h>
#include <net/if.h>

#define DISCOVERY_PORT   9999
#define MAX_BUF          1024

typedef struct {
    unsigned int request_id; 
    int hops; 
} discovery_request_t;

/* Cache pour éviter les boucles */
#define CACHE_SIZE 100

typedef struct {
    unsigned int request_id;
    time_t       timestamp; 
} request_cache_entry_t;

request_cache_entry_t cache[CACHE_SIZE];

/* Ajoute un request_id au cache */
void add_to_cache(unsigned int request_id) {
    time_t now = time(NULL);
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].request_id == 0) {
            cache[i].request_id = request_id;
            cache[i].timestamp  = now;
            return;
        }
    }
    /* Si le cache est plein, on écrase la plus ancienne entrée */
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

/* Vérifie si request_id est déjà dans le cache */
bool is_in_cache(unsigned int request_id) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].request_id == request_id) {
            return true;
        }
    }
    return false;
}

/**
 * \brief Re-diffuse la requête sur toutes les interfaces locales actives.
 *
 * \param sockfd   Le socket UDP déjà créé (avec SO_BROADCAST si nécessaire).
 * \param req      La requête à relayer (hops déjà décrémenté).
 *
 *  On parcourt la liste des interfaces, on récupère leur adresse de broadcast,
 *  et on fait un sendto() dessus. Le cache nous protège contre les boucles.
 */
static void rebroadcast_request(int sockfd, discovery_request_t *req)
{
    // On active éventuellement le broadcast sur ce socket
    int broadcast_enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST,
                   &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        perror("setsockopt - SO_BROADCAST");
        return;
    }

    struct ifconf ifc;
    struct ifreq  ifr[20]; // limite arbitraire : 20 interfaces
    memset(&ifc, 0, sizeof(ifc));
    ifc.ifc_req = ifr;
    ifc.ifc_len = sizeof(ifr);

    // Obtenir la liste des interfaces
    if (ioctl(sockfd, SIOCGIFCONF, &ifc) == -1) {
        perror("ioctl - SIOCGIFCONF");
        return;
    }

    int nifs = ifc.ifc_len / sizeof(struct ifreq);
    for (int i = 0; i < nifs; i++) {
        // Vérifier si l’interface est UP, et si elle supporte le broadcast
        struct ifreq ifr_flags;
        memset(&ifr_flags, 0, sizeof(ifr_flags));
        strncpy(ifr_flags.ifr_name, ifr[i].ifr_name, IFNAMSIZ - 1);

        if (ioctl(sockfd, SIOCGIFFLAGS, &ifr_flags) == -1) {
            continue;
        }
        short flags = ifr_flags.ifr_flags;
        if ((flags & IFF_UP) == 0)        continue; // Interface down
        if ((flags & IFF_BROADCAST) == 0) continue; // Pas de broadcast

        // Récupérer l’adresse de broadcast de cette interface
        struct ifreq ifr_brd;
        memset(&ifr_brd, 0, sizeof(ifr_brd));
        strncpy(ifr_brd.ifr_name, ifr[i].ifr_name, IFNAMSIZ - 1);

        if (ioctl(sockfd, SIOCGIFBRDADDR, &ifr_brd) == -1) {
            // peut échouer si pas d’adresse de broadcast configurée
            continue;
        }

        // En théorie, on pourrait aussi vouloir ignorer l’interface
        // d’où provient la requête. Mais pour simplifier, on diffuse sur toutes
        // (le cache fera qu’on ne re-répond pas).
        struct sockaddr_in *baddr = (struct sockaddr_in *)&ifr_brd.ifr_broadaddr;

        // Envoi en broadcast sur cette interface
        if (sendto(sockfd, req, sizeof(*req), 0,
                   (struct sockaddr *)baddr, sizeof(*baddr)) < 0) {
            perror("sendto - rebroadcast");
        }
    }

    // Optionnellement, on peut re-désactiver le broadcast
    broadcast_enable = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));
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

    /* Associer le socket à l’adresse et au port d’écoute */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(DISCOVERY_PORT);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("neighboragent: démarré, en écoute sur le port %d\n", DISCOVERY_PORT);

    while (1) {
        discovery_request_t req;
        memset(&req, 0, sizeof(req));

        int recv_len = recvfrom(sockfd, &req, sizeof(req), 0,
                                (struct sockaddr *)&client_addr, &addr_len);
        if (recv_len < 0) {
            perror("recvfrom");
            continue;
        }

        if (recv_len == sizeof(discovery_request_t)) {
            // Vérifier si déjà vu dans le cache
            if (!is_in_cache(req.request_id)) {
                // Nouveau => on l’ajoute
                add_to_cache(req.request_id);

                // Répondre avec le hostname local
                char hostname[128] = {0};
                gethostname(hostname, sizeof(hostname));

                if (sendto(sockfd, hostname, strlen(hostname)+1, 0,
                           (struct sockaddr *)&client_addr, addr_len) < 0) {
                    perror("sendto - hostname reply");
                }

                // Si hops > 1, on relaie
                if (req.hops > 1) {
                    req.hops--;
                    rebroadcast_request(sockfd, &req);
                }
            } 
            // Sinon, on ignore (déjà traité)
        } 
        else {
            // Paquet mal formé, on l’ignore
        }
    }

    close(sockfd);
    return 0;
}
