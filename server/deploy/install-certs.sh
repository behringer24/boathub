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
LIVE="${LETSENCRYPT_LIVE:-/etc/letsencrypt/live}/$DOMAIN"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
CERTS="$HERE/mosquitto/certs"

# The uid mosquitto runs as inside eclipse-mosquitto:2. The files are chowned
# to it because the container has no way to read anything else.
MOSQUITTO_UID=1883

for f in fullchain.pem privkey.pem; do
    if [ ! -f "$LIVE/$f" ]; then
        echo "not found: $LIVE/$f" >&2
        echo "Is the certificate for $DOMAIN issued, and is this the right host?" >&2
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
