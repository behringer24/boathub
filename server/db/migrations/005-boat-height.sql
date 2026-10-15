-- Each boat's barometer height, which the dashboard reduces station pressure
-- to sea level with.
--
-- Apply to a database that predates it:
--
--   docker exec -i boathub-db psql -U boathub -d boathub \
--     < server/db/migrations/005-boat-height.sql
--
-- Safe to run more than once. Ingest does not touch this table, so it can be
-- applied before or after a deploy. Until it is, the Sea-level pressure panel
-- reports that the relation does not exist; after it, the panel stays empty
-- until the boat has a row - server/README.md has the statement.

CREATE TABLE IF NOT EXISTS boats (
    boat_id  text PRIMARY KEY,
    height_m real NOT NULL CHECK (height_m BETWEEN -500 AND 5000)
);
