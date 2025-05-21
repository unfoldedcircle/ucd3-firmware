// Helper script to create a hawkBit DDI software deployment object.
// This is used in our UC OTA server which uses the hawkBit API.
//
// Usage:
// node create-ota-hawkbit-meta.js VERSION FILE [FILE2 ...]
//

// const semver = require("semver"); // KISS no installation of dependencies
const fs = require("fs");

const deploymentTemplate = {
  download: "attempt",
  update: "skip",
  maintenanceWindow: "unavailable",
  chunks: [
    {
      part: "dock",
      version: "",
      name: "Dock 3 firmware",
      artifacts: []
    }
  ]
};

const artifactTemplate = {
  filename: "",
  hashes: {
    sha1: "",
    md5: "",
    sha256: ""
  },
  size: 0,
  _links: {
    download: {
      href: ""
    },
    md5sum: {
      href: ""
    }
  }
};

const metadata = deploymentTemplate;
const href_prefix = "${UC_DOWNLOAD_URL}${UC_OTA_TENANT}/${ARTIFACT_PATH}/";

function ensureFileExists(file) {
  if (!fs.existsSync(file)) {
    console.error(`File does not exist: ${file}`);
    process.exit(1);
  }
}

function readHashFromHashFile(file) {
  const data = fs.readFileSync(file, "utf-8");
  const parts = data.split(" ", 1);
  if (parts.length != 1) {
    console.error(`Invalid hash file: ${file}`);
    process.exit(1);
  }

  return parts[0];
}

if (process.argv.length < 4) {
  console.error("Expected at least two arguments: <version> <deployment_file> [<deployment_file>*]");
  process.exit(1);
}

const version = process.argv[2];
// if (!semver.valid(version)) {
//   console.error(`Invalid semver: ${version}`);
//   process.exit(1);
// }

metadata.chunks[0].version = version;

for (let i = 3; i < process.argv.length; i++) {
  const deploymentFile = process.argv[i];
  const deploymentFileMD5 = deploymentFile + ".MD5SUM";
  const deploymentFileSha1 = deploymentFile + ".sha1sum";
  const deploymentFileSha256 = deploymentFile + ".sha256sum";

  ensureFileExists(deploymentFile);
  ensureFileExists(deploymentFileMD5);
  ensureFileExists(deploymentFileSha1);
  ensureFileExists(deploymentFileSha256);

  console.log(`Including deployment file: ${deploymentFile}`);
  const stats = fs.statSync(deploymentFile);

  const artifact = JSON.parse(JSON.stringify(artifactTemplate));
  artifact.filename = deploymentFile;
  artifact.hashes.sha1 = readHashFromHashFile(deploymentFileSha1);
  artifact.hashes.md5 = readHashFromHashFile(deploymentFileMD5);
  artifact.hashes.sha256 = readHashFromHashFile(deploymentFileSha256);
  artifact.size = stats.size;
  artifact._links.download.href = href_prefix + deploymentFile;
  artifact._links.md5sum.href = href_prefix + deploymentFileMD5;

  metadata.chunks[0].artifacts.push(artifact);
}

fs.writeFileSync("deployment.json", JSON.stringify(metadata, null, 2), "utf-8");
