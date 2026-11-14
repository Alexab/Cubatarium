"""Resolve local glTF URI paths, decoding percent escapes before containment checks."""
from pathlib import Path
from urllib.parse import unquote,urlsplit


def resolve_resource(folder,uri):
    if not isinstance(uri,str) or not uri or '\\' in uri:raise ValueError('Expected a local relative glTF URI')
    parts=urlsplit(uri)
    if parts.scheme or parts.netloc or parts.query or parts.fragment:raise ValueError('Only local glTF resource paths without query/fragment are supported')
    name=unquote(parts.path,errors='strict')
    if not name or ':' in name or '\\' in name or Path(name).is_absolute():raise ValueError('Expected a local relative glTF URI')
    root=Path(folder).resolve();path=(root/name).resolve()
    if not path.is_relative_to(root) or path==root or not path.is_file():raise ValueError(f'Missing or escaping glTF resource: {uri}')
    return path
