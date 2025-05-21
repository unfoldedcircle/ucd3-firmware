// Quick and dirty helper script to create the license overview page in markdown for the Dock firmware.
//
// Usage:
// node transform-licenses.js licenses.json licenses.md

const fs = require("fs");

function ensureFileExists(file) {
  if (!fs.existsSync(file)) {
    console.error(`File does not exist: ${file}`);
    process.exit(1);
  }
}

if (process.argv.length < 4) {
  console.error("Expected two argument: <licenses.json> <README.md>");
  process.exit(1);
}

const licenseFile = process.argv[2];
const outputFile = process.argv[3];

ensureFileExists(licenseFile);

const licenses = JSON.parse(fs.readFileSync(licenseFile, "utf-8"));

fs.writeFileSync(outputFile, fs.readFileSync("_templates/licenses-header.md", "utf-8"), "utf-8");

// overview

for (const module in licenses) {
  console.log(`${module}: ${licenses[module].License}`);

  const repository = licenses[module].Repository;
  const license = licenses[module].License;
  const version = licenses[module].Version;

  fs.appendFileSync(outputFile, `- ${module} - ${license}\n`, "utf-8");
}

fs.appendFileSync(outputFile, `\n`, "utf-8");

fs.appendFileSync(outputFile, `### Dock 3 firmware license\n\n`, "utf-8");
fs.appendFileSync(outputFile, "```\n", "utf-8");
fs.appendFileSync(outputFile, fs.readFileSync("../../LICENSE", "utf-8").trim(), "utf-8");
fs.appendFileSync(outputFile, "\n```\n\n", "utf-8");

// individual components
for (const module in licenses) {
  const repository = licenses[module].Repository;
  const license = licenses[module].License;
  const version = licenses[module].Version;

  fs.appendFileSync(outputFile, `### ${module} @ ${version}\n\n`, "utf-8");
  if (licenses[module].Description) {
    fs.appendFileSync(outputFile, `${licenses[module].Description}  \n`, "utf-8");
  }

  fs.appendFileSync(outputFile, `- ${license}\n`, "utf-8");
  fs.appendFileSync(outputFile, `- ${repository}\n`, "utf-8");

  const licenseFile = `${module}/${licenses[module].licenseFile}`;
  if (!fs.existsSync(licenseFile)) {
    console.error(`ERROR: ${module} (${licenses[module].License}) no license file found! "${licenseFile}"`);
    continue;
  }

  fs.appendFileSync(outputFile, "\n#### License\n\n", "utf-8");
  fs.appendFileSync(outputFile, "```\n", "utf-8");
  fs.appendFileSync(outputFile, fs.readFileSync(`${licenseFile}`, "utf-8").trim(), "utf-8");
  fs.appendFileSync(outputFile, "\n```\n\n", "utf-8");
}

fs.appendFileSync(outputFile, fs.readFileSync("_templates/licenses-footer.md", "utf-8"), "utf-8");
