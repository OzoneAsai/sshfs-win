# README handoff: WiX v7 packaging

The implementation now packages with the SDK-style WiX v7 project in
`sshfs-win.wixproj`; `Makefile` invokes it through `dotnet build`. The README
still says that the project uses the WiX v3 `candle`/`heat`/`light` pipeline and
describes WiX v7 as future work. That section is intentionally left unchanged
for a follow-up documentation agent.

The README follow-up should:

- replace the stale `Packaging toolchain status` paragraph with the current
  WiX v7 SDK/MSBuild workflow;
- state that a supported .NET SDK and NuGet package restore are required;
- retain the distinction between a signed release build and the local-only
  `AllowUnsigned=1` path;
- mention that the verified Windows build used portable .NET SDK 10.0.401 and
  produced `.build/x64/dist/sshfs-win-4.0.0-x64.msi`;
- avoid changing dependency source locks: SSHFS remains pinned to commit
  `24448e2493533ead984d6ca322c583e1a26cc613`, tree
  `35655f60d37403663238a73b4204cfd64fdca73c`.
