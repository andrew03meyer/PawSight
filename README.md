# PawSight

## Folder Contents

- config and ha-config - HomeAssistant config files
- PawSight_ESP - all code written for the ESP32, in Arduino IDE
- docker-compose.yaml - for setting up HomeAssistant
- hacs.sh - script for installing HACS (HomeAsisstant Community Store) into the Docker container

## Setup

### Docker Setup of HomeAssistant

- Run the Docker compose:
  - cd into the PawSight Folder
  - docker compose up -d --build
- This should install HACS as well. If not go to:
  - https://www.hacs.xyz/docs/use/download/download/#to-download-hacs

### Setting up MQTTX

- Go to: https://mqttx.app/
- Sign in
- Connect to HiveMQTT-Public
- Setup a publish and subscribe model on there for GPS and temperature

### Accessing HomeAssistant

- Go to localhost:8123
- create your user

## Arduino Code

- You'll need to update the Arduino code to match your pub/sub topics