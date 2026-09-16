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
	WindowS   *int32     `json:"window_s"`
	N         *int16     `json:"n"`

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
  boat_id, ts, time_valid, window_s, n,
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
  $1,$2,$3,$4,$5,
  $6,$7,$8,$9,
  $10,$11,$12,
  $13,$14,$15,
  $16,$17,$18,
  $19,$20,$21,
  $22,$23,$24,
  $25,$26,$27,
  $28,$29,$30,
  $31)`

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
	"boat_id", "ts", "time_valid", "window_s", "n",
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
		SetAutoReconnect(true).
		SetConnectRetry(true).
		SetConnectRetryInterval(5 * time.Second).
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
	if tok := client.Connect(); tok.Wait() && tok.Error() != nil {
		log.Printf("initial broker connect failed, retrying in background: %v", tok.Error())
	}

	<-ctx.Done()
	log.Println("shutting down")
	client.Disconnect(500)
}

func onTelemetry(ctx context.Context, pool *pgxpool.Pool, m mqtt.Message) {
	id, ok := boatID(m.Topic())
	if !ok {
		log.Printf("ignoring unexpected topic %q", m.Topic())
		m.Ack() // nothing here will ever parse; redelivering it forever helps nobody
		return
	}

	var t telemetry
	if err := json.Unmarshal(m.Payload(), &t); err != nil {
		// Logged with the raw payload and acknowledged. A message that cannot be
		// parsed will not parse on redelivery either, and leaving it unacknowledged
		// would block every message behind it.
		log.Printf("bad payload on %s: %v: %s", m.Topic(), err, m.Payload())
		m.Ack()
		return
	}

	err := insertWithRetry(ctx, pool, insertTelemetry,
		id, t.TS, t.TimeValid, t.WindowS, t.N,
		t.UptimeS, t.HeapFree, t.RSSIdBm, t.ResetReason,
		t.BatteryV, t.BatteryVMin, t.BatteryVMax,
		t.CabinTempC, t.CabinTempCMin, t.CabinTempCMax,
		t.CabinRH, t.CabinRHMin, t.CabinRHMax,
		t.EngineTempC, t.EngineTempCMin, t.EngineTempCMax,
		t.BilgeTempC, t.BilgeTempCMin, t.BilgeTempCMax,
		t.FridgeTempC, t.FridgeTempCMin, t.FridgeTempCMax,
		t.BilgeLevelCm, t.BilgeLevelCmMin, t.BilgeLevelCmMax,
		t.SeatalkOnline)
	if err != nil {
		// Deliberately NOT acknowledged. The broker redelivers on the next
		// reconnect, and its inflight limit becomes backpressure rather than
		// this service quietly dropping measurements it could not store.
		log.Printf("insert telemetry for %s failed, leaving unacknowledged: %v", id, err)
		return
	}
	m.Ack()
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
