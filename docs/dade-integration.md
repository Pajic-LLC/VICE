# DADE launch integration

VICE can gate a game launch on a DADE ownership decision before project game code is loaded.

This is the first integration boundary between VICE and DADE. VICE does not parse digital deeds or hold owner keys. A DADE provider does that work and returns one decision to the runtime.

## Opting a project into DADE

Place a file named `dade.asset` next to the project manifest.

Example:

```text
com.pajic.vice
```

The file contains one DADE asset ID. Blank lines and lines starting with `#` are ignored.

If `dade.asset` is absent, the project launches normally without a DADE check.

If it is present, `vice_player` requires a DADE provider and fails closed if ownership cannot be verified.

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

## Current security boundary

`dade.asset` is integration metadata, not a production anti-tamper mechanism.

A final shipping pipeline must bind the DADE asset ID to signed or otherwise integrity-protected game metadata. Deleting or editing one loose text file must not be enough to turn a protected retail build into an unprotected build.

That packaging work is separate from the runtime provider ABI and should be completed before DADE enforcement is treated as a retail security boundary.

## Why the provider is separate

DADE is still proving its reference implementation and has not frozen a production SDK language or binary ABI.

Keeping the provider behind a small module boundary lets VICE integrate now without copying the Python reference verifier into the engine or making DADE internals part of VICE's public runtime API.

A later native DADE SDK can implement the same provider boundary or replace it cleanly when the production integration is ready.
