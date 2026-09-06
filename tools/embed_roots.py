"""Embed unchanged PEM certificate bytes as DER for no-copy trust loading."""
import base64
import re


def render_roots(pem):
    blocks = re.findall(br'-----BEGIN CERTIFICATE-----\s*(.*?)\s*-----END CERTIFICATE-----', pem, re.S)
    if not blocks:
        raise ValueError('Trust store has no certificates')
    lines = ['/* Generated DER trust anchors; included only by net/roots.c. */']
    for i, block in enumerate(blocks):
        der = base64.b64decode(re.sub(br'\s+', b'', block), validate=True)
        if not der or der[0] != 0x30:
            raise ValueError('Invalid DER certificate')
        lines.append('static const unsigned char presence_root_%d[] = {%s};' %
                     (i, ','.join(map(str, der))))
    lines.append('static const struct { const unsigned char *bytes; size_t size; } presence_root_spans[] = {')
    lines.extend(' {presence_root_%d,sizeof(presence_root_%d)},' % (i, i) for i in range(len(blocks)))
    lines.append('};')
    return '\n'.join(lines) + '\n'
