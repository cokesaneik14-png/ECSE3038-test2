import json
import os
import secrets
from datetime import datetime, timezone
from typing import Literal

import paho.mqtt.publish as publish
from dotenv import load_dotenv
from fastapi import FastAPI, Header, HTTPException, Query, Response
from pydantic import BaseModel, Field
from pymongo import ASCENDING, MongoClient
from pymongo.errors import DuplicateKeyError

load_dotenv()
client = MongoClient(os.getenv("MONGODB_URI"), tz_aware=True)
db = client["test2"]
fans = db["fans"]
readings = db["readings"]
fan_settings = db["settings"]

fans.create_index([("mac", ASCENDING)], unique=True)    # one fan per MAC

MQTT_BROKER = "135.232.203.144"
MQTT_NAME = os.getenv("yourname")

app = FastAPI()


class Fan(BaseModel):
    mac: str
    room: str


class Reading(BaseModel):
    mac: str
    humidity: float = Field(ge=0, le=100)


class Settings(BaseModel):
    mode: Literal["auto", "on", "off"]
    max_humidity: float = Field(ge=0, le=100)

@app.post("/fans", status_code=201)
def create_fan(fan: Fan):
    new_fan = fan.model_dump()
    new_fan["key"] = secrets.token_urlsafe(16)
    try:
        fans.insert_one(new_fan)
    except DuplicateKeyError:
        raise HTTPException(status_code=409, detail="A fan with MAC " + fan.mac + " already exists")
    new_fan.pop("_id")
    return new_fan                                   # the only time the key is shown

@app.get("/fans")
def get_fans():
    return list(fans.find({}, {"_id": 0, "key": 0}))

@app.get("/fans/{mac}")
def get_fan(mac: str):
    fan = fans.find_one({"mac": mac}, {"_id": 0, "key": 0})
    if fan is None:
        raise HTTPException(status_code=404, detail="No fan with MAC " + mac)
    return fan

@app.post("/readings", status_code=201)
def create_reading(reading: Reading, x_api_key: str | None = Header(default=None)):
    if x_api_key is None:
        raise HTTPException(status_code=401, detail="Missing X-API-Key header")
    owner = fans.find_one({"key": x_api_key})
    if owner is None:
        raise HTTPException(status_code=401, detail="Unknown API key")
    if owner["mac"] != reading.mac:
        raise HTTPException(status_code=403, detail="This key belongs to another fan")

    new_reading = reading.model_dump()
    new_reading["time"] = datetime.now(timezone.utc)    # the server adds the time
    readings.insert_one(new_reading)
    new_reading.pop("_id")
    return new_reading

@app.get("/readings")
def get_readings(mac: str | None = None, limit: int = Query(default=10, ge=1, le=100)):
    query = {}
    if mac is not None:
        query["mac"] = mac
    return list(readings.find(query, {"_id": 0}).sort("time", -1).limit(limit))

@app.put("/fans/{mac}/settings")
def put_settings(mac: str, new: Settings, response: Response):
    if fans.find_one({"mac": mac}) is None:
        raise HTTPException(status_code=404, detail="No fan with MAC " + mac)

    doc = new.model_dump()
    doc["mac"] = mac
    result = fan_settings.replace_one({"mac": mac}, doc, upsert=True)
    if result.upserted_id is not None:
        response.status_code = 201
    doc.pop("_id", None)

    try:
        publish.single("ecse3038/" + MQTT_NAME + "/fan/settings",
                       json.dumps(new.model_dump()),
                       retain=True, hostname=MQTT_BROKER)
    except Exception as error:
        print("Could not publish the settings:", error)
    return doc









