// BoatHub telemetry ingest.
//
// Subscribes to the broker and writes rows. It publishes nothing and can make
// no boat do anything - it is a subscriber and only a subscriber.
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
type telemetry struct {
	TS            *time.Time `json:"ts"`
	TimeValid     *bool      `json:"time_valid"`
	UptimeS       *int32     `json:"uptime_s"`
	HeapFree      *int32     `json:"heap_free"`
	RSSIdBm       *int16     `json:"rssi_dbm"`
	ResetReason   *string    `json:"reset_reason"`
	BatteryV      *float32   `json:"battery_v"`
	CabinTempC    *float32   `json:"cabin_temp_c"`
	CabinRH       *float32   `json:"cabin_rh"`
	EngineTempC   *float32   `json:"engine_temp_c"`
	BilgeTempC    *float32   `json:"bilge_temp_c"`
	FridgeTempC   *float32   `json:"fridge_temp_c"`
	BilgeLevelCm  *float32   `json:"bilge_level_cm"`
	SeatalkOnline *bool      `json:"seatalk_online"`
}

const insertTelemetry = `
INSERT INTO telemetry (
  boat_id, ts, time_valid, uptime_s, heap_free, rssi_dbm, reset_reason,
  battery_v, cabin_temp_c, cabin_rh, engine_temp_c, bilge_temp_c,
  fridge_temp_c, bilge_level_cm, seatalk_online
) VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15)`

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

func main() {
	log.SetFlags(log.LstdFlags | log.LUTC)

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	pool := connectDB(ctx, env("DATABASE_URL", ""))
	defer pool.Close()

	broker := fmt.Sprintf("tcp://%s:%s", env("MQTT_HOST", "mosquitto"), env("MQTT_PORT", "1883"))

	opts := mqtt.NewClientOptions().
		AddBroker(broker).
		SetClientID(env("MQTT_CLIENT_ID", "boathub-ingest")).
		SetUsername(env("MQTT_USER", "")).
		SetPassword(env("MQTT_PASS", "")).
		SetAutoReconnect(true).
		SetConnectRetry(true).
		SetConnectRetryInterval(5 * time.Second).
		SetKeepAlive(30 * time.Second)

	// Subscribing from OnConnect rather than after Connect() means the
	// subscriptions come back by themselves after a broker restart.
	opts.OnConnect = func(c mqtt.Client) {
		log.Printf("broker connected: %s", broker)
		for topic, handler := range map[string]mqtt.MessageHandler{
			"boathub/+/telemetry": func(_ mqtt.Client, m mqtt.Message) { onTelemetry(ctx, pool, m) },
			"boathub/+/status":    func(_ mqtt.Client, m mqtt.Message) { onStatus(ctx, pool, m) },
		} {
			if tok := c.Subscribe(topic, 0, handler); tok.Wait() && tok.Error() != nil {
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
		return
	}

	var t telemetry
	if err := json.Unmarshal(m.Payload(), &t); err != nil {
		// Logged with the raw payload and dropped. One bad message must never
		// be able to stop ingest.
		log.Printf("bad payload on %s: %v: %s", m.Topic(), err, m.Payload())
		return
	}

	_, err := pool.Exec(ctx, insertTelemetry,
		id, t.TS, t.TimeValid, t.UptimeS, t.HeapFree, t.RSSIdBm, t.ResetReason,
		t.BatteryV, t.CabinTempC, t.CabinRH, t.EngineTempC, t.BilgeTempC,
		t.FridgeTempC, t.BilgeLevelCm, t.SeatalkOnline)
	if err != nil {
		log.Printf("insert telemetry for %s failed: %v", id, err)
	}
}

func onStatus(ctx context.Context, pool *pgxpool.Pool, m mqtt.Message) {
	id, ok := boatID(m.Topic())
	if !ok {
		return
	}
	payload := strings.TrimSpace(string(m.Payload()))
	if payload == "" {
		return // a cleared retained message, not a state
	}

	online := payload == "online"
	if _, err := pool.Exec(ctx, insertStatus, id, online); err != nil {
		log.Printf("insert status for %s failed: %v", id, err)
		return
	}
	log.Printf("%s is %s", id, payload)
}
