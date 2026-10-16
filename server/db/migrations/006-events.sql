-- Events from the board: its time, its identity, and the columns a dashboard
-- filters on, beside the whole event as it was sent.
--
-- Apply to a database that predates it:
--
--   docker exec -i boathub-db psql -U boathub -d boathub \
--     < server/db/migrations/006-events.sql
--
-- Safe to run more than once. Apply it before deploying the ingest that
-- writes events: that ingest checks for these columns at startup, and every
-- event insert fails while they are missing.

ALTER TABLE events ADD COLUMN IF NOT EXISTS ts          timestamptz;
ALTER TABLE events ADD COLUMN IF NOT EXISTS time_valid  boolean;
ALTER TABLE events ADD COLUMN IF NOT EXISTS time_source text;
ALTER TABLE events ADD COLUMN IF NOT EXISTS boot_id     integer;
ALTER TABLE events ADD COLUMN IF NOT EXISTS seq         bigint;
ALTER TABLE events ADD COLUMN IF NOT EXISTS subject     text;
ALTER TABLE events ADD COLUMN IF NOT EXISTS state       text;
ALTER TABLE events ADD COLUMN IF NOT EXISTS severity    text;

CREATE UNIQUE INDEX IF NOT EXISTS events_identity_idx ON events (boat_id, boot_id, seq);
