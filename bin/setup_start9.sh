#!/bin/bash
# Quick config helper for Start9 + T-Beam Supreme + T-Echo deployments

set -e

echo "╔════════════════════════════════════════════════════╗"
echo "║  Meshtastic Start9 Configuration Helper           ║"
echo "║  Optimized for T-Beam Supreme + T-Echo            ║"
echo "╚════════════════════════════════════════════════════╝"
echo ""

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration variables
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CONFIG_PATH="${REPO_ROOT}/lightning_gateway_config.json"
START9_HOST="${START9_HOST:-start9-server.local}"
TBEAM_HOST="${TBEAM_HOST:-tbeam-supreme.local}"
LIGHTNING_BACKEND="${LIGHTNING_BACKEND:-lnd}"

echo -e "${GREEN}Step 1: Checking repository context...${NC}"

echo "Repository: ${REPO_ROOT}"
echo -e "${GREEN}✓ No repository-side gateway files are required${NC}"
echo ""

echo -e "${GREEN}Step 2: Your Setup Configuration${NC}"
echo "Start9 Server: ${START9_HOST}"
echo "T-Beam Supreme: ${TBEAM_HOST}"
echo "Lightning Backend: ${LIGHTNING_BACKEND}"
echo ""

read -p "Is this correct? (y/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
	echo "Please set environment variables:"
	echo "  export START9_HOST='your-start9-address'"
	echo "  export TBEAM_HOST='your-tbeam-ip'"
	echo "  export LIGHTNING_BACKEND='lnd' # or 'cln'"
	exit 1
fi

echo -e "${GREEN}Step 3: Configuring gateway for Start9 deployment...${NC}"

# Generate Start9-specific config (for deployment ON Start9)
cat >"${CONFIG_PATH}" <<EOF
{
  "meshtastic": {
    "device": "tcp",
    "tcp_host": "${TBEAM_HOST}",
    "tcp_port": 4403,
    "comment": "T-Beam Supreme WiFi connection"
  },
  "lightning": {
    "backend": "${LIGHTNING_BACKEND}",
    "lnd": {
      "host": "localhost",
      "port": 8080,
      "macaroon_path": "/embassy-data/package-data/volumes/lnd/data/chain/bitcoin/mainnet/admin.macaroon",
      "tls_cert_path": "/embassy-data/package-data/volumes/lnd/tls.cert",
      "comment": "Using localhost - gateway runs ON Start9 with LND"
    },
    "cln": {
      "socket_path": "/embassy-data/package-data/volumes/cln/lightning-rpc",
      "comment": "For Core Lightning on Start9"
    }
  },
  "limits": {
    "max_payment_msat": 100000000,
    "comment": "100,000 sats maximum per payment"
  }
}
EOF

echo -e "${GREEN}✓ Config file created: ${CONFIG_PATH}${NC}"
echo ""

echo -e "${YELLOW}IMPORTANT: Manual Steps Required${NC}"
echo ""
echo "1. Deploy to Start9:"
echo "   # From this machine:"
echo "   ssh start9@${START9_HOST} 'mkdir -p ~/lightning-gateway'"
echo "   scp ${CONFIG_PATH} start9@${START9_HOST}:~/lightning-gateway/"
echo ""
echo "   # Then SSH to Start9:"
echo "   ssh start9@${START9_HOST}"
echo "   cd ~/lightning-gateway"
echo "   # Install or copy your gateway service implementation here and point it at lightning_gateway_config.json"
echo ""
echo "   # Verify paths exist:"
echo "   ls -la /embassy-data/package-data/volumes/lnd/data/chain/bitcoin/mainnet/admin.macaroon"
echo "   ls -la /embassy-data/package-data/volumes/lnd/tls.cert"
echo ""
echo "2. Flash T-Beam Supreme firmware:"
echo "   cd meshtastic_firmware"
echo "   pio run -e tbeam-s3-core -t upload"
echo ""
echo "3. Configure T-Beam Supreme as gateway:"
echo "   meshtastic --dest \!your-tbeam-id --set device.role ROUTER"
echo "   meshtastic --dest \!your-tbeam-id --set network.wifi_enabled true"
echo "   meshtastic --dest \!your-tbeam-id --set network.wifi_ssid 'YourSSID'"
echo "   meshtastic --dest \!your-tbeam-id --set network.wifi_password 'YourPassword'"
echo ""
echo "4. Flash T-Echo firmware:"
echo "   pio run -e t-echo -t upload"
echo ""
echo "5. Start the gateway (on Start9):"
echo "   Run the gateway service you installed in ~/lightning-gateway with lightning_gateway_config.json"
echo ""

echo -e "${GREEN}Setup script complete!${NC}"
echo ""
echo "Next steps:"
echo "  1. Complete manual steps above"
echo "  2. Start your gateway service on Start9"
echo "  3. Generate test invoice in Alby Go on iPhone"
echo "  4. Send via Meshtastic app"
echo "  5. Check Zeus for payment confirmation"
