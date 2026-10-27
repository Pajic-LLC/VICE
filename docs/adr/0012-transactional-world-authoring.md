# Transactional authored World editing

## Decision

Authoring operations use persistent VICE IDs and the existing WorldSnapshot,
World validator, source parser and deterministic serializer. A bounded candidate
transaction either validates completely or leaves authoritative state untouched.
Editor authoring and development CLI clients share these transaction primitives.

Persisted edits require a SHA-256 revision of the exact authored source bytes.
The same edit path powers dry-run and apply. Apply also compares the complete
source bytes under a local OS-owned publication lock before replacing the file.
Native file operations and hash implementations live behind VICE Platform.
Existing files are replaced through same-directory temporary files. New Editor
Save As targets use atomic create-if-absent publication.

The initial request format is `vice-world-edit 1`, followed by one revision and
ordered semantic operation records. This follows VICE's existing line-based
authored formats and avoids introducing a separate scene representation or
requiring a general JSON dependency. CLI responses use the existing JSON
`vice.dev.v1` envelope. A future JSON or MCP adapter must call this same typed API.

## Constraints

Requests are bounded to 64 KiB and 256 operations. Authored text files are bounded
to 16 MiB. Global positions retain double precision. Canonical paths make symlink
aliases share publication locks. Locks are released by the OS when a process
exits; the empty sidecar file is ignored generated state, not World truth.

Publication serializes cooperating VICE writers, including the Editor. Raw file
writes by external tools must be rebased through a new inspect operation. Source
is checked again immediately before publication, but unrelated programs that
ignore the lock cannot be made participants in a filesystem transaction. Atomic
replacement is a visibility guarantee, not a complete power-loss recovery system.

No-op edits preserve exact existing source bytes and metadata. Changed sources
are canonicalized through the existing serializer, so source comments are not
preserved across semantic changes. Editor saves reject external changes and keep
the unsaved document available for explicit reload or Save As.

## Alternatives and revisit conditions

Direct text rewriting duplicates validation and cannot safely express batches.
Editing cooked files makes derived state authoritative. Runtime memory mutation
has different lifecycle and safety requirements and is not part of this service.

Revisit the text-size bound and source format when representative projects need
larger documents or incremental storage. Add schema versions and migrations before
changing persistent request semantics. Live reload and simulation payloads should
extend shared World contracts rather than create agent-only state.
