#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <ifaddrs.h>
#include <net/if.h>

#define SERVER_PORT 9999
#define BUF_SIZE 4096

/*
 * Compte les bits à 1 dans buf (pour calculer le prefix length).
 */
static int count_prefix_len(const unsigned char *buf, size_t buflen) {
    int prefix_len = 0;
    for (size_t i = 0; i < buflen; i++) {
        unsigned char c = buf[i];
        while (c) {
            prefix_len += (c & 1);
            c >>= 1;
        }
    }
    return prefix_len;
}

/*
 * Ajoute à outbuf la chaîne "addr/prefix" ou "addr (prefix inconnu)".
 */
static void append_address_with_prefix(int family,
                                       const void *addr,
                                       const void *netmask,
                                       char *outbuf,
                                       size_t outbuf_len) 
{
    char addr_str[INET6_ADDRSTRLEN] = {0};

    if (!addr) return;

    // Convertit l'adresse en chaîne
    inet_ntop(family, addr, addr_str, sizeof(addr_str));

    // Si pas de netmask => pas de prefix
    if (!netmask) {
        snprintf(outbuf + strlen(outbuf),
                 outbuf_len - strlen(outbuf),
                 "%s (prefix inconnu)\n", addr_str);
        return;
    }

    // Calcul du prefix (en supposant un masque contigu)
    int prefix_len = 0;
    if (family == AF_INET) {
        prefix_len = count_prefix_len(netmask, 4);
    } else {
        prefix_len = count_prefix_len(netmask, 16);
    }

    // Ajout au buffer
    snprintf(outbuf + strlen(outbuf),
             outbuf_len - strlen(outbuf),
             "%s/%d\n", addr_str, prefix_len);
}

/*
 * Récupère toutes les interfaces, formate le résultat dans 'outbuf'.
 */
static void get_all_interfaces(char *outbuf, size_t outbuf_len)
{
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        snprintf(outbuf, outbuf_len, "Erreur getifaddrs\n");
        return;
    }

    outbuf[0] = '\0';

    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name || !ifa->ifa_addr) continue;

        int family = ifa->ifa_addr->sa_family;
        if (family == AF_INET || family == AF_INET6) {
            void *addr_ptr = NULL;
            void *mask_ptr = NULL;

            if (family == AF_INET) {
                struct sockaddr_in *sin = (struct sockaddr_in*)ifa->ifa_addr;
                struct sockaddr_in *msk = (struct sockaddr_in*)ifa->ifa_netmask;
                addr_ptr = &sin->sin_addr;
                mask_ptr = msk ? &msk->sin_addr : NULL;
            } else {
                struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)ifa->ifa_addr;
                struct sockaddr_in6 *msk6 = (struct sockaddr_in6*)ifa->ifa_netmask;
                addr_ptr = &sin6->sin6_addr;
                mask_ptr = msk6 ? &msk6->sin6_addr : NULL;
            }

            // On écrit "ifname: addr/prefix" (ou ...prefix inconnu)
            snprintf(outbuf + strlen(outbuf),
                     outbuf_len - strlen(outbuf),
                     "%s: ", ifa->ifa_name);

            append_address_with_prefix(family, addr_ptr, mask_ptr,
                                       outbuf, outbuf_len);
        }
    }

    freeifaddrs(ifaddr);
}

/*
 * Récupère uniquement l'interface nommée 'ifname'.
 */
static void get_one_interface(const char *ifname, char *outbuf, size_t outbuf_len)
{
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        snprintf(outbuf, outbuf_len, "Erreur getifaddrs\n");
        return;
    }

    outbuf[0] = '\0';

    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name || !ifa->ifa_addr) continue;

        if (strcmp(ifa->ifa_name, ifname) != 0) {
            continue;
        }

        int family = ifa->ifa_addr->sa_family;
        if (family == AF_INET || family == AF_INET6) {
            void *addr_ptr = NULL;
            void *mask_ptr = NULL;

            if (family == AF_INET) {
                struct sockaddr_in *sin = (struct sockaddr_in*)ifa->ifa_addr;
                struct sockaddr_in *msk = (struct sockaddr_in*)ifa->ifa_netmask;
                addr_ptr = &sin->sin_addr;
                mask_ptr = msk ? &msk->sin_addr : NULL;
            } else {
                struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)ifa->ifa_addr;
                struct sockaddr_in6 *msk6 = (struct sockaddr_in6*)ifa->ifa_netmask;
                addr_ptr = &sin6->sin6_addr;
                mask_ptr = msk6 ? &msk6->sin6_addr : NULL;
            }

            append_address_with_prefix(family, addr_ptr, mask_ptr,
                                       outbuf, outbuf_len);
        }
    }

    freeifaddrs(ifaddr);

    // Si rien n'a été écrit => interface introuvable ou sans IP
    if (strlen(outbuf) == 0) {
        snprintf(outbuf, outbuf_len,
                 "Aucune adresse pour l'interface %s\n", ifname);
    }
}

/*
 * Le serveur TCP qui reçoit une requête, ex: "-a" ou "-i eth0",
 * exécute localement la logique (get_all_interfaces ou get_one_interface)
 * et renvoie le résultat.
 */
int main(void)
{
    int listenfd, connfd;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t cli_len = sizeof(cliaddr);

    // Ignorer SIGPIPE (si le client ferme brutalement)
    signal(SIGPIPE, SIG_IGN);

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        return 1;
    }

    // Autorise la réutilisation du port
    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(SERVER_PORT);

    if (bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind");
        close(listenfd);
        return 1;
    }

    if (listen(listenfd, 5) < 0) {
        perror("listen");
        close(listenfd);
        return 1;
    }

    printf("Agent ifshow-like en écoute sur le port %d...\n", SERVER_PORT);

    // Boucle: accepte un client, lit une requête, répond, ferme.
    while (1) {
        connfd = accept(listenfd, (struct sockaddr*)&cliaddr, &cli_len);
        if (connfd < 0) {
            perror("accept");
            continue;
        }

        char request[BUF_SIZE];
        memset(request, 0, sizeof(request));

        // On lit la requête du client
        ssize_t r = read(connfd, request, sizeof(request)-1);
        if (r <= 0) {
            close(connfd);
            continue;
        }

        // request peut être "-a" ou "-i <ifname>"
        // On prépare la réponse
        char response[BUF_SIZE * 4]; // plus grand si besoin
        memset(response, 0, sizeof(response));

        // On regarde le début de la requête
        if (strncmp(request, "-a", 2) == 0) {
            // Liste de TOUTES les interfaces
            get_all_interfaces(response, sizeof(response));
        }
        else if (strncmp(request, "-i ", 3) == 0) {
            // -i ifname
            char ifn[128];
            memset(ifn, 0, sizeof(ifn));
            sscanf(request + 3, "%127s", ifn);
            get_one_interface(ifn, response, sizeof(response));
        }
        else {
            snprintf(response, sizeof(response),
                     "Requête invalide: %s\n", request);
        }

        // On renvoie la réponse
        write(connfd, response, strlen(response));

        // Ferme la connexion
        close(connfd);
    }

    close(listenfd);
    return 0;
}
