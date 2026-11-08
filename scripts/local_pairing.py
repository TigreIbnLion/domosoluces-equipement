Import("env")
import os
import re

code = os.environ.get("DOMO_PAIRING_CODE", "")
if code:
    if not re.fullmatch(r"[A-Za-z0-9]{8,63}", code):
        raise ValueError("DOMO_PAIRING_CODE must be 8-63 ASCII letters or digits")
    env.Append(CPPDEFINES=[("DOMO_PROVISIONING_CODE", '\\"' + code + '\\"')])
