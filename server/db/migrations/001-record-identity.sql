-- Record identity, so a redelivered message is not counted twice.
--
-- Apply to a database that predates it:
--
--   docker exec -i boathub-db psql -U boathub -d boathub \
--     < server/db/migrations/001-record-identity.sql
--
-- Safe to run more than once. A database created after this change already
-- carries all of it from db/init/01-schema.sql, and every statement here is
-- guarded.

ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS boot_id integer;
ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS seq     bigint;

CREATE TABLE IF NOT EXISTS telemetry_seen (
    boat_id text        NOT NULL,
    boot_id integer     NOT NULL,
    seq     bigint      NOT NULL,
    seen_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (boat_id, boot_id, seq)
);

CREATE INDEX IF NOT EXISTS telemetry_seen_age_idx ON telemetry_seen (seen_at);
