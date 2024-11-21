#!/bin/bash

# Couleurs pour le terminal
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Fonction pour afficher les messages de test
print_test() {
    echo -e "${GREEN}[TEST]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERREUR]${NC} $1"
}

# Fonction pour attendre qu'un fichier contienne une chaîne
wait_for_string() {
    local file=$1
    local string=$2
    local timeout=10
    local count=0
    
    while [ $count -lt $timeout ]; do
        if grep -q "$string" "$file"; then
            return 0
        fi
        sleep 1
        count=$((count + 1))
    done
    return 1
}

# Démarrer le serveur en arrière-plan
print_test "Démarrage du serveur sur le port 9999..."
./awale_server 9999 > server.log 2>&1 &
SERVER_PID=$!
sleep 2

# Créer un répertoire temporaire pour les logs
TEMP_DIR=$(mktemp -d)
trap 'rm -rf "$TEMP_DIR"; kill $SERVER_PID 2>/dev/null' EXIT

# Test 1: Connexion de base
print_test "Test 1: Connexion de base"
{
    sleep 1
    echo "joueur1"
    sleep 1
    echo "/list"
    sleep 1
    echo "/quit"
} > "$TEMP_DIR/input1"

./awale_client 127.0.0.1 9999 < "$TEMP_DIR/input1" > "$TEMP_DIR/output1" 2>&1 &
PID1=$!
sleep 3

# Test 2: Création de bio
print_test "Test 2: Test des bios"
{
    sleep 1
    echo "joueur2"
    sleep 1
    echo "/create-bio Je suis joueur2"
    sleep 1
    echo "/check-bio joueur1"
    sleep 1
    echo "/quit"
} > "$TEMP_DIR/input2"

./awale_client 127.0.0.1 9999 < "$TEMP_DIR/input2" > "$TEMP_DIR/output2" 2>&1 &
PID2=$!
sleep 3

# Test 3: Partie entre deux joueurs
print_test "Test 3: Test d'une partie"
{
    sleep 1
    echo "joueur3"
    sleep 1
    echo "/c joueur4"
    sleep 2
    echo "1"
    sleep 1
    echo "/quit"
} > "$TEMP_DIR/input3"

{
    sleep 1
    echo "joueur4"
    sleep 2
    echo "/a joueur3"
    sleep 2
    echo "1"
    sleep 1
    echo "/quit"
} > "$TEMP_DIR/input4"

./awale_client 127.0.0.1 9999 < "$TEMP_DIR/input3" > "$TEMP_DIR/output3" 2>&1 &
PID3=$!
sleep 1
./awale_client 127.0.0.1 9999 < "$TEMP_DIR/input4" > "$TEMP_DIR/output4" 2>&1 &
PID4=$!
sleep 5

# Vérification des résultats
print_test "Vérification des résultats..."

# Test 1: Vérifier la connexion
if grep -q "Connecté au serveur" "$TEMP_DIR/output1"; then
    print_test "✓ Test 1: Connexion réussie"
else
    print_error "✗ Test 1: Échec de la connexion"
fi

# Test 2: Vérifier la bio
if grep -q "Bio mise à jour avec succès" "$TEMP_DIR/output2"; then
    print_test "✓ Test 2: Création de bio réussie"
else
    print_error "✗ Test 2: Échec de la création de bio"
fi

# Test 3: Vérifier la partie
if grep -q "Tour du joueur" "$TEMP_DIR/output3" && grep -q "Tour du joueur" "$TEMP_DIR/output4"; then
    print_test "✓ Test 3: Partie créée avec succès"
else
    print_error "✗ Test 3: Échec de la création de partie"
fi

# Nettoyage
print_test "Nettoyage..."
kill $SERVER_PID 2>/dev/null
wait $PID1 2>/dev/null
wait $PID2 2>/dev/null
wait $PID3 2>/dev/null
wait $PID4 2>/dev/null

print_test "Tests terminés"

# Afficher les logs en cas d'erreur
if [ $? -ne 0 ]; then
    echo "=== Server Log ==="
    cat server.log
    echo "=== Client 1 Log ==="
    cat "$TEMP_DIR/output1"
    echo "=== Client 2 Log ==="
    cat "$TEMP_DIR/output2"
    echo "=== Client 3 Log ==="
    cat "$TEMP_DIR/output3"
    echo "=== Client 4 Log ==="
    cat "$TEMP_DIR/output4"
fi 