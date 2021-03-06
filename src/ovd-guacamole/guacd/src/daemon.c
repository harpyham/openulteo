
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is guacd.
 *
 * The Initial Developer of the Original Code is
 * Michael Jumper.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 * David PHAM-VAN <d.pham-van@ulteo.com> Ulteo SAS - http://www.ulteo.com
 * Alex Bligh <alex@alex.org.uk>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

#include <errno.h>
#include <syslog.h>
#include <libgen.h>

#include <guacamole/client.h>
#include <guacamole/error.h>

#include "client.h"
#include "log.h"

#define GUACD_DEV_NULL "/dev/null"
#define GUACD_ROOT     "/"

void guacd_handle_connection(int fd) {

    guac_client* client;
    guac_client_plugin* plugin;
    guac_instruction* select;
    guac_instruction* connect;

    /* Open guac_socket */
    guac_socket* socket = guac_socket_open(fd);

    /* Get protocol from select instruction */
    select = guac_protocol_expect_instruction(
            socket, GUACD_USEC_TIMEOUT, "select");
    if (select == NULL) {

        /* Log error */
        guacd_log_guac_error("Error reading \"select\"");

        /* Free resources */
        guac_socket_close(socket);
        return;
    }

    /* Validate args to select */
    if (select->argc != 1) {

        /* Log error */
        guacd_log_error("Bad number of arguments to \"select\" (%i)",
                select->argc);

        /* Free resources */
        guac_socket_close(socket);
        return;
    }

    guacd_log_info("Protocol \"%s\" selected", select->argv[0]);

    /* Get plugin from protocol in select */
    plugin = guac_client_plugin_open(select->argv[0]);
    guac_instruction_free(select);

    if (plugin == NULL) {

        /* Log error */
        guacd_log_guac_error("Error loading client plugin");

        /* Free resources */
        guac_socket_close(socket);
        return;
    }

    /* Send args response */
    if (guac_protocol_send_args(socket, plugin->args)
            || guac_socket_flush(socket)) {

        /* Log error */
        guacd_log_guac_error("Error sending \"args\"");

        if (guac_client_plugin_close(plugin))
            guacd_log_guac_error("Error closing client plugin");

        guac_socket_close(socket);
        return;
    }

    /* Get args from connect instruction */
    connect = guac_protocol_expect_instruction(
            socket, GUACD_USEC_TIMEOUT, "connect");
    if (connect == NULL) {

        /* Log error */
        guacd_log_guac_error("Error reading \"connect\"");

        if (guac_client_plugin_close(plugin))
            guacd_log_guac_error("Error closing client plugin");

        guac_socket_close(socket);
        return;
    }

    /* Load and init client */
    client = guac_client_plugin_get_client(plugin, socket,
            connect->argc, connect->argv,
            guacd_client_log_info, guacd_client_log_error);

    guac_instruction_free(connect);

    if (client == NULL) {

        guacd_log_guac_error("Error instantiating client");

        if (guac_client_plugin_close(plugin))
            guacd_log_guac_error("Error closing client plugin");

        guac_socket_close(socket);
        return;
    }

    /* Start client threads */
    guacd_log_info("Starting client");
    if (guacd_client_start(client))
        guacd_log_error("Client finished abnormally");
    else
        guacd_log_info("Client finished normally");

    /* Clean up */
    guac_client_free(client);
    if (guac_client_plugin_close(plugin))
        guacd_log_error("Error closing client plugin");

    /* Close socket */
    guac_socket_close(socket);

}

int redirect_fd(int fd, int flags) {

    /* Attempt to open bit bucket */
    int new_fd = open(GUACD_DEV_NULL, flags);
    if (new_fd < 0)
        return 1;

    /* If descriptor is different, redirect old to new and close new */
    if (new_fd != fd) {
        dup2(new_fd, fd);
        close(new_fd);
    }

    return 0;

}

int daemonize() {

    pid_t pid;

    /* Fork once to ensure we aren't the process group leader */
    pid = fork();
    if (pid < 0) {
        guacd_log_error("Could not fork() parent: %s", strerror(errno));
        return 1;
    }

    /* Exit if we are the parent */
    if (pid > 0) {
        guacd_log_info("Exiting and passing control to PID %i", pid);
        _exit(0);
    }

    /* Start a new session (if not already group leader) */
    setsid();

    /* Fork again so the session group leader exits */
    pid = fork();
    if (pid < 0) {
        guacd_log_error("Could not fork() group leader: %s", strerror(errno));
        return 1;
    }

    /* Exit if we are the parent */
    if (pid > 0) {
        guacd_log_info("Exiting and passing control to PID %i", pid);
        _exit(0);
    }

    /* Change to root directory */
    if (chdir(GUACD_ROOT) < 0) {
        guacd_log_error(
                "Unable to change working directory to "
                GUACD_ROOT);
        return 1;
    }

    /* Reopen the 3 stdxxx to /dev/null */

    if (redirect_fd(STDIN_FILENO, O_RDONLY)
    || redirect_fd(STDOUT_FILENO, O_WRONLY)
    || redirect_fd(STDERR_FILENO, O_WRONLY)) {

        guacd_log_error(
                "Unable to redirect standard file descriptors to "
                GUACD_DEV_NULL);
        return 1;
    }

    /* Success */
    return 0;

}


int main(int argc, char* argv[]) {

    /* Server */
    int socket_fd;
    struct addrinfo* addresses;
    struct addrinfo* current_address;
    char bound_address[1024];
    char bound_port[64];
    int opt_on = 1;

    struct addrinfo hints = {
        .ai_family   = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP
    };

    /* Client */
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    int connected_socket_fd;

    /* Arguments */
    char* listen_address = NULL; /* Default address of INADDR_ANY */
    char* listen_port = "4822";  /* Default port */
    char* pidfile = NULL;
    int opt;
    int foreground = 0;

    /* General */
    int retval;

    /* Parse arguments */
    while ((opt = getopt(argc, argv, "l:b:p:f")) != -1) {
        if (opt == 'l') {
            listen_port = strdup(optarg);
        }
        else if (opt == 'b') {
            listen_address = strdup(optarg);
        }
        else if (opt == 'f') {
            foreground = 1;
        }
        else if (opt == 'p') {
            pidfile = strdup(optarg);
        }
        else {

            fprintf(stderr, "USAGE: %s"
                    " [-l LISTENPORT]"
                    " [-b LISTENADDRESS]"
                    " [-p PIDFILE]"
                    " [-f]\n", argv[0]);

            exit(EXIT_FAILURE);
        }
    }

    /* Set up logging prefix */
    strncpy(log_prefix, basename(argv[0]), sizeof(log_prefix));

    /* Open log as early as we can */
    openlog(NULL, LOG_PID, LOG_DAEMON);

    /* Log start */
    guacd_log_info("Guacamole proxy daemon (guacd) version " VERSION);

    /* Get addresses for binding */
    if ((retval = getaddrinfo(listen_address, listen_port,
                    &hints, &addresses))) {

        guacd_log_error("Error parsing given address or port: %s",
                gai_strerror(retval));
        exit(EXIT_FAILURE);

    }

    /* Get socket */
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        guacd_log_error("Error opening socket: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* Allow socket reuse */
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR,
                (void*) &opt_on, sizeof(opt_on))) {
        guacd_log_info("Unable to set socket options for reuse: %s",
                strerror(errno));
    }

    /* Attempt binding of each address until success */
    current_address = addresses;
    while (current_address != NULL) {

        int retval;

        /* Resolve hostname */
        if ((retval = getnameinfo(current_address->ai_addr,
                current_address->ai_addrlen,
                bound_address, sizeof(bound_address),
                bound_port, sizeof(bound_port),
                NI_NUMERICHOST | NI_NUMERICSERV)))
            guacd_log_error("Unable to resolve host: %s",
                    gai_strerror(retval));

        /* Attempt to bind socket to address */
        if (bind(socket_fd,
                    current_address->ai_addr,
                    current_address->ai_addrlen) == 0) {

            guacd_log_info("Successfully bound socket to "
                    "host %s, port %s", bound_address, bound_port);

            /* Done if successful bind */
            break;

        }

        /* Otherwise log information regarding bind failure */
        else
            guacd_log_info("Unable to bind socket to "
                    "host %s, port %s: %s",
                    bound_address, bound_port, strerror(errno));

        current_address = current_address->ai_next;

    }

    /* If unable to bind to anything, fail */
    if (current_address == NULL) {
        guacd_log_error("Unable to bind socket to any addresses.");
        exit(EXIT_FAILURE);
    }

    /* Daemonize if requested */
    if (!foreground) {

        /* Attempt to daemonize process */
        if (daemonize()) {
            guacd_log_error("Could not become a daemon.");
            exit(EXIT_FAILURE);
        }

    }

    /* Write PID file if requested */
    if (pidfile != NULL) {

        /* Attempt to open pidfile and write PID */
        FILE* pidf = fopen(pidfile, "w");
        if (pidf) {
            fprintf(pidf, "%d\n", getpid());
            fclose(pidf);
        }
        
        /* Fail if could not write PID file*/
        else {
            guacd_log_error("Could not write PID file: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

    }

    /* Ignore SIGPIPE */
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        guacd_log_info("Could not set handler for SIGPIPE to ignore. "
                "SIGPIPE may cause termination of the daemon.");
    }

    /* Ignore SIGCHLD (force automatic removal of children) */
    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
        guacd_log_info("Could not set handler for SIGCHLD to ignore. "
                "Child processes may pile up in the process table.");
    }

    /* Log listening status */
    guacd_log_info("Listening on host %s, port %s", bound_address, bound_port);

    /* Free addresses */
    freeaddrinfo(addresses);

    /* Daemon loop */
    for (;;) {

        pid_t child_pid;

        /* Listen for connections */
        if (listen(socket_fd, 5) < 0) {
            guacd_log_error("Could not listen on socket: %s", strerror(errno));
            return 3;
        }

        /* Accept connection */
        client_addr_len = sizeof(client_addr);
        connected_socket_fd = accept(socket_fd,
                (struct sockaddr*) &client_addr, &client_addr_len);

        if (connected_socket_fd < 0) {
            guacd_log_error("Could not accept client connection: %s",
                    strerror(errno));
            return 3;
        }

        /* 
         * Once connection is accepted, send child into background.
         *
         * Note that we prefer fork() over threads for connection-handling
         * processes as they give each connection its own memory area, and
         * isolate the main daemon and other connections from errors in any
         * particular client plugin.
         */

        child_pid = fork();

        /* If error, log */
        if (child_pid == -1)
            guacd_log_error("Error forking child process: %s", strerror(errno));

        /* If child, start client, and exit when finished */
        else if (child_pid == 0) {
            guacd_handle_connection(connected_socket_fd);
            close(connected_socket_fd);
            return 0;
        }

        /* If parent, close reference to child's descriptor */
        else if (close(connected_socket_fd) < 0) {
            guacd_log_error("Error closing daemon reference to "
                    "child descriptor: %s", strerror(errno));
        }

    }

    /* Close socket */
    if (close(socket_fd) < 0) {
        guacd_log_error("Could not close socket: %s", strerror(errno));
        return 3;
    }

    return 0;

}

