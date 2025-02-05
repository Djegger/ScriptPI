/****************************************************
 * agent.c
 *
 * Compilation :
 *    gcc agent.c -o agent
 *
 * Exécution (exemple) :
 *    sudo ./agent
 *
 * Explications :
 *  - Écoute UDP 9999
 *  - À la réception d'un message du type :
 *       "NEIGHBOR_DISCOVERY message_id=1234 hop=3 origin=machineA"
 *    -> Répond avec le hostname
 *    -> S'il hop>1, décrémente hop et envoie la requête vers la gateway.
 *
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
#include <ctype.h>

#define AGENT_PORT 9999
#define BUFFER_SIZE 1024

// Stockage basique des messages déjà vus (origin, message_id)
#define MAX_SEEN 1000

typedef struct {
    char origin[64];
    int  message_id;
} seen_message_t;

static seen_message_t seen_messages[MAX_SEEN];
static int seen_count = 0;

// Récupère le hostname local
static void get_local_hostname(char *buf, size_t buflen) {
    if (gethostname(buf, buflen) != 0) {
        perror("gethostname");
        strncpy(buf, "UnknownHost", buflen);
        buf[buflen-1] = '\0';
    }
}

// Récupère la passerelle par défaut (IPv4) en la parsant dans "ip route show default"
// => renvoie 1 si trouvé, 0 sinon
static int get_default_gateway(char *gateway, size_t gwlen) {
    FILE *fp = popen("ip route show default", "r");
    if (!fp) {
        perror("popen");
        return 0;
    }
    char line[256];
    memset(gateway, 0, gwlen);

    while (fgets(line, sizeof(line), fp)) {
        // Exemple: "default via 192.168.1.254 dev eth0 ..."
        char *via = strstr(line, "via ");
        if (via) {
            via += 4; // skip "via "
            sscanf(via, "%63s", gateway);
            break;
        }
    }

    pclose(fp);

    if (strlen(gateway) > 0) {
        return 1;
    } else {
        return 0;
    }
}

// Vérifie si (origin, message_id) déjà vu
static int has_already_seen(const char *origin, int msg_id) {
    for (int i = 0; i < seen_count; i++) {
        if ((seen_messages[i].message_id == msg_id) &&
            (strcmp(seen_messages[i].origin, origin) == 0)) {
            return 1;
        }
    }
    return 0;
}

// Marque (origin, message_id) comme vu
static void mark_as_seen(const char *origin, int msg_id) {
    if (seen_count < MAX_SEEN) {
        strncpy(seen_messages[seen_count].origin, origin, sizeof(seen_messages[seen_count].origin) - 1);
        seen_messages[seen_count].origin[sizeof(seen_messages[seen_count].origin) - 1] = '\0';
        seen_messages[seen_count].message_id = msg_id;
        seen_count++;
    }
}

// Fonction utilitaire pour envoyer la requête (message) en UDP à une IP donnée
static void send_udp_message(const char *ip, unsigned short port,
                             const char *message, size_t msg_len)
{
    int sockfd;
    struct sockaddr_in addr;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        return;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);

    if (sendto(sockfd, message, msg_len, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("sendto");
    }
    close(sockfd);
}

int main(void) {
    int sockfd;
    struct sockaddr_in serv_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // Récupération du hostname local
    char hostname[256];
    get_local_hostname(hostname, sizeof(hostname));

    // Création du socket UDP
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        return 1;
    }

    // Bind sur le port AGENT_PORT (9999)
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port        = htons(AGENT_PORT);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    printf("[Agent] Démarré sur le port %d\n", AGENT_PORT);
    printf("[Agent] Mon hostname = %s\n", hostname);

    // Boucle principale de réception
    while (1) {
        char buffer[BUFFER_SIZE];
        ssize_t recvlen = recvfrom(sockfd, buffer, BUFFER_SIZE - 1, 0,
                                   (struct sockaddr *)&client_addr, &addr_len);
        if (recvlen < 0) {
            perror("recvfrom");
            break; // quitte la boucle en cas d'erreur
        }

        buffer[recvlen] = '\0';

        // On s'attend à un message du type:
        //    "NEIGHBOR_DISCOVERY message_id=1234 hop=3 origin=MachineA"
        // On va parser ça très simplement
        int msg_id = 0;
        int hop    = 0;
        char origin[64];
        memset(origin, 0, sizeof(origin));

        if (strncmp(buffer, "NEIGHBOR_DISCOVERY", 18) == 0) {
            // Extraire message_id, hop, origin
            // Format naïf : "NEIGHBOR_DISCOVERY message_id=%d hop=%d origin=%s"
            char dummy[32]; // pour "NEIGHBOR_DISCOVERY"
            if (sscanf(buffer, "%s message_id=%d hop=%d origin=%63s",
                       dummy, &msg_id, &hop, origin) == 4)
            {
                // Vérifier si on a déjà vu (origin, msg_id)
                if (!has_already_seen(origin, msg_id)) {
                    // Marquer comme vu
                    mark_as_seen(origin, msg_id);

                    // Répondre immédiatement au client -> on envoie "hostname"
                    {
                        ssize_t sent = sendto(sockfd,
                                              hostname,
                                              strlen(hostname),
                                              0,
                                              (struct sockaddr *)&client_addr,
                                              addr_len);
                        if (sent < 0) {
                            perror("sendto response");
                        }
                    }

                    // Si hop > 1, relayer vers la gateway (hop - 1)
                    if (hop > 1) {
                        // Récupère la GW
                        char gateway[64];
                        if (get_default_gateway(gateway, sizeof(gateway))) {
                            // Construit le nouveau message
                            char new_msg[BUFFER_SIZE];
                            snprintf(new_msg, sizeof(new_msg),
                                     "NEIGHBOR_DISCOVERY message_id=%d hop=%d origin=%s",
                                     msg_id, hop - 1, origin);

                            // Envoie en unicast à la GW
                            send_udp_message(gateway, AGENT_PORT,
                                             new_msg, strlen(new_msg));
                            // NOTE : Sur un routeur, on voudrait diffuser
                            //        sur chaque interface (sauf celle d'où
                            //        est venue la requête). Ici, on se limite
                            //        à la gateway par défaut : BFS simple.
                        }
                    }
                }
                // sinon -> déjà vu, on ne fait rien
            }
            // sinon -> format invalide, on ignore
        }
        // sinon -> message non reconnu, on ignore
    }

    close(sockfd);
    return 0;
}
