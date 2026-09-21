-- Which clock produced a record's timestamp.
--
-- Apply to a database that predates it:
--
--   docker exec -i boathub-db psql -U boathub -d boathub \
--     < server/db/migrations/002-time-source.sql
--
-- Safe to run more than once.

ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS time_source text;
