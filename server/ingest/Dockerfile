# Build
FROM golang:1.25-alpine AS build
WORKDIR /src

# Dependencies first, so editing main.go does not re-download the world.
COPY go.mod go.sum ./
RUN go mod download

COPY . .
RUN CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /ingest .

# Run
FROM alpine:3.21
# Needed the day the broker or the database moves to TLS.
RUN apk add --no-cache ca-certificates && adduser -D -u 10001 ingest
USER ingest
COPY --from=build /ingest /usr/local/bin/ingest
ENTRYPOINT ["/usr/local/bin/ingest"]
