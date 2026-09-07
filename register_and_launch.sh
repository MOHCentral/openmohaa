#!/bin/bash

# Server identity — set via env vars, not positional args (those are passed to the game)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SERVER_NAME="${OPM_SERVER_NAME:-Dev Server $(date +%s)}"
SERVER_IP="${OPM_SERVER_IP:-127.0.0.1}"
SERVER_PORT="${OPM_SERVER_PORT:-12203}"
API_BASE="http://localhost:8084/api/v1/servers"
API_URL="$API_BASE/register"
CFG_DIR="/home/elgan/.local/share/openmohaa/main"
CFG_FILE="$CFG_DIR/opm_server.cfg"
GAME_BIN="$SCRIPT_DIR/build/Debug/omohaaded-dbg"
CLIENT_BIN="$SCRIPT_DIR/build/Debug/openmohaa-dbg"

# Ensure cfg directory exists
mkdir -p "$CFG_DIR"

# Check if we already have a valid token
if [ -f "$CFG_FILE" ]; then
    echo "Found existing config, checking token validity..."
    
    # Extract existing server_id and token
    EXISTING_ID=$(grep 'opm_server_id' "$CFG_FILE" | sed 's/.*"\([^"]*\)".*/\1/')
    EXISTING_TOKEN=$(grep 'opm_server_token' "$CFG_FILE" | sed 's/.*"\([^"]*\)".*/\1/')
    
    if [ -n "$EXISTING_ID" ] && [ -n "$EXISTING_TOKEN" ]; then
        # Validate the token with the API
        VALIDATE_RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" \
            -H "Authorization: Bearer $EXISTING_TOKEN" \
            "$API_BASE/$EXISTING_ID")
        
        if [ "$VALIDATE_RESPONSE" = "200" ]; then
            echo "Existing token is valid! Reusing server_id: $EXISTING_ID"
            echo "----------------------------------------"
            cat "$CFG_FILE"
            echo "----------------------------------------"
            
            # Skip registration, go straight to launch
            # Skip registration, go straight to launch
            
            if [ -n "$GAME_BIN" ]; then
                clear
                BUILD_DIR=$(dirname "$GAME_BIN")
                export LD_LIBRARY_PATH="$BUILD_DIR:$LD_LIBRARY_PATH"

                echo "Launching dedicated server in background..."
                gdb -batch -ex "run" -ex "bt" --args "$GAME_BIN" +set developer 1 +set logfile 1 +set fs_game main +exec server.cfg +exec opm_server.cfg &
                SERVER_PID=$!
                echo "Server PID: $SERVER_PID"

                # Give the server a moment to start
                sleep 2

                echo "Launching game client..."
                "$CLIENT_BIN" +set r_fullscreen 0 +connect 127.0.0.1:$SERVER_PORT "$@"

                # Kill server when client exits
                kill $SERVER_PID 2>/dev/null
            else
                echo "Could not find openmohaa executable."
                exit 1
            fi
            exit 0
        else
            echo "Existing token is invalid (HTTP $VALIDATE_RESPONSE), registering new server..."
        fi
    fi
fi

echo "Registering server..."
echo "  Name: $SERVER_NAME"
echo "  IP:   $SERVER_IP"
echo "  Port: $SERVER_PORT"
echo "----------------------------------------"

# Register
RESPONSE=$(curl -s -X POST "$API_URL" \
  -H "Content-Type: application/json" \
  -d "{
    \"name\": \"$SERVER_NAME\",
    \"ip_address\": \"$SERVER_IP\",
    \"port\": $SERVER_PORT
  }")

# Check if curl failed
if [ -z "$RESPONSE" ]; then
    echo "Error: Empty response from API. Is it running at $API_URL?"
    exit 1
fi

# Parse JSON and create cfg file
CFG_CONTENT=$(echo "$RESPONSE" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    if 'server_id' in data and 'token' in data:
        print(f'set opm_server_id \"{data[\"server_id\"]}\"')
        print(f'set opm_server_token \"{data[\"token\"]}\"')
    else:
        print('// Error: unexpected response format', file=sys.stderr)
        print(data, file=sys.stderr)
        sys.exit(1)
except Exception as e:
    print(f'// Error parsing response: {e}', file=sys.stderr)
    sys.exit(1)
")

if [ $? -ne 0 ]; then
    echo "Failed to parse API response"
    exit 1
fi

# Write cfg file
echo "// Auto-generated OPM server config" > "$CFG_FILE"
echo "// Generated: $(date)" >> "$CFG_FILE"
echo "$CFG_CONTENT" >> "$CFG_FILE"

echo "SUCCESS! Created $CFG_FILE"
echo "----------------------------------------"
cat "$CFG_FILE"
echo "----------------------------------------"

# Find and launch the game

if [ -n "$GAME_BIN" ]; then
    clear
    BUILD_DIR=$(dirname "$GAME_BIN")
    export LD_LIBRARY_PATH="$BUILD_DIR:$LD_LIBRARY_PATH"

    echo "Launching dedicated server in background..."
    gdb -batch -ex "run" -ex "bt" --args "$GAME_BIN" +set developer 1 +set logfile 1 +set fs_game main +exec server.cfg +exec opm_server.cfg &
    SERVER_PID=$!
    echo "Server PID: $SERVER_PID"

    # Give the server a moment to start
    sleep 2

    echo "Launching game client..."
    "$CLIENT_BIN" +set r_fullscreen 0 +connect 127.0.0.1:$SERVER_PORT "$@"

    # Kill server when client exits
    kill $SERVER_PID 2>/dev/null
else
    echo "Could not find openmohaa executable."
    exit 1
fi