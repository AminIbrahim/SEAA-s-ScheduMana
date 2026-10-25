# SEAA's ScheduMana

**ScheduMana** is a hackathon prototype of an AI-assisted task manager. A laptop webcam checks in an enrolled user, the Python/Flask backend retrieves that user's to-do list from a Google Sheet, and an ESP32 with a 1.69-inch ST7789V3 TFT shows their tasks. A rotary encoder on the display unit lets them select a task; a local Ollama model estimates its completion time and returns three practical tips. A separate ESP32-WROOM-32D supports keypad/PIN entry and access events.

> **Prototype, not a production security system.** Face matching and the locally stored demo PIN are not suitable for protecting sensitive information or controlling a real door. Do not expose the Flask server to the public internet.

## System overview

```text
Laptop webcam --> face_recognition_server.py -- POST /face --> Flask server.py
Google Sheets (published CSV) ---------------------> Flask server.py
ESP32-D keypad -------------------- POST /api/keypad and /api/pin-result --> Flask
ESP32 display <------------------------------- GET /status -------- Flask
ESP32 display -------- POST /api/analyze-task ---> Flask --> Ollama (local AI)
```

The two ESP32 boards communicate **through the laptop over Wi-Fi**, not directly with one another.

## Repository layout

```text
backend/
  server.py                    Flask API, Google Sheets CSV, Ollama calls
  face_recognition_server.py   webcam-to-Flask face recognition (main camera program)
  enroll_face.py               enroll a user by capturing ten face samples
  face_recognition_live.py     standalone recognition test
  face_detection.py            standalone face detection test
  requirements.txt             Python dependencies
  .env.example                 local server configuration template
  faces/                       local biometric embeddings (gitignored)
firmware/
  display_esp32/               TFT, encoder, LEDs, buzzer; polls Flask
  keypad_esp32/                4x4 keypad and PIN events; POSTs to Flask
docs/
  hardware-wiring.md           wiring used in source sketches
  integration-notes.md         endpoint details and current limitations
```

## Requirements

- Windows/macOS/Linux laptop with Python 3.10+, a webcam, and network access to both ESP32s.
- Python packages from `backend/requirements.txt`.
- [Ollama](https://ollama.com/) with the `llama3.2:3b` model for task-time/tips.
- Two ESP32 development boards, an ST7789V3 240x280 TFT, a 4x4 keypad, and other components as wired.
- Arduino IDE with ESP32 board support. Libraries: **Keypad**, **ArduinoJson**, **Adafruit GFX Library**, **Adafruit ST7735 and ST7789 Library**.

## Quick start: laptop

Run commands **from inside the `backend` folder**: the supplied Python scripts use paths relative to the current working directory.

```powershell
cd backend
py -m venv .venv
.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
Copy-Item .env.example .env
```

Edit `.env` and set `GOOGLE_SHEETS_CSV_URL` to the published CSV export URL for the demonstration sheet. A **published Google Sheet may be viewable by anyone with the published link**; use only demo data. The server refreshes tasks on a 60-second cache.

Install [Ollama](https://ollama.com/) and run once:

```powershell
ollama pull llama3.2:3b
```

### Obtain face recognition model files

Download these two models from the official [OpenCV Zoo](https://github.com/opencv/opencv_zoo) and put **both files directly in `backend/`** (not `backend/models/`, since the current scripts reference bare filenames):

1. [face_detection_yunet_2023mar.onnx](https://github.com/opencv/opencv_zoo/blob/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx)
2. [face_recognition_sface_2021dec.onnx](https://github.com/opencv/opencv_zoo/blob/main/models/face_recognition_sface/face_recognition_sface_2021dec.onnx)

Download the **actual ONNX model files**, not small Git LFS pointer files. Model binaries are excluded from this repository. Review their upstream licenses before redistribution.

Enroll authorized demo participants individually, with consent:

```powershell
python enroll_face.py
```

Each participant presses Space to save ten samples. Embeddings are stored as private `.npy` files in `backend/faces/`, which is excluded from Git. Face recognition can be fooled; don't treat it as secure identity proof.

Open **two terminals** in `backend/` (activate your virtual environment in both):

Terminal 1 (Flask server):

```powershell
python server.py
```

Terminal 2 (face recognition, after enrolling participants):

```powershell
python face_recognition_server.py
```

The face program sends results to Flask at `http://127.0.0.1:5000/face`. Flask listens on port 5000 and serves `/status` to the display ESP32.

### Flash the two ESP32 boards

- For **each** firmware sketch, copy its `secrets.example.h` to a new file named `secrets.h` in the same folder and edit Wi-Fi details, server IP, and the **demo-only** keypad PIN. The local `secrets.h` must not be uploaded to GitHub.
- Find the laptop's active Wi-Fi IPv4 address via `ipconfig` on Windows; the two ESP32s must be able to reach it on port `5000`. On restricted campus networks, a private hotspot or travel router may work better.
- In Arduino IDE, open `display_esp32.ino`, select the display board/port, and upload. Repeat with `keypad_esp32.ino` for the keypad board.
- Open Serial Monitor at **115200** on each ESP32 to troubleshoot networking and input.

The uploaded sketches originally contained hard-coded Wi-Fi details, a LAN IP, and a demo PIN. This repository replaces those values with local **untracked** configuration templates; set them before flashing.

## How to use

1. Start Ollama, Flask, and webcam recognition on the laptop.
2. Stand in front of the webcam to check in.
3. See your Google Sheets tasks on the TFT and use the display's rotary encoder to browse/select one.
4. Select a task to request a time estimate and three tips from the local AI model.
5. The separate keypad provides prototype PIN/guest access functions. **The current code does not map keypad A/B/C/D to task navigation.**

## Project limits and safety

- **Authentication is prototype-grade:** the keypad validates a hard-coded PIN on the ESP32, then tells Flask only `ACCESS_GRANTED`/`ACCESS_DENIED`; Flask trusts that event. This is not protected against forged requests.
- **No public hosting:** Flask is exposed on the local Wi-Fi interface without API authentication. Run only on a trusted private network with Windows Firewall configured appropriately.
- **Personal data:** biometric face embeddings and task data should not be committed. Obtain permission from anyone whose face is enrolled.
- **Published Sheet:** the Sheets CSV URL is loaded from `.env` rather than public source. Published Sheets can be accessible to others. Use non-sensitive example tasks.
- **AI estimates:** time estimates are guesses, not guarantees. Ollama must be installed and running for AI task analysis.
- **Session behavior:** recognized access remains active until a manual reset in the current Python server code.
- **GPIO caution:** the display firmware contains rotary encoder assignments including GPIO12, which is an ESP32 strapping pin; hardware connections can affect boot. Verify wiring before powering.
- The face matching threshold is a demonstration setting and has not been established as secure.

## GitHub publishing

See [docs/integration-notes.md](docs/integration-notes.md) for APIs. For first upload, create an empty GitHub repository named `SEAA-ScheduMana`, then upload the *contents* of this folder. Make sure files named `secrets.h`, `.env`, and enrolled `.npy` data are **not** uploaded. See setup instructions in the chat for `git` commands.

## Credits

Created by the SEAA hackathon team. This repository organizes the original team source code with configuration files separated for public sharing.

**License:** No open-source license selected yet. The code is not automatically licensed for reuse just because it is publicly viewable; the team can add a license later.
