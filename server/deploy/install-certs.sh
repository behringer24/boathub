#!/bin/sh
# Give the broker a copy of the certificate, and reload it.
#
#   sudo server/deploy/install-certs.sh boathub.example.com
#
# Run it once TLS is first set up, and again after every renewal.
#
# Two sources, because two things issue certificates and they disagree about
# names and places:
#
#   acme-companion   <domain>.crt and <domain>.key, in a Docker volume that
#                    nginx-proxy has mounted. Both are symlinks into a
#                    <domain>/ directory beside them, which is why the copy
#                    has to follow them. Tried first.
#   certbot          fullchain.pem and privkey.pem under /etc/letsencrypt/live.
#
# Why a copy rather than mounting the volume into the broker. The private key
# belongs to root and the broker runs unprivileged inside its container;
# mounting the volume read-only does not change that, it only makes the refusal
# read-only too. The alternatives are running the broker as root or loosening
# permissions on a key that other sites also depend on. A copy owned by the
# broker's own user is the smaller concession.

set -eu

DOMAIN="${1:?usage: install-certs.sh <domain>}"

# Where acme-companion's certificates can be read from. Any container with the
# certs volume mounted will do; nginx-proxy has it read-only.
PROXY_CONTAINER="${PROXY_CONTAINER:-nginx-proxy}"
PROXY_CERTS_PATH="${PROXY_CERTS_PATH:-/etc/nginx/certs}"

# Where certbot keeps them, if it is certbot. CERT_DIR overrides the guess:
# the directory is named after the first name a certificate was issued for,
# which a wildcard's is not.
ROOT="${LETSENCRYPT_LIVE:-/etc/letsencrypt/live}"
LIVE="${CERT_DIR:-$ROOT/$DOMAIN}"

HERE="$(cd "$(dirname "$0")/.." && pwd)"
CERTS="$HERE/mosquitto/certs"

# The uid mosquitto runs as inside eclipse-mosquitto:2. The files are chowned
# to it because the container has no way to read anything else.
MOSQUITTO_UID=1883

mkdir -p "$CERTS"
FOUND=""

# --- acme-companion -----------------------------------------------------
if docker ps --format '{{.Names}}' 2>/dev/null | grep -qx "$PROXY_CONTAINER"; then
    if docker exec "$PROXY_CONTAINER" test -f "$PROXY_CERTS_PATH/$DOMAIN.crt" 2>/dev/null; then
        # -L follows the symlink. Without it docker cp copies the link
        # itself, and what lands on the host points at a directory that only
        # exists inside the container - a file that reports success and is not
        # there.
        docker cp -L "$PROXY_CONTAINER:$PROXY_CERTS_PATH/$DOMAIN.crt" "$CERTS/fullchain.pem"
        docker cp -L "$PROXY_CONTAINER:$PROXY_CERTS_PATH/$DOMAIN.key" "$CERTS/privkey.pem"
        FOUND="$PROXY_CONTAINER:$PROXY_CERTS_PATH"
    fi
fi

# --- certbot ------------------------------------------------------------
if [ -z "$FOUND" ] && [ -f "$LIVE/fullchain.pem" ] && [ -f "$LIVE/privkey.pem" ]; then
    cp "$LIVE/fullchain.pem" "$CERTS/fullchain.pem"
    cp "$LIVE/privkey.pem" "$CERTS/privkey.pem"
    FOUND="$LIVE"
fi

if [ -z "$FOUND" ]; then
    echo "no certificate found for $DOMAIN" >&2
    echo >&2
    if docker ps --format '{{.Names}}' 2>/dev/null | grep -qx "$PROXY_CONTAINER"; then
        echo "$PROXY_CONTAINER has these:" >&2
        docker exec "$PROXY_CONTAINER" sh -c "ls -1 $PROXY_CERTS_PATH/*.crt 2>/dev/null" |
            sed 's|.*/||; s|\.crt$||; s|^|  |' >&2 || echo "  (none)" >&2
        echo >&2
        echo "acme-companion issues one per LETSENCRYPT_HOST. If $DOMAIN is not" >&2
        echo "listed, set BOATHUB_DOMAIN in .env, bring the stack up, and give it" >&2
        echo "a minute - the name has to resolve to this host first." >&2
    elif [ -d "$ROOT" ]; then
        echo "certbot has these:" >&2
        ls -1 "$ROOT" 2>/dev/null | sed 's/^/  /' >&2
        echo >&2
        echo "If one of them covers $DOMAIN - a wildcard, say - point CERT_DIR at it." >&2
    else
        echo "Neither $PROXY_CONTAINER nor $ROOT is here, so nothing on this host" >&2
        echo "is issuing certificates that this script knows how to find." >&2
    fi
    exit 1
fi

chown "$MOSQUITTO_UID:$MOSQUITTO_UID" "$CERTS/fullchain.pem" "$CERTS/privkey.pem"
chmod 644 "$CERTS/fullchain.pem"
chmod 600 "$CERTS/privkey.pem"

echo "installed $DOMAIN from $FOUND"

# Mosquitto re-reads its certificate files on SIGHUP, so a renewal costs no
# downtime and drops no session. If the container is not running - a first
# install, before the stack is up - there is nothing to reload.
if docker ps --format '{{.Names}}' | grep -qx boathub-mqtt; then
    docker kill -s HUP boathub-mqtt >/dev/null
    echo "broker reloaded"
else
    echo "broker not running; it will pick the certificate up when it starts"
fi
