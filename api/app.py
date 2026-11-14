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
MQTT_NAME = os.getenv("MQTT_NAME")

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




