"""Mask C++ comments/literals without changing offsets or line numbers."""
import re
TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
def mask_cpp(text: str, *, strings: bool = True) -> str:
    def replace(match):
        token=match.group()
        if not strings and not token.startswith(("//", "/*")):
            return token
        return "".join("\n" if c=="\n" else " " for c in token)
    return TOKEN.sub(replace,text)
