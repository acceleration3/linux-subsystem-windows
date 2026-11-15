import re
import sys
import xml.etree.ElementTree as ET

registry, names_file, out_dir = sys.argv[1], sys.argv[2], sys.argv[3]
root = ET.parse(registry).getroot()

commands = {}
for command in root.find("commands").findall("command"):
    proto = command.find("proto")
    name = proto.find("name").text
    ret = "".join(t for t in proto.itertext()).replace(name, "").strip()
    params = []
    for param in command.findall("param"):
        pname = param.find("name")
        head = (param.text or "")
        ptype = param.find("ptype")
        if ptype is not None:
            head += ptype.text + (ptype.tail or "")
        head = head.strip()
        tail = (pname.tail or "").strip()
        if tail.startswith("["):
            head += " *"
        params.append(head)
    commands[name] = (ret, params)

wanted = []
for line in open(names_file):
    line = line.split("#")[0].strip()
    if line:
        wanted.extend(line.split())

lines = []
missing = []
seen = set()
for name in wanted:
    if name in seen:
        continue
    seen.add(name)
    if name not in commands:
        missing.append(name)
        continue
    ret, params = commands[name]
    if params == ["void"]:
        params = []
    parts = ", ".join([ret, name] + params)
    lines.append("    X(%s)" % parts)
if missing:
    sys.stderr.write("unknown commands: %s\n" % " ".join(missing))
    sys.exit(1)

with open(out_dir + "/functions.inc", "w") as out:
    out.write("#define LSW_GL_FUNCTIONS(X) \\\n")
    out.write(" \\\n".join(lines))
    out.write("\n")

renames = {"GL_FALSE": "FALSE_", "GL_TRUE": "TRUE_", "GL_NO_ERROR": "NO_ERROR_"}
done = set()
with open(out_dir + "/enums.inc", "w") as out:
    for enums in root.findall("enums"):
        for enum in enums.findall("enum"):
            api = enum.get("api")
            if api is not None and api != "gl":
                continue
            name = enum.get("name")
            value = enum.get("value")
            if value is None or not name.startswith("GL_"):
                continue
            number = int(value, 0)
            if number < 0 or number > 0xFFFFFFFF:
                continue
            short = renames.get(name, name[3:])
            if short[0].isdigit():
                short = "_" + short
            if short in done:
                continue
            done.add(short)
            out.write('#pragma push_macro("%s")\n#undef %s\nconstexpr GLenum %s = 0x%X;\n#pragma pop_macro("%s")\n' % (short, short, short, number, short))
