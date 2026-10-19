import pathlib
import re
import sys

report = pathlib.Path(sys.argv[1]).read_text()
expected = pathlib.Path(sys.argv[2]).read_text().strip().lower()
actual = re.findall(r"Signer #\d+ certificate SHA-256 digest: ([a-fA-F0-9]+)", report)
if len(actual) != 1 or actual[0].lower() != expected:
    raise SystemExit("FOXHOLLOW_ANDROID_SIGNING_CERTIFICATE_MISMATCH")
