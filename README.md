# ECSE3038 Test 2: the bathroom fan
An ESP32 in Wokwi reads a DHT22's humidity every 10 s and POSTs it, with its own
API key, to a FastAPI server backed by MongoDB Atlas. PUT /fans/{mac}/settings sets
the fan's mode (auto, on, off) and max_humidity; the API publishes them as JSON,
retained, on the class MQTT broker, and the board drives the fan LED from them.

Run the API
cd api
python -m venv venv
venv\Scripts\activate (Linux: source venv/bin/activate)
python -m pip install -r requirements.txt
create .env with MONGODB_URI=... and MQTT_NAME=yourname
uvicorn app:app --reload (VM: --host 127.0.0.1 --port 8000)

Run the board
Open the board folder in VS Code with PlatformIO.
Create include/secrets.h with: #define DEVICE_KEY ""
Set API_URL and MQTT_NAME in src/main.cpp.
Build, then F1 -> Wokwi: Start Simulator. Copy the MAC from the Serial Monitor.
POST /fans with that MAC; put the returned key in include/secrets.h; build and run again.
MQTT topics
ecse3038/yourname/fan/settings: {"mode", "max_humidity"} from the API (retained)
ecse3038/yourname/fan/state: on or off, from the board (retained)
ecse3038/yourname/status: online, or offline from the last will (retained)