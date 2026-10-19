import re
import subprocess
import sys


def verify(badging, name, code):
    package = next((line for line in badging.splitlines() if line.startswith("package: ")), "")
    fields = dict(re.findall(r"(\w+)='([^']*)'", package))
    expected = {"name": "dev.foxhollow", "versionName": name, "versionCode": str(code)}
    if any(fields.get(key) != value for key, value in expected.items()):
        raise ValueError(f"APK_VERSION_MISMATCH expected={expected} actual={fields}")
    print(f"APK_VERSION_VERIFIED {name} {code}")


if __name__ == "__main__":
    apk, name, code, aapt = sys.argv[1:]
    badging = subprocess.check_output([aapt, "dump", "badging", apk], text=True)
    verify(badging, name, code)
