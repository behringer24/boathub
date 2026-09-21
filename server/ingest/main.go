// BoatHub telemetry ingest.
//
// Subscribes to the broker and writes rows. It publishes nothing and can make
// no boat do anything - it is a subscriber and only a subscriber.
//
// Delivery is deliberately lossless end to end. The board buffers measurements
// while it is at sea and deletes them once the broker acknowledges them, so a
// message the broker accepts but this service never stores would be a
// measurement lost without anyone noticing. Three things prevent that:
//
//   - a persistent session (clean session off), so the broker queues messages
//     while this service is restarting instead of discarding them
//   - QoS 1 subscriptions, so delivery is acknowledged rather than fire-and-forget
//   - manual acknowledgement, sent only after the row is committed
//
// Design: ../../docs/design/A-006-telemetry-storage.md
package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"log"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"
)

// Pointer fields throughout: a field the board did not send stays null in the
// database. "No sensor fitted" and "measured zero" must never look alike.
//
// Fields the firmware sends that are not listed here are ignored by
// encoding/json. That is deliberate - new firmware must never be able to stop
// ingest just by sending something this server has not been taught yet.
//
// A row is normally an aggregate over a measurement window: the bare field is
// the mean, `_min`/`_max` are the extremes. A spot reading is the same shape
// with N = 1 and no extremes.
type telemetry struct {
	TS        *time.Time `json:"ts"`
	TimeValid *bool      `json:"time_valid"`

	// Which clock produced TS. "ntp" and "gps" are trustworthy, "restored" is
	// a lower bound carried across a restart, "none" means only boot_id and
	// the board's uptime order this record. TimeValid stays the field to
	// filter on; this one says why.
	TimeSource *string `json:"time_source"`
	WindowS   *int32     `json:"window_s"`
	N         *int16     `json:"n"`

	// Identity of the record, for deduplication. Absent on firmware that
	// predates them, in which case the row is stored without the check.
	BootID *int32 `json:"boot_id"`
	Seq    *int64 `json:"seq"`

	UptimeS     *int32  `json:"uptime_s"`
	HeapFree    *int32  `json:"heap_free"`
	RSSIdBm     *int16  `json:"rssi_dbm"`
	ResetReason *string `json:"reset_reason"`

	BatteryV    *float32 `json:"battery_v"`
	BatteryVMin *float32 `json:"battery_v_min"`
	BatteryVMax *float32 `json:"battery_v_max"`

	CabinTempC    *float32 `json:"cabin_temp_c"`
	CabinTempCMin *float32 `json:"cabin_temp_c_min"`
	CabinTempCMax *float32 `json:"cabin_temp_c_max"`

	CabinRH    *float32 `json:"cabin_rh"`
	CabinRHMin *float32 `json:"cabin_rh_min"`
	CabinRHMax *float32 `json:"cabin_rh_max"`

	EngineTempC    *float32 `json:"engine_temp_c"`
	EngineTempCMin *float32 `json:"engine_temp_c_min"`
	EngineTempCMax *float32 `json:"engine_temp_c_max"`

	BilgeTempC    *float32 `json:"bilge_temp_c"`
	BilgeTempCMin *float32 `json:"bilge_temp_c_min"`
	BilgeTempCMax *float32 `json:"bilge_temp_c_max"`

	FridgeTempC    *float32 `json:"fridge_temp_c"`
	FridgeTempCMin *float32 `json:"fridge_temp_c_min"`
	FridgeTempCMax *float32 `json:"fridge_temp_c_max"`

	BilgeLevelCm    *float32 `json:"bilge_level_cm"`
	BilgeLevelCmMin *float32 `json:"bilge_level_cm_min"`
	BilgeLevelCmMax *float32 `json:"bilge_level_cm_max"`

	SeatalkOnline *bool `json:"seatalk_online"`
}

const insertTelemetry = `
INSERT INTO telemetry (
  boat_id, ts, time_valid, time_source, window_s, n,
  boot_id, seq,
  uptime_s, heap_free, rssi_dbm, reset_reason,
  battery_v, battery_v_min, battery_v_max,
  cabin_temp_c, cabin_temp_c_min, cabin_temp_c_max,
  cabin_rh, cabin_rh_min, cabin_rh_max,
  engine_temp_c, engine_temp_c_min, engine_temp_c_max,
  bilge_temp_c, bilge_temp_c_min, bilge_temp_c_max,
  fridge_temp_c, fridge_temp_c_min, fridge_temp_c_max,
  bilge_level_cm, bilge_level_cm_min, bilge_level_cm_max,
  seatalk_online
) VALUES (
  $1,$2,$3,$4,$5,$6,
  $7,$8,
  $9,$10,$11,$12,
  $13,$14,$15,
  $16,$17,$18,
  $19,$20,$21,
  $22,$23,$24,
  $25,$26,$27,
  $28,$29,$30,
  $31,$32,$33,
  $34)`

// Claims an identity before the row is written. Returns no rows when the pair
// has been seen, which is the whole mechanism: QoS 1 redelivers, and a second
// copy has to be recognised rather than counted.
const claimTelemetry = `
INSERT INTO telemetry_seen (boat_id, boot_id, seq)
VALUES ($1, $2, $3)
ON CONFLICT DO NOTHING`

const insertStatus = `INSERT INTO boat_status (boat_id, online) VALUES ($1, $2)`

func env(key, fallback string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return fallback
}

// boatID pulls the id out of "boathub/<id>/telemetry".
func boatID(topic string) (string, bool) {
	parts := strings.Split(topic, "/")
	if len(parts) != 3 || parts[0] != "boathub" || parts[1] == "" {
		return "", false
	}
	return parts[1], true
}

// The database may still be starting when this service is. Keep trying: a
// container that exits because a dependency was slow is a container that needs
// watching, and nobody watches a boat server.
func connectDB(ctx context.Context, url string) *pgxpool.Pool {
	delay := 2 * time.Second
	for {
		pool, err := pgxpool.New(ctx, url)
		if err == nil {
			if err = pool.Ping(ctx); err == nil {
				log.Println("database connected")
				return pool
			}
			pool.Close()
		}
		log.Printf("database not ready (%v), retrying in %s", err, delay)
		time.Sleep(delay)
		if delay < 30*time.Second {
			delay *= 2
		}
	}
}

// insertWithRetry rides out a brief database hiccup. It deliberately gives up
// quickly: an unacknowledged message is redelivered by the broker on the next
// reconnect, and a handler that blocks for minutes would stall every other
// message behind it.
func insertWithRetry(ctx context.Context, pool *pgxpool.Pool, sql string, args ...any) error {
	var err error
	delay := 200 * time.Millisecond
	for attempt := 1; attempt <= 3; attempt++ {
		if _, err = pool.Exec(ctx, sql, args...); err == nil {
			return nil
		}
		if attempt < 3 {
			time.Sleep(delay)
			delay *= 2
		}
	}
	return err
}

// The columns the insert above names. Kept next to it deliberately: the two
// have to agree, and a mismatch is otherwise only discovered as a failing
// insert on every single message.
var required = []string{
	"boat_id", "ts", "time_valid", "time_source", "window_s", "n",
	"boot_id", "seq",
	"uptime_s", "heap_free", "rssi_dbm", "reset_reason",
	"battery_v", "battery_v_min", "battery_v_max",
	"cabin_temp_c", "cabin_temp_c_min", "cabin_temp_c_max",
	"cabin_rh", "cabin_rh_min", "cabin_rh_max",
	"engine_temp_c", "engine_temp_c_min", "engine_temp_c_max",
	"bilge_temp_c", "bilge_temp_c_min", "bilge_temp_c_max",
	"fridge_temp_c", "fridge_temp_c_min", "fridge_temp_c_max",
	"bilge_level_cm", "bilge_level_cm_min", "bilge_level_cm_max",
	"seatalk_online",
}

// db/init only runs when the data directory is created, so a database that
// predates a schema change keeps the old columns and every insert fails with
// the same error, forever. Said once at startup it is a diagnosis; discovered
// through the insert log it is an afternoon.
//
// This does not exit. A service that dies on a bad environment is a service
// somebody has to watch, and nobody watches a boat server - so it says what is
// wrong as loudly as it can and carries on.
func checkSchema(ctx context.Context, pool *pgxpool.Pool) {
	const q = `SELECT column_name FROM information_schema.columns WHERE table_name = 'telemetry'`

	rows, err := pool.Query(ctx, q)
	if err != nil {
		log.Printf("could not inspect the telemetry table: %v", err)
		return
	}
	defer rows.Close()

	have := map[string]bool{}
	for rows.Next() {
		var name string
		if err := rows.Scan(&name); err == nil {
			have[name] = true
		}
	}

	if len(have) == 0 {
		log.Println("!!! there is no telemetry table - did db/init run?")
		return
	}

	var missing []string
	for _, c := range required {
		if !have[c] {
			missing = append(missing, c)
		}
	}
	if len(missing) == 0 {
		log.Printf("schema ok, %d columns", len(have))
		return
	}

	log.Printf("!!! the telemetry table is missing %d column(s): %s",
		len(missing), strings.Join(missing, ", "))
	log.Println("!!! this database predates the current schema and every insert will fail")
	log.Println("!!! see \"Changing the schema\" in server/README.md")
}

func main() {
	log.SetFlags(log.LstdFlags | log.LUTC)

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	pool := connectDB(ctx, env("DATABASE_URL", ""))
	defer pool.Close()

	checkSchema(ctx, pool)

	broker := fmt.Sprintf("tcp://%s:%s", env("MQTT_HOST", "mosquitto"), env("MQTT_PORT", "1883"))

	opts := mqtt.NewClientOptions().
		AddBroker(broker).
		SetClientID(env("MQTT_CLIENT_ID", "boathub-ingest")).
		SetUsername(env("MQTT_USER", "")).
		SetPassword(env("MQTT_PASS", "")).
		// Reconnecting is supervised here rather than left to the library -
		// see reconnectLoop.
		SetAutoReconnect(false).
		SetConnectRetry(false).
		SetKeepAlive(30 * time.Second).
		// A persistent session. While this service is down the broker holds
		// messages for it instead of dropping them, which is what makes a
		// restart or a redeploy cost nothing. Requires a stable client id.
		SetCleanSession(false).
		// Acknowledge only after the row is written - see the file header.
		SetAutoAckDisabled(true)

	// Subscribing from OnConnect rather than after Connect() means the
	// subscriptions come back by themselves after a broker restart.
	opts.OnConnect = func(c mqtt.Client) {
		log.Printf("broker connected: %s", broker)
		for topic, handler := range map[string]mqtt.MessageHandler{
			"boathub/+/telemetry": func(_ mqtt.Client, m mqtt.Message) { onTelemetry(ctx, pool, m) },
			"boathub/+/status":    func(_ mqtt.Client, m mqtt.Message) { onStatus(ctx, pool, m) },
		} {
			// QoS 1: at-least-once. The board deletes buffered measurements once
			// they are acknowledged, so fire-and-forget would lose them silently.
			if tok := c.Subscribe(topic, 1, handler); tok.Wait() && tok.Error() != nil {
				log.Printf("subscribe %s failed: %v", topic, tok.Error())
			} else {
				log.Printf("subscribed to %s", topic)
			}
		}
	}
	opts.OnConnectionLost = func(_ mqtt.Client, err error) {
		log.Printf("broker connection lost: %v", err)
	}

	client := mqtt.NewClient(opts)
	go reconnectLoop(ctx, client)

	<-ctx.Done()
	log.Println("shutting down")
	client.Disconnect(500)
}

// Keeps the broker connection up, and says so when it cannot.
//
// The library has an automatic reconnect of its own and it is deliberately not
// used. When the broker was restarted underneath a live connection it stayed
// down silently: the broker's own log recorded no further connection attempt
// from this service at all, while measurements queued up for a session that
// never came back. A telemetry service that stops listening without saying so
// is worse than one that never started.
//
// So the connection is driven from here, the way the board drives its own:
// ask whether it is open, and if it is not, connect. IsConnectionOpen rather
// than IsConnected, because the latter answers optimistically while a
// reconnect is merely intended.
func reconnectLoop(ctx context.Context, client mqtt.Client) {
	const interval = 5 * time.Second

	ticker := time.NewTicker(interval)
	defer ticker.Stop()

	attempts := 0
	for {
		if !client.IsConnectionOpen() {
			attempts++
			tok := client.Connect()
			if tok.WaitTimeout(10*time.Second) && tok.Error() != nil {
				// Loud at first, then occasional. A broker that is down for a
				// night must not bury everything else in the log, and a broker
				// that is down at all must not be invisible.
				if attempts <= 3 || attempts%60 == 0 {
					log.Printf("broker connect failed, attempt %d: %v", attempts, tok.Error())
				}
			} else {
				attempts = 0
			}
		}

		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
		}
	}
}

func onTelemetry(ctx context.Context, pool *pgxpool.Pool, m mqtt.Message) {
	id, ok := boatID(m.Topic())
	if !ok {
		log.Printf("ignoring unexpected topic %q", m.Topic())
		m.Ack() // nothing here will ever parse; redelivering it forever helps nobody
		return
	}

	batch, err := decodeBatch(m.Payload())
	if err != nil {
		// Logged with the raw payload and acknowledged. A message that cannot be
		// parsed will not parse on redelivery either, and leaving it unacknowledged
		// would block every message behind it.
		log.Printf("bad payload on %s: %v: %s", m.Topic(), err, m.Payload())
		m.Ack()
		return
	}
	if len(batch) == 0 {
		m.Ack()
		return
	}

	stored, err := storeWithRetry(ctx, pool, id, batch)
	if err != nil {
		// Deliberately NOT acknowledged. The broker redelivers on the next
		// reconnect, and its inflight limit becomes backpressure rather than
		// this service quietly dropping measurements it could not store.
		log.Printf("insert telemetry for %s failed, leaving unacknowledged: %v", id, err)
		return
	}
	if stored < len(batch) {
		// Redeliveries of records already in the table. Acknowledged, because
		// they are stored - just not by this delivery.
		log.Printf("%s: %d of %d records already stored", id, len(batch)-stored, len(batch))
	}
	m.Ack()
}

// A telemetry message is an array of records. It was a single object before
// buffering existed, and that shape is still accepted: a board sends what its
// firmware knows how to send, and the server does not get to insist.
func decodeBatch(payload []byte) ([]telemetry, error) {
	trimmed := bytes.TrimLeft(payload, " \t\r\n")
	if len(trimmed) == 0 {
		return nil, nil
	}

	if trimmed[0] == '[' {
		var batch []telemetry
		if err := json.Unmarshal(trimmed, &batch); err != nil {
			return nil, err
		}
		return batch, nil
	}

	var one telemetry
	if err := json.Unmarshal(trimmed, &one); err != nil {
		return nil, err
	}
	return []telemetry{one}, nil
}

// Claims the record's identity and writes the row, both or neither.
//
// The order matters. Claiming first means a crash between the two leaves the
// claim rolled back with the row, so the redelivery that follows still finds
// its way in. Claiming after a successful write would be the same thing with
// more ways to go wrong.
//
// Without an identity - firmware older than this - the row is written
// unconditionally. That is the previous behaviour, duplicates and all, and it
// is the right answer for a board that cannot tell us which message this is.
func storeTelemetry(ctx context.Context, pool *pgxpool.Pool, id string, batch []telemetry) (int, error) {
	tx, err := pool.Begin(ctx)
	if err != nil {
		return 0, err
	}
	defer tx.Rollback(ctx) //nolint:errcheck // no-op once committed

	stored := 0
	for _, t := range batch {
		ok, err := storeOne(ctx, tx, id, t)
		if err != nil {
			return 0, err
		}
		if ok {
			stored++
		}
	}

	return stored, tx.Commit(ctx)
}

// One record inside the batch's transaction. The whole batch commits or none
// of it does, so a redelivery finds either every record already claimed or
// none of them - never half.
func storeOne(ctx context.Context, tx pgx.Tx, id string, t telemetry) (bool, error) {
	if t.BootID != nil && t.Seq != nil {
		tag, err := tx.Exec(ctx, claimTelemetry, id, *t.BootID, *t.Seq)
		if err != nil {
			return false, err
		}
		if tag.RowsAffected() == 0 {
			return false, nil
		}
	}

	if _, err := tx.Exec(ctx, insertTelemetry,
		id, t.TS, t.TimeValid, t.TimeSource, t.WindowS, t.N,
		t.BootID, t.Seq,
		t.UptimeS, t.HeapFree, t.RSSIdBm, t.ResetReason,
		t.BatteryV, t.BatteryVMin, t.BatteryVMax,
		t.CabinTempC, t.CabinTempCMin, t.CabinTempCMax,
		t.CabinRH, t.CabinRHMin, t.CabinRHMax,
		t.EngineTempC, t.EngineTempCMin, t.EngineTempCMax,
		t.BilgeTempC, t.BilgeTempCMin, t.BilgeTempCMax,
		t.FridgeTempC, t.FridgeTempCMin, t.FridgeTempCMax,
		t.BilgeLevelCm, t.BilgeLevelCmMin, t.BilgeLevelCmMax,
		t.SeatalkOnline); err != nil {
		return false, err
	}

	return true, nil
}

func storeWithRetry(ctx context.Context, pool *pgxpool.Pool, id string, batch []telemetry) (int, error) {
	var err error
	delay := 200 * time.Millisecond
	for attempt := 1; attempt <= 3; attempt++ {
		var stored int
		if stored, err = storeTelemetry(ctx, pool, id, batch); err == nil {
			return stored, nil
		}
		if attempt < 3 {
			time.Sleep(delay)
			delay *= 2
		}
	}
	return 0, err
}

func onStatus(ctx context.Context, pool *pgxpool.Pool, m mqtt.Message) {
	id, ok := boatID(m.Topic())
	if !ok {
		m.Ack()
		return
	}
	payload := strings.TrimSpace(string(m.Payload()))
	if payload == "" {
		m.Ack() // a cleared retained message, not a state
		return
	}

	online := payload == "online"
	if err := insertWithRetry(ctx, pool, insertStatus, id, online); err != nil {
		log.Printf("insert status for %s failed, leaving unacknowledged: %v", id, err)
		return
	}
	m.Ack()
	log.Printf("%s is %s", id, payload)
}
