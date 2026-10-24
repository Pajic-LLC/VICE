from pathlib import Path

path = Path('.github/workflows/ci.yml')
text = path.read_text(encoding='utf-8')
old_version = 'if ($manifest.reflection.version -ne 1) {'
new_version = 'if ($manifest.reflection.version -ne 2) {'
if old_version not in text:
    raise SystemExit('reflection version assertion anchor not found')
text = text.replace(old_version, new_version, 1)
old_resource = '''            if ($resource.class -ne $class -or\n                $resource.binding -ne $binding -or'''
new_resource = '''            if ($resource.class -ne $class -or\n                $resource.kind -ne "generic" -or\n                $resource.binding -ne $binding -or'''
if old_resource not in text:
    raise SystemExit('resource assertion anchor not found')
text = text.replace(old_resource, new_resource, 1)
path.write_text(text, encoding='utf-8')
