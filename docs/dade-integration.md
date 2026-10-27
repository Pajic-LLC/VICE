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

`vice_player` contains one reserved 320-byte package metadata slot. The Editor patches that slot in the staged copy instead of adding a removable sidecar.

The slot records whether DADE is required and, when it is required, the exact DADE asset ID. Non-DADE games get a valid package record too.

The record keeps the existing SHA-256 integrity check for its fields. A staged executable with missing, duplicated, unsupported, or corrupted package metadata is rejected.

The slot is part of the executable image. That matters for platform signing. VICE writes the DADE requirement first, then the final code signature protects the executable bytes that contain it.

On macOS, development staging applies a new ad-hoc signature after the slot is written and verifies it with `codesign --verify`. A release pipeline can then replace that signature with a Developer ID or distribution signature.

On Windows, development builds may stay unsigned. Retail builds should use Authenticode after the slot is written.

Development launches that explicitly pass a project manifest still read `dade.asset` directly. They do not require staged package metadata.

## Signing a staged executable

`vice_package_signer` signs an already stamped executable and checks the DADE record again after the platform signer finishes.

It can also stamp a fresh `vice_player` copy before signing.

macOS ad-hoc example:

```text
vice_package_signer --executable MyGame --stamp-asset com.pajic.vice --identity -
```

macOS release example:

```text
vice_package_signer --executable MyGame --identity "Developer ID Application: Example"
```

Windows example with a certificate in the current user's `My` certificate store:

```text
vice_package_signer.exe --executable MyGame.exe --certificate-sha1 <thumbprint> --signtool <path-to-signtool.exe>
```

A production Windows pipeline can also pass `--timestamp-url` to use an RFC 3161 timestamp server.

The signer fails if the platform signature cannot be created, if platform verification fails, or if the DADE requirement changes during signing.

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

The staged DADE requirement lives inside the executable instead of a removable text file. Changing the requirement means changing signed executable bytes.

The SHA-256 value inside the record catches damaged or casually edited metadata. It is not the authentication boundary. Someone who can deliberately rewrite an unsigned executable can also rewrite the record and its hash.

The platform code signature is the outer trust boundary for a retail executable. The required order is:

```text
copy vice_player
stamp package metadata
sign executable
verify platform signature
re-read and compare package metadata
publish package
```

The package signer follows that order and fails closed when signing, signature verification, or metadata verification fails.

This does not claim that a general-purpose executable cannot be cracked or patched. It prevents the DADE requirement from being disabled by deleting a loose setting while leaving a legitimate signed package intact.

## Why the provider is separate

Keeping the provider behind a small module boundary lets VICE use DADE without making deed verification, owner keys, or device keys part of VICE's public runtime API.

The DADE provider can change its internal implementation without changing the VICE launch boundary.
