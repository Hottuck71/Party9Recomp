# Publishing on GitHub

## The first time

1. On github.com, signed in: **New repository**, name it (for example `PartyRecomp`), public, **without** a
   README, licence, or .gitignore (this folder has them).
2. In this folder (PowerShell), with the address GitHub shows:

```
git add .
git commit -m "PartyRecomp 1.0.0"
git branch -M main
git remote add origin https://github.com/<your-username>/PartyRecomp.git
git push -u origin main
```

## Each release

1. Make it: `powershell -ExecutionPolicy Bypass -File scripts\make_release.ps1 -Toolkit "D:\Wii Party PC"`. It
   copies only the toolkit's own files, searches every file for this PC's user name and user folders, and makes
   `work\release\PartyRecomp-<version>.zip` and `PartyRecomp-<version>-Setup.exe`.
2. On GitHub: **Releases > Draft a new release**, tag `v<version>`, and attach both (GitHub allows files up to
   2 GB), or `gh release create v<version> <zip> <setup.exe>`.

## Never upload

- The game, a disc image, or anything extracted from it.
- A built `PartyRecomp-Game.exe` (or any other build of the game), or the `build\` folder: they're made from the
  game's code.
- Save files, shader caches, or reports from playing.

The repository's `.gitignore` keeps these out, and `scripts\make_release.ps1` refuses to make a toolkit that
contains any of them. Before each push, `git status` shows exactly what would go up.
