#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>

/*
 * Fonction pour calculer le nombre de bits à 1 (pour un masque contigu)
 * dans un buffer binaire. On l'utilise pour compter le préfixe.
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
 * Affiche l'adresse (IPv4 ou IPv6) et le préfixe sous la forme d.d.d.d/p
 * ou d:d:d:d:d:d:d:d/p.
 */
static void print_address_with_prefix(int family, 
                                      const void *addr, 
                                      const void *netmask) 
{
    char addr_str[INET6_ADDRSTRLEN] = {0};
    char mask_str[INET6_ADDRSTRLEN] = {0};

    // Convertit l'adresse en chaîne lisible
    inet_ntop(family, addr, addr_str, sizeof(addr_str));

    // Convertit le masque en chaîne binaire (pour calcul du préfixe)
    // inet_ntop n'est utile que pour l'affichage, mais nous voulons surtout
    // son contenu binaire brut (pour count_prefix_len).
    // On reconvertit netmask sous forme de tableau d'octets pour calculer le /p.
    
    if (netmask == NULL) {
        // Si le masque n'est pas défini pour cette interface, on ne l'affiche pas
        // (c'est parfois le cas pour des interfaces virtuelles ou sans IP).
        printf("%s (prefix inconnu)\n", addr_str);
        return;
    }

    // On compte la taille en bits du préfixe
    int prefix_len = 0;

    if (family == AF_INET) {
        // 4 octets pour IPv4
        prefix_len = count_prefix_len((const unsigned char *)netmask, 4);
    } else if (family == AF_INET6) {
        // 16 octets pour IPv6
        prefix_len = count_prefix_len((const unsigned char *)netmask, 16);
    }

    printf("%s/%d\n", addr_str, prefix_len);
}

/*
 * Affiche les adresses IPv4 et IPv6 d'une interface donnée.
 */
static void show_interface(const char *ifname_filter) {
    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        // On ignore les interfaces sans nom ou sans adresse
        if (!ifa->ifa_name || !ifa->ifa_addr) {
            continue;
        }

        // Filtrage sur le nom d'interface si ifname_filter != NULL
        if (ifname_filter && strcmp(ifname_filter, ifa->ifa_name) != 0) {
            continue;
        }

        int family = ifa->ifa_addr->sa_family;
        if (family == AF_INET || family == AF_INET6) {
            // On récupère l'adresse
            void *addr_ptr = NULL;
            void *mask_ptr = NULL;

            if (family == AF_INET) {
                struct sockaddr_in *sin = (struct sockaddr_in*)ifa->ifa_addr;
                struct sockaddr_in *msk = (struct sockaddr_in*)ifa->ifa_netmask;
                addr_ptr = &(sin->sin_addr);
                if (msk) {
                    mask_ptr = &(msk->sin_addr);
                }
            } else { // AF_INET6
                struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)ifa->ifa_addr;
                struct sockaddr_in6 *msk6 = (struct sockaddr_in6*)ifa->ifa_netmask;
                addr_ptr = &(sin6->sin6_addr);
                if (msk6) {
                    mask_ptr = &(msk6->sin6_addr);
                }
            }

            // Affichage
            // Si ifname_filter est défini, on n'affiche pas le nom de l'interface
            // à chaque adresse. Sinon, on indique l'interface.
            if (ifname_filter) {
                // On n'affiche pas le nom (car on sait déjà sur quelle interface on est)
                print_address_with_prefix(family, addr_ptr, mask_ptr);
            } else {
                // On affiche le nom de l'interface, puis l'adresse
                printf("%s: ", ifa->ifa_name);
                print_address_with_prefix(family, addr_ptr, mask_ptr);
            }
        }
    }

    freeifaddrs(ifaddr);
}

/*
 * Affiche la liste de *toutes* les interfaces réseau (noms) avec leur(s) 
 * adresse(s) + préfixes.
 */
static void show_all_interfaces() {
    // On appelle show_interface() sans filtre pour toutes les interfaces
    show_interface(NULL);
}

/*
 * Affiche l'usage de la commande.
 */
static void usage(const char *progname) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s -a              # Affiche toutes les interfaces + adresses/prefixes\n", progname);
    fprintf(stderr, "  %s -i <ifname>     # Affiche les adresses/prefixes de l'interface <ifname>\n", progname);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        usage(argv[0]);
    }

    // Gestion des arguments
    if (strcmp(argv[1], "-a") == 0) {
        // ifshow -a
        show_all_interfaces();
    }
    else if (strcmp(argv[1], "-i") == 0) {
        // ifshow -i ifname
        if (argc < 3) {
            usage(argv[0]);
        }
        show_interface(argv[2]);
    }
    else {
        usage(argv[0]);
    }

    return 0;
}
