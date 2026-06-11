const fs = require('fs');
const path = require('path');

const jsonPath = path.join(__dirname, '../../data/DeviceSettings.schema.json');
const outPath = path.join(__dirname, '../src/default_device_settings_schema.hpp');
const json = fs.readFileSync(jsonPath, 'utf8').trim();

const out = `#pragma once

// Auto-generated from Platformio/data/DeviceSettings.schema.json — do not edit by hand.
#include <Arduino.h>

static const char kDefaultDeviceSettingsSchema[] = R"OMOTE_SCHEMA(${json})OMOTE_SCHEMA";
`;

fs.writeFileSync(outPath, out);
console.log('Wrote', outPath, '(' + out.length + ' bytes)');
