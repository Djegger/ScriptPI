/* ifnetshow.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_PORT 9999

static void usage(const char *prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s -n <server_ip> -a\n", prog);
    fprintf(stderr, "  %s -n <server_ip> -i <ifname>\n", prog);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    if (argc < 4) {
        usage(argv[0]);
    }

    // Parsing ultra simple
    char *server_ip = NULL;
    int show_all = 0;
    char *ifname = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i+1 < argc) {
            server_ip = argv[++i];
        } else if (strcmp(argv[i], "-a") == 0) {
            show_all = 1;
        } else if (strcmp(argv[i], "-i") == 0 && i+1 < argc) {
            ifname = argv[++i];
        }
    }

    if (!server_ip || (!show_all && !ifname)) {
        usage(argv[0]);
    }

    // On crée la requête qu'on enverra au serveur
    // => agent attend "-a" ou "-i <ifname>"
    char request[256];
    memset(request, 0, sizeof(request));

    if (show_all) {
        strcpy(request, "-a");
    } else {
        snprintf(request, sizeof(request), "-i %s", ifname);
    }

    // Création de la socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    // Configuration de l'adresse du serveur
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, server_ip, &servaddr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        return 1;
    }

    // Connexion
    if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect");
        close(sockfd);
        return 1;
    }

    // Envoi de la requête
    if (write(sockfd, request, strlen(request)) < 0) {
        perror("write");
        close(sockfd);
        return 1;
    }

    // Lecture de la réponse
    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));

    // On lit en une ou plusieurs fois
    // Pour faire simple, on lit une première fois, 
    // en supposant que tout tient dans 4096 octets.
    ssize_t n = read(sockfd, buffer, sizeof(buffer) - 1);
    if (n < 0) {
        perror("read");
        close(sockfd);
        return 1;
    }

    // Affichage
    printf("%s", buffer);

    close(sockfd);
    return 0;
}
