-- Station pressure and the battery bay's air from the BME680, and the gas
-- resistance it reports only with its heater switched on.
--
-- Apply to a database that predates it:
--
--   docker exec -i boathub-db psql -U boathub -d boathub \
--     < server/db/migrations/004-barometer.sql
--
-- Safe to run more than once. Apply it before deploying the ingest that
-- writes these columns: that ingest checks for them at startup, and every
-- insert fails while they are missing.

ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS pressure_hpa      real;
ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS pressure_hpa_min  real;
ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS pressure_hpa_max  real;

ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS bay_temp_c        real;
ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS bay_temp_c_min    real;
ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS bay_temp_c_max    real;

ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS bay_rh            real;
ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS bay_rh_min        real;
ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS bay_rh_max        real;

ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS bay_gas_kohm      real;
ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS bay_gas_kohm_min  real;
ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS bay_gas_kohm_max  real;
