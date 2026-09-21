#!/bin/sh
# Copy the broker's certificate out of whatever manages Let's Encrypt on this
# host, and reload the broker.
#
#   sudo server/deploy/install-certs.sh boathub.behringer24.de
#
# Run it once when TLS is first set up, and again after every renewal. certbot
# can do the second part itself:
#
#   /etc/letsencrypt/renewal-hooks/deploy/boathub.sh
#     #!/bin/sh
#     exec /srv/boathub/server/deploy/install-certs.sh boathub.behringer24.de
#
# Why a copy rather than mounting /etc/letsencrypt.
#
# privkey.pem is owned by root and readable by nobody else, and the broker runs
# as an unprivileged user inside its container. Mounting the directory
# read-only does not change that - the container would still be refused. The
# alternatives are running the broker as root or loosening the permissions on
# every key on the host, and a copy that belongs to the broker's own user is
# better than either.

set -eu

DOMAIN="${1:?usage: install-certs.sh <domain>}"
ROOT="${LETSENCRYPT_LIVE:-/etc/letsencrypt/live}"

# Where the certificate actually is.
#
# The directory is named after the first name the certificate was issued for,
# which is not always the name being installed: a wildcard covering the whole
# domain lives under its own name, and a certificate reissued with extra names
# keeps the directory of the first one. CERT_DIR skips the guessing:
#
#   CERT_DIR=/etc/letsencrypt/live/behringer24.de install-certs.sh boathub.behringer24.de
LIVE="${CERT_DIR:-$ROOT/$DOMAIN}"

HERE="$(cd "$(dirname "$0")/.." && pwd)"
CERTS="$HERE/mosquitto/certs"

# The uid mosquitto runs as inside eclipse-mosquitto:2. The files are chowned
# to it because the container has no way to read anything else.
MOSQUITTO_UID=1883

for f in fullchain.pem privkey.pem; do
    if [ ! -f "$LIVE/$f" ]; then
        echo "not found: $LIVE/$f" >&2
        echo >&2
        if [ -d "$ROOT" ]; then
            echo "Certificates this host does have:" >&2
            ls -1 "$ROOT" 2>/dev/null | sed 's/^/  /' >&2
            echo >&2
            echo "If one of those covers $DOMAIN - a wildcard, say - point CERT_DIR at it." >&2
            echo "\"certbot certificates\" lists which names each one is valid for." >&2
        else
            echo "$ROOT does not exist, so certbot is not what manages certificates here." >&2
            echo "A proxy that handles ACME itself - Caddy, Traefik, acme-companion -" >&2
            echo "keeps them somewhere of its own, often inside a Docker volume." >&2
        fi
        exit 1
    fi
done

mkdir -p "$CERTS"
cp "$LIVE/fullchain.pem" "$CERTS/fullchain.pem"
cp "$LIVE/privkey.pem" "$CERTS/privkey.pem"

chown "$MOSQUITTO_UID:$MOSQUITTO_UID" "$CERTS/fullchain.pem" "$CERTS/privkey.pem"
chmod 644 "$CERTS/fullchain.pem"
chmod 600 "$CERTS/privkey.pem"

echo "installed $DOMAIN into $CERTS"

# Mosquitto re-reads its certificate files on SIGHUP, so a renewal costs no
# downtime and no dropped sessions. If the container is not running - a first
# install, before the stack is up - there is nothing to reload.
if docker ps --format '{{.Names}}' | grep -qx boathub-mqtt; then
    docker kill -s HUP boathub-mqtt >/dev/null
    echo "broker reloaded"
else
    echo "broker not running; it will pick the certificate up when it starts"
fi
