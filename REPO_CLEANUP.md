# Repo cleanup guide

This repo has external libs as submodules. Here are options to clean things up for deployment (e.g. GitHub Pages).

## Current state

- **extern/Pomme**: Submodule (jorio/Pomme) with local modifications (WASM fixes)
- **extern/SDL**: Submodule (libsdl-org/SDL) with local modifications (WebGL alpha/powerPreference)
- **patches/pomme-wasm.patch**: Captures the Pomme modifications for clean application.
- **patches/sdl-emscripten.patch**: Captures the SDL emscripten modifications for clean application.

---

## Option A: Fix submodules (recommended for upstream updates)

Keeps submodule workflow. Works with `git clone --recursive` and GitHub Actions.

### 1. SDL is now in .gitmodules

SDL has been added back. Verify:

```bash
git submodule status
# Should show both extern/Pomme and extern/SDL
```

### 2. Pomme: fork + push, or apply patch

**2a. Fork approach (cleanest)**  
- Fork https://github.com/jorio/Pomme on your GitHub  
- Push the modified Pomme files to your fork  
- Update `.gitmodules`: change Pomme URL to your fork  
- In parent: `git submodule update --remote extern/Pomme` then commit the new ref  

**2b. Patch approach (no fork needed)**  
- Commit Pomme changes in the submodule, then update parent ref (changes stay local)  
- Or: reset Pomme and SDL to upstream, apply patches in CI/build:

```bash
# In your build script or Makefile, after submodule init:
make submodule-setup
```

### 3. Makefile target for submodule setup

Add to Makefile:

```makefile
submodule-setup:
	git submodule update --init --recursive
	# Patches for Pomme and SDL (see patches/README.md)
```

---

## Option B: Vendor (no submodules)

Single clone, no submodule setup. Larger repo, harder to update upstream.

### 1. Remove submodule structure

```bash
# Deinit and remove (back up first!)
git submodule deinit -f extern/Pomme extern/SDL
rm -rf .git/modules/extern/Pomme .git/modules/extern/SDL
git rm extern/Pomme extern/SDL  # removes from index
```

### 2. Add as regular directories

```bash
# Re-clone without .git, or copy from your current state:
rm -rf extern/Pomme extern/SDL
git clone --depth 1 https://github.com/jorio/Pomme extern/Pomme
rm -rf extern/Pomme/.git
git clone --depth 1 https://github.com/libsdl-org/SDL extern/SDL
rm -rf extern/SDL/.git

# Apply patches (from repo root; git apply works on git-formatted patches)
git apply --directory=extern/Pomme patches/pomme-wasm.patch
git apply --directory=extern/SDL patches/sdl-emscripten.patch

git add extern/Pomme extern/SDL
git commit -m "Vendor Pomme and SDL (modified)"
```

### 3. Remove .gitmodules

```bash
rm .gitmodules
git add .gitmodules
git commit -m "Remove submodules (vendored)"
```

---

## GitHub Pages deployment

For GitHub Actions to build WASM:

1. **Submodules** (Option A): In your workflow, run `git submodule update --init --recursive` before `make wasm`.
2. **Vendored** (Option B): No extra steps.

Example workflow step:

```yaml
- uses: actions/checkout@v4
  with:
    submodules: recursive  # if using Option A
```
