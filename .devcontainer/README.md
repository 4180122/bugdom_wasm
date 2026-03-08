# Dev Container Setup

## Git credentials

The devcontainer mounts your host's `~/.gitconfig` and `~/.ssh` so git uses your identity and keys.

**Prerequisites on your host:**

1. **Git config** (if not set):
   ```bash
   git config --global user.name "Your Name"
   git config --global user.email "your@email.com"
   ```

2. **SSH keys** – Ensure `~/.ssh` exists and contains your keys. For SSH auth, Cursor/VS Code usually forwards your SSH agent automatically. If git push over SSH fails:
   - **macOS**: Start Keychain Access and ensure ssh-agent is running (`ssh-add -l` should list keys)
   - **Linux**: Run `eval "$(ssh-agent)"` and `ssh-add` before opening the devcontainer
   - **HTTPS**: Use a [credential helper](https://docs.github.com/get-started/getting-started-with-git/caching-your-github-credentials-in-git) on your host; it’s shared with the container

3. **Key permissions** – SSH requires `~/.ssh` to be 700 and key files to be 600. Fix on your host if needed:
   ```bash
   chmod 700 ~/.ssh
   chmod 600 ~/.ssh/id_*
   ```

If `~/.gitconfig` or `~/.ssh` is missing, the container may fail to start. Create minimal versions if needed:
```bash
touch ~/.gitconfig   # or run: git config --global user.name "x"
mkdir -p ~/.ssh
```
