-- BoatHub telemetry schema
--
-- Runs once, when the data directory is first created. Changing this file
-- afterwards has no effect on an existing database - use a migration.
--
-- Design: ../../docs/design/A-006-telemetry-storage.md

CREATE EXTENSION IF NOT EXISTS timescaledb;
-- Not used yet. Present from the start so the track logger does not need the
-- whole database migrated later.
CREATE EXTENSION IF NOT EXISTS postgis;

-- One row per received message.
--
-- Partitioned on received_at, not on the board's own ts: a partitioning column
-- cannot be null, and ts is null until NTP has synced. The server's clock is
-- also the one that can be trusted. ts is kept as the board's own claim.
--
-- Every sensor column is nullable on purpose. A field the board did not send
-- stays null - "no sensor fitted" and "measured zero" must never look alike.
CREATE TABLE telemetry (
    received_at    timestamptz NOT NULL DEFAULT now(),
    boat_id        text        NOT NULL,
    ts             timestamptz,
    time_valid     boolean     NOT NULL DEFAULT false,

    -- Diagnostics. These explain silent restarts, leaks and radio trouble, and
    -- stay in the payload once the sensors arrive.
    uptime_s       integer,
    heap_free      integer,
    rssi_dbm       smallint,
    reset_reason   text,

    -- Sensors, filled in as they come up.
    battery_v      real,
    cabin_temp_c   real,
    cabin_rh       real,
    engine_temp_c  real,
    bilge_temp_c   real,
    fridge_temp_c  real,
    bilge_level_cm real,
    seatalk_online boolean
);

SELECT create_hypertable('telemetry', 'received_at');

CREATE INDEX telemetry_boat_time_idx ON telemetry (boat_id, received_at DESC);

-- About 390 MB a year raw, some 20-40 MB compressed. Full resolution is kept
-- indefinitely, so there is no retention policy here on purpose.
ALTER TABLE telemetry SET (
    timescaledb.compress,
    timescaledb.compress_segmentby = 'boat_id',
    timescaledb.compress_orderby   = 'received_at DESC'
);
SELECT add_compression_policy('telemetry', INTERVAL '7 days');

-- Online/offline transitions from the retained status topic. A handful of rows
-- a week, and the evidence behind any "the boat went quiet" question.
CREATE TABLE boat_status (
    received_at timestamptz NOT NULL DEFAULT now(),
    boat_id     text        NOT NULL,
    online      boolean     NOT NULL
);

CREATE INDEX boat_status_boat_time_idx ON boat_status (boat_id, received_at DESC);

-- Alarms and state changes. Empty until a version raises them.
CREATE TABLE events (
    received_at timestamptz NOT NULL DEFAULT now(),
    boat_id     text        NOT NULL,
    kind        text        NOT NULL,
    payload     jsonb
);

CREATE INDEX events_boat_time_idx ON events (boat_id, received_at DESC);
