// Helper script to create firmware metadata file to include in the Hawkbit deployment.
// This is used in our UC OTA server which uses the hawkBit API.
//
// Usage:
// node create-ota-firmware-meta.js FIRMWARE_FILE VERSION CHANNEL
//

const fs = require("fs");

let metadata = {
  "id": "$replaced_by_core",
  "title": "Dock 3 firmware",
  "description": {
    "en": "New release"
  },
  "version": "$VERSION",
  "channel": "DEVELOPMENT",
  "release_date": "$DATE",
  "size": 0
};

function ensureFileExists(file) {
  if (!fs.existsSync(file)) {
    console.error(`File does not exist: ${file}`);
    process.exit(1);
  }
}

// --- start of app

if (process.argv.length != 6) {
  console.error("Expected four arguments: <FIRMWARE_FILE> <VERSION> <CHANNEL> <OUTPUT_FILE>");
  process.exit(1);
}

const outputFile = process.argv[5];
const firmwareFile = process.argv[2];
ensureFileExists(firmwareFile);
const stats = fs.statSync(firmwareFile);
const date = new Date();

metadata.version = process.argv[3];
metadata.channel = process.argv[4];
metadata.release_date = date.toISOString().split('T')[0];
metadata.size = stats.size;

// include release notes
const regex = new RegExp('release-notes_([a-z]{2})\.md');
let changelogs = fs.readdirSync("../../../doc/release").filter(f => regex.test(f));

console.log("Including release notes:", changelogs)

for (let file of changelogs) {
  let lang = file.match(regex)[1];
  console.log("Processing release note language:", lang);
  const notes = fs.readFileSync("../../../doc/release/" + file, "utf-8");
  metadata.description[lang] = notes;
}

fs.writeFileSync(outputFile, JSON.stringify(metadata, null, 2), "utf-8");
