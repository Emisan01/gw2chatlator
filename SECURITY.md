# Security Policy

GW2Chatlator is a Raidcore Nexus add-on: its DLL runs inside the Guild Wars 2 process, reads the chat through
"Events: Chat" and may download its translator and voice modules from this repository's releases.

## Supported versions

Only the newest release (marked **Latest**) receives fixes. Nexus updates installed copies by itself.

## Reporting a vulnerability

Please report privately through GitHub: **[Report a vulnerability](https://github.com/Emisan01/gw2chatlator/security/advisories/new)**.
Do not open a public issue for it. You get an answer as soon as the maintainer has looked at it – usually within a few
days; a confirmed problem is fixed in a new release and credited in the changelog if you wish.

## What counts

- Anything that could crash or hang the game through chat text, a chat code, a link or another add-on's data
- Code execution or file access through chat input, the translator helper or the voice helper
- A download that is used without its SHA-256 being checked, or from anywhere but this repository's releases (and the
  official Events: Chat release)
- Anything that sends text to the game without the user's keypress (outside Hands free, which the user switches on)
- Chat lines, names or other players' data leaving the PC or being stored – the add-on keeps none of them

## How releases can be checked

Every release lists the SHA-256 of `GW2Chatlator.dll` in its notes and in `manifest.json`; the modules in the
`extras` release are listed in `extras-manifest.json`. The release audit (`tools/verify_release.py --full`) and the
GitHub CI run on every version; their rules are in [AGENTS.md](AGENTS.md).
