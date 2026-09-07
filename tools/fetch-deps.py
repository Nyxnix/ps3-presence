from pathlib import Path
import hashlib,tarfile,urllib.request
from embed_roots import render_roots
root=Path(__file__).resolve().parent.parent
version='3.6.6'
archive=root/'deps'/f'mbedtls-{version}.tar.bz2'
source=root/'deps'/f'mbedtls-{version}'
if not (source/'include/mbedtls/version.h').exists():
    archive.parent.mkdir(exist_ok=True)
    expected='8fb65fae8dcae5840f793c0a334860a411f884cc537ea290ce1c52bb64ca007a'
    if archive.exists() and hashlib.sha256(archive.read_bytes()).hexdigest()!=expected:
        archive.unlink()
    if not archive.exists():
        partial=archive.with_suffix(archive.suffix+'.part')
        try:
            urllib.request.urlretrieve(f'https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-{version}/mbedtls-{version}.tar.bz2',partial)
            if hashlib.sha256(partial.read_bytes()).hexdigest()!=expected: raise SystemExit('Mbed TLS archive checksum mismatch')
            partial.replace(archive)
        finally:
            partial.unlink(missing_ok=True)
    with tarfile.open(archive) as t: t.extractall(archive.parent,filter='data')
data=(root/'certs/gts-roots.pem').read_bytes()
(root/'build').mkdir(exist_ok=True)
(root/'build/roots.h').write_text(render_roots(data))
print('Mbed TLS 3.6.6 dependency and embedded GTS trust roots ready')
