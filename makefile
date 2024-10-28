# Noms des exécutables
SERVER = awale_server
CLIENT = awale_client

# Compilateur
CC = gcc

# Options de compilation
CFLAGS = -Wall -Wextra -std=c11 -pthread

# Fichiers sources
SERVER_SRC = awale_server.c
CLIENT_SRC = awale_client.c
COMMON_SRC = awale_v2.c

# Fichiers headers
HEADERS = awale_v2.c

# Règle par défaut
all: $(SERVER) $(CLIENT)

# Règle pour créer le serveur
$(SERVER): $(SERVER_SRC) $(COMMON_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(SERVER_SRC) -g 

# Règle pour créer le client
$(CLIENT): $(CLIENT_SRC) $(COMMON_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_SRC) -g 

# Règle pour nettoyer les fichiers générés
clean:
	rm -f $(SERVER) $(CLIENT)

# Règle pour nettoyer tout
distclean: clean

# Indique que ces cibles ne correspondent pas à des fichiers
.PHONY: all clean distclean