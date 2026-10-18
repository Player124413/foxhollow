import pathlib
import re

paths = [pathlib.Path('android-build.log')]
if not paths[0].is_file():
    paths = [pathlib.Path('android-sdk.log')]
pattern = re.compile(r'\berror\b|\bfatal\b|\bFAILED:|undefined symbol|AssertionError|ninja: build stopped', re.I)
for path in paths:
    if not path.is_file():
        continue
    lines = path.read_text(errors='replace').splitlines()
    indices = [i for i, line in enumerate(lines) if pattern.search(line)]
    blocks = ['\n'.join(lines[max(0, i - 1):i + 4]) for i in indices[-12:]]
    blocks.append('\n'.join(lines[-10:]))
    for index, block in enumerate(blocks):
        message = block[:3000].replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
        print(f'::error title={path.name}:{index}::{message}')

import xml.etree.ElementTree as ET

for path in pathlib.Path('android/app/build/reports').glob('lint-results-*.xml'):
    for issue in ET.parse(path).getroot().findall('issue'):
        if issue.get('severity') not in ('Error', 'Fatal'):
            continue
        location = issue.find('location')
        message = issue.get('message', '')
        if location is not None:
            message = str(location.attrib) + '\n' + message
        message = message[:3000].replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
        print(f"::error title={issue.get('id')}::{message}")
