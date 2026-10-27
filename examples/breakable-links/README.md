# Breakable Links

Two payloads hang from authored distance links. After one second the game applies
a downward velocity to the left payload. Its finite impulse threshold causes its
link to break; that payload falls to the floor while the independent right link
continues holding its payload. The background changes after the module observes
the local failure. No genre-specific mechanism is built into the engine.

```sh
build/tools/vice_cli/vice project build examples/breakable-links/links.viceproj
build/tools/vice_cli/vice project run examples/breakable-links/links.viceproj
```

Editor Play runs the same module and isolated simulation. Reset/relaunch restores
the authored intact state. Joint lines are not drawn; the logical link entities
are observable through World inspect and Runtime entity debugging:

- weak link: `97000000-0000-4000-8000-000000000203`
- independent link: `97000000-0000-4000-8000-000000000206`

A broken link reports its endpoint IDs, threshold, measured break impulse and
fixed-step index. Saving a running World preserves the break. Playing does not
rewrite authored source. Distance links currently join body centers in one
physical region; zero break threshold means unbreakable.
