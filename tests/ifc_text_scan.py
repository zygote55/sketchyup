#!/usr/bin/env python3
"""A raw-text scan of an IFC STEP file that shares no code with IfcOpenShell or SketchyUp (ADR 0171
section 6.2). It reads the instance lines with a small recursive-descent parser and reports what the
tests compare the import against: entity counts per class, units, the spatial tree by GlobalId, property
and quantity values (occurrence and type level), group membership, classification and layers.

    python3 -I tests/ifc_text_scan.py FILE.ifc          prints the facts as JSON
"""
import json
import re
import sys


class Ref(int):
    pass


class Enum(str):
    pass


class Typed:
    def __init__(self, name, args):
        self.name = name
        self.args = args

    def __repr__(self):
        return "%s(%r)" % (self.name, self.args)


def decode_string(raw):
    """Decodes the STEP escapes of a string body (without its quotes)."""
    raw = raw.replace("''", "'")
    out = []
    i = 0
    while i < len(raw):
        if raw.startswith("\\X2\\", i):
            end = raw.index("\\X0\\", i)
            hexes = raw[i + 4:end]
            out.append(bytes.fromhex(hexes).decode("utf-16-be"))
            i = end + 4
        elif raw.startswith("\\X4\\", i):
            end = raw.index("\\X0\\", i)
            hexes = raw[i + 4:end]
            out.append(bytes.fromhex(hexes).decode("utf-32-be"))
            i = end + 4
        elif raw.startswith("\\S\\", i):
            out.append(chr(ord(raw[i + 3]) + 128))
            i += 4
        elif raw.startswith("\\X\\", i):
            out.append(chr(int(raw[i + 3:i + 5], 16)))
            i += 5
        elif raw.startswith("\\\\", i):
            out.append("\\")
            i += 2
        else:
            out.append(raw[i])
            i += 1
    return "".join(out)


_number = re.compile(r"[+-]?(?:\d+\.?\d*(?:[eE][+-]?\d+)?|\.\d+(?:[eE][+-]?\d+)?)")
_word = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def parse_value(text, at):
    """Parses one STEP parameter starting at text[at]; returns (value, next index)."""
    while text[at] in " \t\r\n":
        at += 1
    c = text[at]
    if c == "$":
        return None, at + 1
    if c == "*":
        return "*", at + 1
    if c == "#":
        m = re.compile(r"#(\d+)").match(text, at)
        return Ref(int(m.group(1))), m.end()
    if c == ".":
        m = re.compile(r"\.([A-Za-z0-9_]+)\.").match(text, at)
        return Enum(m.group(1)), m.end()
    if c == "'":
        i = at + 1
        while True:
            j = text.index("'", i)
            if j + 1 < len(text) and text[j + 1] == "'":
                i = j + 2
                continue
            return decode_string(text[at + 1:j]), j + 1
    if c == "(":
        items = []
        at += 1
        while True:
            while text[at] in " \t\r\n":
                at += 1
            if text[at] == ")":
                return items, at + 1
            value, at = parse_value(text, at)
            items.append(value)
            while text[at] in " \t\r\n":
                at += 1
            if text[at] == ",":
                at += 1
    m = _number.match(text, at)
    if m and (c.isdigit() or c in "+-."):
        s = m.group(0)
        return (float(s) if re.search(r"[.eE]", s) else int(s)), m.end()
    m = _word.match(text, at)
    if m:
        name = m.group(0)
        at = m.end()
        while text[at] in " \t\r\n":
            at += 1
        if text[at] == "(":
            args, at = parse_value(text, at)
            return Typed(name.upper(), args), at
        return name, at
    raise ValueError("cannot parse %r" % text[at:at + 30])


def statements(data):
    """Yields (id, entity name, argument list) for every instance line of the DATA section."""
    start = data.index("DATA;") + 5
    end = data.rindex("ENDSEC;")
    body = re.sub(r"/\*.*?\*/", "", data[start:end], flags=re.S)
    at = 0
    n = len(body)
    while at < n:
        while at < n and body[at] in " \t\r\n":
            at += 1
        if at >= n:
            break
        m = re.compile(r"#(\d+)\s*=\s*([A-Za-z0-9_]+)\s*").match(body, at)
        if not m:
            raise ValueError("expected an instance at %r" % body[at:at + 40])
        at = m.end()
        if body[at] == "(":
            args, at = parse_value(body, at)
        else:
            args = []
        while body[at] in " \t\r\n":
            at += 1
        assert body[at] == ";", body[at:at + 20]
        at += 1
        yield int(m.group(1)), m.group(2).upper(), args


class Scan:
    def __init__(self, path):
        with open(path, "rb") as f:
            raw = f.read()
        self.sha = None
        text = raw.decode("latin-1")
        self.entities = {}
        self.order = []
        for step, name, args in statements(text):
            self.entities[step] = (name, args)
            self.order.append(step)
        self.counts = {}
        for step in self.order:
            name = self.entities[step][0]
            self.counts[name] = self.counts.get(name, 0) + 1
        self.schema = re.search(r"FILE_SCHEMA\s*\(\s*\(\s*'([^']*)'", text, re.I).group(1).upper()

    # ---- helpers -----------------------------------------------------------------------------
    def of(self, *names):
        for step in self.order:
            if self.entities[step][0] in names:
                yield step, self.entities[step][1]

    def arg(self, step, index):
        args = self.entities[step][1]
        return args[index] if index < len(args) else None

    def name(self, step):
        return self.entities[step][0]

    # ---- units ---------------------------------------------------------------------------------
    PREFIX = {"EXA": 1e18, "PETA": 1e15, "TERA": 1e12, "GIGA": 1e9, "MEGA": 1e6, "KILO": 1e3, "HECTO": 1e2,
              "DECA": 1e1, "DECI": 1e-1, "CENTI": 1e-2, "MILLI": 1e-3, "MICRO": 1e-6, "NANO": 1e-9,
              "PICO": 1e-12, "FEMTO": 1e-15, "ATTO": 1e-18}

    def unit_value(self, step, depth=0):
        name, args = self.entities[step]
        if name == "IFCSIUNIT":
            prefix = self.PREFIX.get(args[2], 1.0) if args[2] else 1.0
            return prefix
        if name == "IFCCONVERSIONBASEDUNIT":
            measure = self.entities[args[3]][1]
            value = measure[0].args[0] if isinstance(measure[0], Typed) else measure[0]
            return float(value) * self.unit_value(measure[1], depth + 1)
        raise ValueError(name)

    def units(self):
        project = next(self.of("IFCPROJECT"))[0]
        assignment = self.arg(project, 8)
        result = {"length": None, "area": 1.0, "volume": 1.0}
        for ref in self.entities[assignment][1][0]:
            if ref not in self.entities:
                continue
            name, args = self.entities[ref]
            kind = args[1] if len(args) > 1 else None
            if name in ("IFCSIUNIT", "IFCCONVERSIONBASEDUNIT"):
                if kind == "LENGTHUNIT":
                    result["length"] = self.unit_value(ref)
                elif kind == "AREAUNIT":
                    result["area"] = self.unit_value(ref)
                elif kind == "VOLUMEUNIT":
                    result["volume"] = self.unit_value(ref)
        return result

    # ---- values --------------------------------------------------------------------------------
    def value(self, v, units, own_unit=None):
        """A property value as the import stores it: str, bool or float (None: not mapped)."""
        if isinstance(v, Typed):
            tname = v.name
            inner = v.args[0] if v.args else None
            if isinstance(inner, Enum):
                if inner == "T":
                    return True
                if inner == "F":
                    return False
                return None  # UNKNOWN
            if isinstance(inner, str):
                return inner
            if isinstance(inner, (int, float)):
                scale = 1.0
                if tname in ("IFCLENGTHMEASURE", "IFCPOSITIVELENGTHMEASURE", "IFCNONNEGATIVELENGTHMEASURE"):
                    scale = units["length"]
                elif tname == "IFCAREAMEASURE":
                    scale = units["area"]
                elif tname == "IFCVOLUMEMEASURE":
                    scale = units["volume"]
                if own_unit is not None and tname in ("IFCLENGTHMEASURE", "IFCPOSITIVELENGTHMEASURE", "IFCAREAMEASURE", "IFCVOLUMEMEASURE"):
                    scale = own_unit
                return float(inner) * scale
        return None

    # ---- the spatial tree and properties by GlobalId -------------------------------------------
    def root_info(self, step):
        name, args = self.entities[step]
        return {"step": step, "class": name, "guid": args[0], "name": args[2]}

    def facts(self):
        units = self.units()
        facts = {"entities": len(self.order), "classes": self.counts, "units": units, "schema": self.schema}
        rooted = {}
        for step in self.order:
            name, args = self.entities[step]
            if len(args) > 2 and isinstance(args[0], str) and len(args[0]) == 22 and name not in ("IFCPROPERTYSET", "IFCELEMENTQUANTITY") and not name.startswith("IFCREL") and not name.startswith("IFCPROPERTY"):
                rooted[step] = self.root_info(step)
        # An object whose GlobalId is malformed is still an object: anything a spatial relationship names.
        for step, args in self.of("IFCRELAGGREGATES"):
            for ref in [args[4]] + list(args[5]):
                if ref not in rooted and ref in self.entities and len(self.entities[ref][1]) > 2 and isinstance(self.entities[ref][1][0], str):
                    rooted[ref] = self.root_info(ref)
        for step, args in self.of("IFCRELCONTAINEDINSPATIALSTRUCTURE"):
            for ref in [args[5]] + list(args[4]):
                if ref not in rooted and ref in self.entities and len(self.entities[ref][1]) > 2 and isinstance(self.entities[ref][1][0], str):
                    rooted[ref] = self.root_info(ref)
        parent, order = {}, {}
        for step, args in self.of("IFCRELAGGREGATES"):
            for child in args[5]:
                parent.setdefault(child, args[4])
                order.setdefault(args[4], []).append(child)
        for step, args in self.of("IFCRELCONTAINEDINSPATIALSTRUCTURE"):
            for child in args[4]:
                parent.setdefault(child, args[5])
                order.setdefault(args[5], []).append(child)
        facts["parents"] = {rooted[c]["guid"]: rooted[p]["guid"] for c, p in parent.items() if c in rooted and p in rooted}
        facts["children"] = {rooted[p]["guid"]: [rooted[c]["guid"] for c in cs if c in rooted] for p, cs in order.items() if p in rooted}
        facts["objects"] = {r["guid"]: {"class": r["class"], "name": r["name"], "step": r["step"]} for r in rooted.values()}
        seen, dup = set(), set()
        for r in rooted.values():
            (dup if r["guid"] in seen else seen).add(r["guid"])
        facts["duplicateGuids"] = sorted(dup)
        # type and occurrence property sets
        psets = {}
        for step, args in self.of("IFCRELDEFINESBYPROPERTIES"):
            for obj in args[4]:
                psets.setdefault(obj, []).append(args[5])
        types = {}
        for step, args in self.of("IFCRELDEFINESBYTYPE"):
            for obj in args[4]:
                types[obj] = args[5]

        def read_set(set_step, origin):
            name, args = self.entities[set_step]
            result = {}
            if name == "IFCPROPERTYSET":
                for p in args[4]:
                    pname, pargs = self.entities[p]
                    if pname == "IFCPROPERTYSINGLEVALUE" and pargs[2] is not None:
                        own = self.unit_value(pargs[3]) if isinstance(pargs[3], Ref) and self.entities[pargs[3]][0] in ("IFCSIUNIT", "IFCCONVERSIONBASEDUNIT") and self.entities[pargs[3]][1][1] in ("LENGTHUNIT", "AREAUNIT", "VOLUMEUNIT") else None
                        v = self.value(pargs[2], units, own)
                        if v is not None:
                            result["%s.%s" % (args[2], pargs[0])] = v
                    elif pname == "IFCPROPERTYENUMERATEDVALUE":
                        values = [self.value(x, units) for x in pargs[2]]
                        result["%s.%s" % (args[2], pargs[0])] = ", ".join(str(x) if not isinstance(x, bool) else ("TRUE" if x else "FALSE") for x in values)
            elif name == "IFCELEMENTQUANTITY":
                for q in args[5]:
                    qname, qargs = self.entities[q]
                    own = None
                    if isinstance(qargs[2], Ref) and self.entities[qargs[2]][0] in ("IFCSIUNIT", "IFCCONVERSIONBASEDUNIT"):
                        own = self.unit_value(qargs[2])
                    value = qargs[3]
                    if qname == "IFCQUANTITYLENGTH":
                        result["%s.%s" % (args[2], qargs[0])] = float(value) * (own if own is not None else units["length"])
                    elif qname == "IFCQUANTITYAREA":
                        result["%s.%s" % (args[2], qargs[0])] = float(value) * (own if own is not None else units["area"])
                    elif qname == "IFCQUANTITYVOLUME":
                        result["%s.%s" % (args[2], qargs[0])] = float(value) * (own if own is not None else units["volume"])
                    elif qname in ("IFCQUANTITYCOUNT", "IFCQUANTITYWEIGHT", "IFCQUANTITYTIME"):
                        result["%s.%s" % (args[2], qargs[0])] = float(value)
            return result

        occurrence, typed = {}, {}
        for obj, sets in psets.items():
            if obj in rooted:
                merged = {}
                for s in sets:
                    merged.update(read_set(s, 0))
                occurrence[rooted[obj]["guid"]] = merged
        for obj, t in types.items():
            if obj in rooted and t in self.entities:
                merged = {}
                for s in self.entities[t][1][5] if len(self.entities[t][1]) > 5 and isinstance(self.entities[t][1][5], list) else []:
                    if s in self.entities:
                        merged.update(read_set(s, 2))
                typed[rooted[obj]["guid"]] = {"name": self.entities[t][1][2], "properties": merged}
        facts["occurrenceProperties"] = occurrence
        facts["typeProperties"] = typed
        # groups
        groups = {}
        for step, args in self.of("IFCRELASSIGNSTOGROUP"):
            group = args[6]
            gname = self.entities[group][1][2]
            for obj in args[4]:
                if obj in rooted:
                    groups.setdefault(rooted[obj]["guid"], []).append(gname)
        facts["groups"] = groups
        # classification
        classes = {}
        for step, args in self.of("IFCRELASSOCIATESCLASSIFICATION"):
            ref = args[5]
            rname, rargs = self.entities[ref]
            entry = {"class": rname, "location": rargs[0] if rname == "IFCCLASSIFICATIONREFERENCE" else None,
                     "identification": rargs[1] if rname == "IFCCLASSIFICATIONREFERENCE" else None,
                     "name": rargs[2] if rname == "IFCCLASSIFICATIONREFERENCE" else rargs[3]}
            for obj in args[4]:
                if obj in rooted:
                    classes.setdefault(rooted[obj]["guid"], []).append(entry)
        facts["classification"] = classes
        # presentation layers
        layers = {}
        for step, args in self.of("IFCPRESENTATIONLAYERASSIGNMENT", "IFCPRESENTATIONLAYERWITHSTYLE"):
            for item in args[2]:
                layers.setdefault(item, []).append({"name": args[0], "on": args[5] if len(args) > 5 else None, "frozen": args[6] if len(args) > 6 else None})
        facts["layers"] = {}
        for step in self.order:
            name, args = self.entities[step]
            if step in rooted and len(args) > 6 and isinstance(args[6], Ref) and args[6] in self.entities and self.entities[args[6]][0] == "IFCPRODUCTDEFINITIONSHAPE":
                reps = self.entities[args[6]][1][2]
                found = []
                for rep in reps:
                    found += layers.get(rep, [])
                    for item in self.entities[rep][1][3]:
                        found += layers.get(item, [])
                if found:
                    facts["layers"][rooted[step]["guid"]] = found
        # map conversion
        maps = []
        for step, args in self.of("IFCMAPCONVERSION"):
            maps.append({"eastings": args[2], "northings": args[3], "orthogonalHeight": args[4], "xAxisAbscissa": args[5], "xAxisOrdinate": args[6], "scale": args[7]})
        facts["mapConversions"] = maps
        return facts


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    print(json.dumps(Scan(argv[1]).facts(), sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
