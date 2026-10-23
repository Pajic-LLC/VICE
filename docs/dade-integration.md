# DADE launch integration

VICE can gate a game launch on a DADE ownership decision before project game code is loaded.

VICE does not parse digital deeds or hold owner keys. A DADE provider does that work and returns one decision to the runtime.

## Opting a project into DADE

During development, place a file named `dade.asset` next to the project manifest.

Example:

```text
com.pajic.vice
```

The file contains one DADE asset ID. Blank lines and lines starting with `#` are ignored.

If `dade.asset` is absent, the project stays non-DADE.

If it is present, VICE requires a DADE provider and fails closed if ownership cannot be verified.

`dade.asset` is authoring input. It is not copied into a staged game as the ownership switch.

## Staged package metadata

Every staged VICE executable carries a fixed package metadata stamp at the end of the executable. The stamp records whether DADE is required and, when it is required, the exact DADE asset ID.

The Editor writes this stamp after it copies the runtime host into the game output. Non-DADE games are stamped too. This means deleting package metadata cannot turn a DADE game into a non-DADE game. A staged executable with missing, truncated, unsupported, or corrupted package metadata is rejected.

The stamp includes a SHA-256 integrity value for the stamped fields. VICE checks the stamp before it parses the staged project manifest, before it loads the DADE provider, and before it loads project game code.

Development launches that explicitly pass a project manifest still read `dade.asset` directly. They do not require a staged package stamp.

## Provider location

Staged runtimes look for the provider beside the runtime modules:

```text
<runtime>/modules/dade_provider.dylib
<runtime>/modules/dade_provider.dll
```

The platform extension is selected by VICE.

The provider implements the small ABI in `vice/runtime/dade_provider_api.hpp` and exports `vice_dade_verify`.

VICE passes:

- the DADE asset ID
- the current platform
- the project root

The provider returns a DADE decision code and a diagnostic string.

Only `owned` allows launch.

## What VICE does not do

VICE does not receive owner private keys.

VICE does not receive device private keys.

VICE does not decide whether a deed signature is valid.

VICE does not contact a Pajic account or store account.

VICE does not turn a failed DADE check into a network activation flow.

The provider is responsible for using the DADE ownership proof stack and locally available trust state.

## Security boundary

The staged DADE requirement now lives inside the executable instead of a removable text file. Changing the requirement means changing the executable itself.

The SHA-256 value inside the stamp detects damaged or casually edited metadata. It is not a substitute for executable signing because someone who can deliberately rewrite the executable can also rewrite the stamp and its hash.

A retail pipeline should stamp first and code-sign the final executable after that. The platform signature is the outer trust boundary for the executable bytes. VICE then treats missing or invalid package metadata as a boot failure.

This layer does not claim that a general-purpose executable cannot be patched. Its job is narrower: the DADE requirement cannot be disabled by deleting or editing a loose package-side setting while leaving the executable untouched.

## Why the provider is separate

Keeping the provider behind a small module boundary lets VICE use DADE without making deed verification, owner keys, or device keys part of VICE's public runtime API.

The DADE provider can change its internal implementation without changing the VICE launch boundary.
