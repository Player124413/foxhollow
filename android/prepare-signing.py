import os
from pathlib import Path
import shutil

source = Path(__file__).resolve().parent / "signing/public-debug.p12"
store = Path(os.environ["RUNNER_TEMP"]) / "foxhollow-public-debug.p12"
shutil.copyfile(source, store)
store.chmod(0o600)
with open(os.environ["GITHUB_ENV"], "a") as env:
    env.write(f"FOXHOLLOW_KEYSTORE_FILE={store}\n")
    env.write("FOXHOLLOW_KEYSTORE_PASSWORD=android\n")
    env.write("FOXHOLLOW_KEYSTORE_ALIAS=androiddebugkey\n")
