import base64
import json
import os
from pathlib import Path
import re

bundle = os.environ.get("FOXHOLLOW_ANDROID_SIGNING_BUNDLE")
if not bundle:
    raise SystemExit("FOXHOLLOW_ANDROID_SIGNING_BUNDLE_REQUIRED")
data = json.loads(bundle)
password = data["password"]
alias = data["alias"]
if not re.fullmatch(r"[A-Za-z0-9_-]{20,}", password) or not re.fullmatch(r"[A-Za-z0-9_-]+", alias):
    raise SystemExit("FOXHOLLOW_ANDROID_SIGNING_BUNDLE_INVALID")
print(f"::add-mask::{password}")
store = Path(os.environ["RUNNER_TEMP"]) / "foxhollow-signing.p12"
store.write_bytes(base64.b64decode(data["keystore"], validate=True))
store.chmod(0o600)
with open(os.environ["GITHUB_ENV"], "a") as env:
    env.write(f"FOXHOLLOW_KEYSTORE_FILE={store}\n")
    env.write(f"FOXHOLLOW_KEYSTORE_PASSWORD={password}\n")
    env.write(f"FOXHOLLOW_KEYSTORE_ALIAS={alias}\n")
