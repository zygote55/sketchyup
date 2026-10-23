"""Runs ifcopenshell.validate (EXPRESS WHERE rules on) over an IFC file and prints the statements it makes.

    python -I validate_ifcos.py FILE.ifc [MAX_STATEMENTS]

Needs the PyPI packages ifcopenshell and pytest (the rule executor imports pytest's assertion rewriting). See README.md.
"""
import sys, json
import ifcopenshell, ifcopenshell.validate
path = sys.argv[1]
f = ifcopenshell.open(path)
logger = ifcopenshell.validate.json_logger()
ifcopenshell.validate.validate(f, logger, express_rules=True)
print(path, "ifcopenshell", ifcopenshell.version, "statements:", len(logger.statements))
for s in logger.statements[:int(sys.argv[2]) if len(sys.argv) > 2 else 20]:
    print({k: (str(v)[:900]) for k, v in s.items()})
