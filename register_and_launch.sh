#!/bin/bash

# Defaults
SERVER_NAME="${1:-Dev Server $(date +%s)}"
SERVER_IP="${2:-127.0.0.1}"
SERVER_PORT="${3:-12203}"
API_BASE="http://localhost:8084/api/v1/servers"
API_URL="$API_BASE/register"
CFG_DIR="/home/elgan/.local/share/openmohaa/main"
CFG_FILE="$CFG_DIR/opm_server.cfg"

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
            cd "$(dirname "$0")/build"
            GAME_BIN=$(find . -maxdepth 2 -type f -name "openmohaa*" -executable | head -n 1)
            if [ -n "$GAME_BIN" ]; then
                clear
                echo "Launching $GAME_BIN..."
                gdb -batch -ex "run" -ex "bt" --args "$GAME_BIN" +set developer 1 +exec server.cfg +exec opm_server.cfg "$@"
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
cd "$(dirname "$0")/build"

GAME_BIN=$(find . -maxdepth 2 -type f -name "openmohaa*" -executable | head -n 1)

if [ -n "$GAME_BIN" ]; then
    clear
    echo "Launching $GAME_BIN..."
    gdb -batch -ex "run" -ex "bt" --args "$GAME_BIN" +set developer 1 +exec server.cfg +exec opm_server.cfg "$@"
else
    echo "Could not find openmohaa executable."
    exit 1
fi
