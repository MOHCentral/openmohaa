#!/bin/bash

# Defaults
SERVER_NAME="${1:-Dev Server $(date +%s)}"
SERVER_IP="${2:-127.0.0.1}"
SERVER_PORT="${3:-12203}"
API_URL="http://localhost:8084/api/v1/servers/register"
CFG_DIR="/home/elgan/.local/share/openmohaa/main"
CFG_FILE="$CFG_DIR/opm_server.cfg"

echo "Registering server..."
echo "  Name: $SERVER_NAME"
echo "  IP:   $SERVER_IP"
echo "  Port: $SERVER_PORT"
echo "----------------------------------------"

# Ensure cfg directory exists
mkdir -p "$CFG_DIR"

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
