# RFID Attendance System — API Server

## Files
```
rfid_server/
├── schema.sql        ← Run this first in MySQL
├── server.js         ← Express API
├── package.json
└── .env.example      ← Copy to .env and fill in your values
```

---

## Setup

### 1. Database
```bash
mysql -u root -p < schema.sql
```

### 2. Install & run
```bash
cp .env.example .env
# Edit .env with your DB credentials

npm install
npm start
# Dev mode with auto-reload:
npm run dev
```

---

## API Reference

All timestamps in responses are **BST (Bangladesh Standard Time, UTC+6)**.

---

### ESP32 Scan Endpoint

#### `POST /api/scan`
Called by the ESP32 every time a card is scanned.

**Request body (JSON):**
```json
{
  "card_number": "0008390566",
  "device_id": "esp32-room1"
}
```

**Response — card found (200):**
```json
{
  "success": true,
  "scanned_at": "2025-04-19 14:32:07.421",
  "timezone": "Asia/Dhaka (UTC+6)",
  "student": {
    "student_name": "Rahim Uddin",
    "id_number": "EEE-2021-045",
    "department": "EEE",
    "dept_name": "Electrical & Electronic Engineering",
    "section": "B",
    "image_path": "uploads/students/student_1713523927421.jpg"
  }
}
```

**Response — card not registered (404):**
```json
{
  "error": "Card not registered",
  "card_number": "0008390566",
  "scanned_at": "2025-04-19 14:32:07.421"
}
```

---

### Students

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET    | `/api/students` | List all students |
| GET    | `/api/students/:id` | Get one student |
| POST   | `/api/students` | Create student (multipart/form-data) |
| PUT    | `/api/students/:id` | Update student |
| DELETE | `/api/students/:id` | Soft-delete student |

**GET filters:** `?department_id=1&section_id=2&search=rahim`

**POST / PUT fields (multipart/form-data):**
| Field | Type | Required |
|-------|------|----------|
| card_number | string | ✓ (POST) |
| student_name | string | ✓ (POST) |
| id_number | string | ✓ (POST) |
| department_id | integer | ✓ (POST) |
| section_id | integer | ✓ (POST) |
| image | file (jpg/png/webp) | optional |

---

### Attendance

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/attendance` | Query logs with filters |
| GET | `/api/attendance/today` | Today's full log (BST date) |
| GET | `/api/attendance/summary` | First-scan summary per student |

**GET /api/attendance filters:**
- `?date=2025-04-19` — single day
- `?from=2025-04-01&to=2025-04-19` — date range
- `?student_id=5`
- `?department_id=1`
- `?section_id=2`

---

### Departments & Sections

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/departments` | All departments |
| GET | `/api/sections` | All sections (`?department_id=1` to filter) |

---

## ESP32 Code Change

In your `.ino` file, change:
```cpp
#define SERVER_URL "http://YOUR_SERVER_IP:3000/api/scan"
```

The ESP32 sends:
```json
{ "card_number": "0008390566", "device_id": "esp32-room1" }
```

---

## Adding a Student (example curl)

```bash
curl -X POST http://localhost:3000/api/students \
  -F "card_number=0008390566" \
  -F "student_name=Rahim Uddin" \
  -F "id_number=EEE-2021-045" \
  -F "department_id=1" \
  -F "section_id=2" \
  -F "image=@/path/to/photo.jpg"
```
