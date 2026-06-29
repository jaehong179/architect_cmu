# Timegrapher API Specification

A REST API for storing and retrieving watch timegrapher measurement data in the cloud.

| Item | Detail |
|------|--------|
| Version | v1.6 |
| Last Updated | 2026-06-23 |
| Protocol | HTTPS |
| Data Format | JSON |
| Authentication | None (Open) — *API key recommended for production* |
| Backend | AWS API Gateway + Lambda + DynamoDB |
| Region | us-east-1 (N. Virginia) |

---

## Base URL

```
https://i5dhq7t6fb.execute-api.us-east-1.amazonaws.com/default/timegrapher_api
```

In this document, `{BASE_URL}` refers to the address above.

---

## Endpoint Summary

| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/timegrapher_api` | Store a single record (with multiple positions) |
| `GET` | `/timegrapher_api` | List all registered watches |
| `GET` | `/timegrapher_api?watch_id={id}` | Retrieve measurement history for a watch |

> The `GET` behavior depends on the presence of the `watch_id` parameter. Without it, the API returns the **list of all watches**; with it, the API returns the **measurement history for that watch**.

---

## watch_id Policy

`watch_id` is the unique key identifying a watch and is **provided directly by the client (user)**. Using the watch's serial number as-is is recommended.

- Measurements sent with the same `watch_id` accumulate as that watch's history.
- To group history correctly, the **same `watch_id` must always be entered** for the same watch.
- To reduce inconsistency, defining an input convention (e.g. unifying case and hyphens) within the team is recommended.

---

## Measurement Position Codes

The standard position codes used as keys in `measurements` are the following six:

| Code | Position | Description |
|------|----------|-------------|
| `DU` | Dial Up | Dial (face) pointing up |
| `DD` | Dial Down | Dial pointing down |
| `CU` | Crown Up | Crown pointing up |
| `CD` | Crown Down | Crown pointing down |
| `CL` | Crown Left | Crown pointing left |
| `CR` | Crown Right | Crown pointing right |

> You do not need to send all six positions — include only the positions that were measured.

---

## Data Structure Overview

A single measurement record consists of watch information plus **per-position measurement values**. Per-position values are stored inside the `measurements` object, **keyed by position code**.

```json
{
  "watch_id": "rolex_123456",
  "engineer": "Mr. Park",
  "comment": "Measured after overhaul",
  "measurements": {
    "DU": { "rate": -3.2, "amplitude": 265, "beat_error": 0.4 },
    "DD": { "rate": -2.8, "amplitude": 270, "beat_error": 0.3 },
    "CD": { "rate": -5.1, "amplitude": 255, "beat_error": 0.6 }
  }
}
```

> Since position codes are used as keys, each position is stored only once (duplicates are automatically prevented), and on retrieval you can access values directly via `measurements["DU"]`.

---

## 1. Store Measurement — POST

Stores a single timegrapher record. Multiple position measurements can be included in one request. The timestamp (`measured_at`) is recorded automatically by the server.

### Request

```
POST {BASE_URL}
Content-Type: application/json
```

### Request Body

#### Top-level Fields

| Field | Type | Required | Description | Example |
|-------|------|:--------:|-------------|---------|
| `watch_id` | string | ✅ | Unique watch identifier (user-provided; serial recommended) | `"rolex_123456"` |
| `engineer` | string | | Assigned engineer's name | `"Mr. Park"` |
| `comment` | string | | Engineer's repair notes | `"Measured after overhaul"` |
| `measurements` | object | | Per-position measurement values (see below) | — |

> **Only `watch_id` is required.** Omitting it returns a `400` error. `engineer`, `comment`, and `measurements` are optional.

#### `measurements` Object Structure

`measurements` uses **position codes as keys** and measurement-value objects as values.

```
"measurements": {
  "<position_code>": { "rate": ..., "amplitude": ..., "beat_error": ... },
  ...
}
```

| Field (inside each position) | Type | Description | Example |
|------|------|-------------|---------|
| `rate` | number | Daily rate (seconds/day) | `-3.2` |
| `amplitude` | number | Amplitude (degrees) | `265` |
| `beat_error` | number | Beat error (ms) | `0.5` |

> Position codes use the six standard codes (`DU`, `DD`, `CU`, `CD`, `CL`, `CR`) from the "Measurement Position Codes" table above.

### Request Example

```json
{
  "watch_id": "rolex_123456",
  "engineer": "Mr. Park",
  "comment": "Measured after overhaul (6 positions)",
  "measurements": {
    "DU": { "rate": -3.2, "amplitude": 265, "beat_error": 0.4 },
    "DD": { "rate": -2.8, "amplitude": 270, "beat_error": 0.3 },
    "CU": { "rate": -1.5, "amplitude": 268, "beat_error": 0.2 },
    "CD": { "rate": -5.1, "amplitude": 255, "beat_error": 0.6 },
    "CL": { "rate": -4.0, "amplitude": 260, "beat_error": 0.5 },
    "CR": { "rate": -2.2, "amplitude": 263, "beat_error": 0.3 }
  }
}
```

### Response (200 OK)

```json
{
  "message": "save complete"
}
```

### Response (400 Bad Request) — Missing Required Field

```json
{
  "error": "required fields missing",
  "missing_fields": ["watch_id"]
}
```

---

## 2. List All Watches — GET (no parameters)

Returns a **summary list** of all registered watches, sorted by **most recently measured first**.

### Request

```
GET {BASE_URL}
```

> No query parameters.

### Response (200 OK)

```json
[
  {
    "watch_id": "omega_789011",
    "last_measured_at": "2026-06-23T08:15:00+00:00",
    "count": 3
  },
  {
    "watch_id": "rolex_123456",
    "last_measured_at": "2026-06-23T05:31:12+00:00",
    "count": 5
  }
]
```

### Response Fields

| Field | Type | Description |
|-------|------|-------------|
| `watch_id` | string | Unique watch identifier |
| `last_measured_at` | string | Last measurement time (ISO 8601, UTC) |
| `count` | number | Total number of records for this watch |

> Select a `watch_id` from this list and call **3. Retrieve Measurement History** below to view that watch's detailed history (a typical "list → detail" flow).

---

## 3. Retrieve Measurement History — GET (with watch_id)

Returns up to 100 records for a specific watch, sorted by **most recent first**.

### Request

```
GET {BASE_URL}?watch_id={watch_id}
```

### Query Parameters

| Parameter | Type | Required | Description |
|-----------|------|:--------:|-------------|
| `watch_id` | string | ✅ | Unique identifier of the watch to query |

### Request Example

```
GET {BASE_URL}?watch_id=rolex_123456
```

### Response (200 OK)

Returns an array of record objects (most recent record appears first).

```json
[
  {
    "watch_id": "rolex_123456",
    "measured_at": "2026-06-23T05:31:12.482931+00:00",
    "engineer": "Mr. Park",
    "comment": "Measured after overhaul (6 positions)",
    "measurements": {
      "DU": { "rate": -3.2, "amplitude": 265, "beat_error": 0.4 },
      "DD": { "rate": -2.8, "amplitude": 270, "beat_error": 0.3 },
      "CD": { "rate": -5.1, "amplitude": 255, "beat_error": 0.6 }
    }
  }
]
```

### Response Fields

| Field | Type | Description |
|-------|------|-------------|
| `watch_id` | string | Unique watch identifier |
| `measured_at` | string | Storage timestamp (ISO 8601, UTC) |
| `engineer` | string | Assigned engineer's name |
| `comment` | string | Repair notes |
| `measurements` | object | Per-position measurement values |

---

## Error Responses

Errors are returned in the following format:

```json
{
  "error": "Error message"
}
```

| Status Code | Meaning | When It Occurs |
|:-----------:|---------|----------------|
| `400` | Bad Request | `watch_id` missing, or `measurements` is not an object |
| `405` | Method Not Allowed | Unsupported HTTP method |
| `500` | Internal Server Error | Internal failure (e.g. DB access error) |

---

## Data Model

### DynamoDB Table: `timegrapher_measurements`

| Attribute | Key Type | Type | Description |
|-----------|----------|------|-------------|
| `watch_id` | Partition Key (PK) | String | Groups records by watch (user-provided) |
| `measured_at` | Sort Key (SK) | String | Chronological sorting & querying (server-generated) |

> Records sharing the same `watch_id` are stored sorted by `measured_at`, allowing efficient chronological retrieval of a watch's history.

> **Numeric storage note:** DynamoDB cannot store floating-point (`float`) values directly, so the server automatically converts all numbers inside `measurements` before storing. Clients should send plain JSON numbers.

> **List query note:** The list-all-watches endpoint (#2) performs a full table `Scan` and aggregates by watch. This is efficient at small scale but may degrade in performance as the number of records grows large.

---

## Usage Examples

### cURL (Linux / Raspberry Pi)

**Store**
```bash
curl -X POST "{BASE_URL}" \
  -H "Content-Type: application/json" \
  -d '{
    "watch_id": "rolex_123456",
    "engineer": "Mr. Park",
    "comment": "Measured after overhaul",
    "measurements": {
      "DU": {"rate": -3.2, "amplitude": 265, "beat_error": 0.4},
      "DD": {"rate": -2.8, "amplitude": 270, "beat_error": 0.3},
      "CD": {"rate": -5.1, "amplitude": 255, "beat_error": 0.6}
    }
  }'
```

**List all watches**
```bash
curl "{BASE_URL}"
```

**Retrieve a watch's history**
```bash
curl "{BASE_URL}?watch_id=rolex_123456"
```

### Python

```python
import requests

BASE_URL = "https://i5dhq7t6fb.execute-api.us-east-1.amazonaws.com/default/timegrapher_api"

# Store
requests.post(BASE_URL, json={
    "watch_id": "rolex_123456",
    "engineer": "Mr. Park",
    "comment": "Measured after overhaul",
    "measurements": {
        "DU": {"rate": -3.2, "amplitude": 265, "beat_error": 0.4},
        "DD": {"rate": -2.8, "amplitude": 270, "beat_error": 0.3},
        "CD": {"rate": -5.1, "amplitude": 255, "beat_error": 0.6},
    }
})

# List all watches
watches = requests.get(BASE_URL).json()

# Retrieve a watch's history
record = requests.get(BASE_URL, params={"watch_id": "rolex_123456"}).json()[0]

# Access a specific position directly
du = record["measurements"]["DU"]
print(du["rate"], du["amplitude"])

# Iterate over all positions
for position, m in record["measurements"].items():
    print(position, m["rate"], m["amplitude"])
```

---

## Notes

- All text uses **UTF-8** encoding. When sending non-ASCII text, specifying `charset=utf-8` in the `Content-Type` header is recommended.
- `measured_at` is automatically recorded as the **UTC timestamp at the moment of storage** on the server.
- Position codes in `measurements` use the six standard codes (`DU`, `DD`, `CU`, `CD`, `CL`, `CR`).
- The API is currently in an **Open** (unauthenticated) state. Applying an **API key** or **IAM authentication** is recommended for shared/production environments.
- The measurement history endpoint (#3) returns up to **100 records** per request.

---

## Change Log

| Version | Date | Changes |
|---------|------|---------|
| v1.0 | 2026-06-22 | Initial release (store / history retrieval) |
| v1.1 | 2026-06-22 | Added list-all-watches endpoint |
| v1.2 | 2026-06-22 | Restructured per-position values into a `measurements` object |
| v1.3 | 2026-06-23 | Removed `bph` field; documented user-provided `watch_id` policy |
| v1.4 | 2026-06-23 | Removed `model` field |
| v1.5 | 2026-06-23 | Removed `watch_user` field |
| v1.6 | 2026-06-23 | Standardized position codes (DU/DD/CU/CD/CL/CR); reduced required fields to `watch_id` only (engineer & comment optional) |

---

*This document is the API specification for the Timegrapher measurement data management system.*
