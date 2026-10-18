import pathlib

for name in ('android-sdk.log', 'android-build.log'):
    path = pathlib.Path(name)
    if not path.is_file():
        continue
    lines = path.read_text(errors='replace').splitlines()
    message = '\n'.join(lines[-100:])[-16000:]
    message = message.replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
    print(f'::error title={name}::{message}')
