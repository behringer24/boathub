-- BoatHub telemetry schema
--
-- Runs once, when the data directory is first created. Changing this file
-- afterwards has no effect on an existing database - drop the volume or write
-- a migration.
--
-- Design: ../../docs/design/A-006-telemetry-storage.md

CREATE EXTENSION IF NOT EXISTS timescaledb;
-- Not used yet. Present from the start so the track logger does not need the
-- whole database migrated later.
CREATE EXTENSION IF NOT EXISTS postgis;

-- One row per received message.
--
-- A row is normally an AGGREGATE over a measurement window, not a single
-- reading: the board measures every 10 s and publishes every 5 min. Storing
-- every reading would be 30x the rows for data that changes over minutes, and
-- - more to the point - the board has to buffer these while it is at sea, and
-- its flash is 6 MB.
--
-- The bare column carries the MEAN over the window. `_min` and `_max` carry the
-- extremes, because a mean alone hides exactly what matters: a fridge that
-- spiked, a bilge that touched freezing, a battery that sagged under the
-- compressor.
--
-- A single spot reading - the BOOT button, or the first message after a
-- restart - is the same shape with n = 1: the bare column holds the reading and
-- `_min`/`_max` stay null. There is no second message format.
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

    -- Which clock produced `ts`, so an odd timestamp is a diagnosis rather
    -- than a puzzle:
    --
    --   ntp, gps  synced, trust it
    --   restored  a floor carried across a restart plus elapsed time - the
    --             ordering is right, the absolute time is a lower bound
    --   none      no clock this boot; only boot_id and the board's uptime
    --             order these records
    --
    -- `time_valid` stays the field to filter on; this one says why.
    time_source    text,

    -- The window this row covers. Null on messages from firmware that does not
    -- aggregate yet; n = 1 marks a spot reading.
    window_s       integer,
    n              smallint,

    -- What identifies this record, for deduplication.
    --
    -- The board publishes at QoS 1, which is at-least-once: a message whose
    -- acknowledgement is lost is sent again. Without an identity the second
    -- copy becomes a second row, and the first place that shows is a dashboard
    -- counting the same five minutes twice.
    --
    -- boot_id increments in the board's NVS on every boot, seq counts records
    -- within one boot. Null on messages from firmware that predates them.
    boot_id        integer,
    seq            bigint,

    -- Diagnostics, instantaneous at the moment the window closed. These explain
    -- silent restarts, leaks and radio trouble.
    uptime_s       integer,
    heap_free      integer,
    rssi_dbm       smallint,
    reset_reason   text,

    -- Sensors, filled in as they come up. Bare = mean, plus the extremes.
    battery_v          real,
    battery_v_min      real,
    battery_v_max      real,

    cabin_temp_c       real,
    cabin_temp_c_min   real,
    cabin_temp_c_max   real,

    cabin_rh           real,
    cabin_rh_min       real,
    cabin_rh_max       real,

    engine_temp_c      real,
    engine_temp_c_min  real,
    engine_temp_c_max  real,

    bilge_temp_c       real,
    bilge_temp_c_min   real,
    bilge_temp_c_max   real,

    fridge_temp_c      real,
    fridge_temp_c_min  real,
    fridge_temp_c_max  real,

    bilge_level_cm     real,
    bilge_level_cm_min real,
    bilge_level_cm_max real,

    -- Boolean: nothing to aggregate, taken at window close.
    seatalk_online     boolean
);

SELECT create_hypertable('telemetry', 'received_at');

CREATE INDEX telemetry_boat_time_idx ON telemetry (boat_id, received_at DESC);

-- At one aggregate every 5 min: 288 rows a day, ~105 000 a year, well under
-- 25 MB raw and a few MB compressed. Full resolution is kept indefinitely, so
-- there is no retention policy here on purpose - the question of what to throw
-- away simply does not arise at this scale. Thinning is a problem for the
-- board's 6 MB of flash, not for this table.
ALTER TABLE telemetry SET (
    timescaledb.compress,
    timescaledb.compress_segmentby = 'boat_id',
    timescaledb.compress_orderby   = 'received_at DESC'
);
SELECT add_compression_policy('telemetry', INTERVAL '7 days');

-- The identity of every telemetry record already stored.
--
-- This cannot be a unique index on `telemetry` itself. That table is a
-- hypertable, and TimescaleDB requires every unique index to contain the
-- partitioning column:
--
--   ERROR: cannot create a unique index without the column "received_at"
--
-- Including `received_at` would satisfy the rule and defeat the purpose, since
-- it is stamped on arrival and a redelivered copy therefore carries a
-- different one. So the claim lives in an ordinary table of its own and is
-- written in the same transaction as the row it belongs to: either both exist
-- or neither does.
--
-- It grows by one row per message - some 105 000 a year, a few megabytes. Old
-- rows could be pruned once they are older than any backlog the board could
-- still be holding, but at this size the question does not press.
CREATE TABLE telemetry_seen (
    boat_id text        NOT NULL,
    boot_id integer     NOT NULL,
    seq     bigint      NOT NULL,
    seen_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (boat_id, boot_id, seq)
);

CREATE INDEX telemetry_seen_age_idx ON telemetry_seen (seen_at);

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
