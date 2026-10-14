-- Heel, pitch and motion from the IMU, and the knocks it counted.
--
-- Apply to a database that predates it:
--
--   docker exec -i boathub-db psql -U boathub -d boathub \
--     < server/db/migrations/003-motion.sql
--
-- Safe to run more than once. Apply it before deploying the ingest that
-- writes these columns: that ingest checks for them at startup, and every
-- insert fails while they are missing.

ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS heel_deg      real;
ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS heel_deg_min  real;
ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS heel_deg_max  real;
ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS heel_deg_sd   real;

ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS pitch_deg     real;
ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS pitch_deg_min real;
ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS pitch_deg_max real;
ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS pitch_deg_sd  real;

ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS motion_g      real;
ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS motion_g_min  real;
ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS motion_g_max  real;

ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS knocks        integer;
