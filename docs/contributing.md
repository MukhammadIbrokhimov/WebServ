# WebServ Contribution Guidelines

## Quick Start: Branch & PR Workflow

**Main branch is protected.** All work must go through Pull Requests.

---

## 1. Create a Feature Branch

```bash
# Update main to latest
git checkout main
git pull origin main

# Create feature branch with clear naming
git checkout -b <branch-name>
```

### Branch Naming Convention

```
<type>/<feature-name>

Examples:
- feature/socket-wrapper
- feature/config-parser
- bugfix/request-parsing-headers
- test/stress-testing-suite
```

**Types:**
- `feature/` - New feature implementation
- `bugfix/` - Bug fix
- `test/` - Tests and testing infrastructure
- `refactor/` - Code refactoring or optimization
- `docs/` - Documentation updates

---

## 2. Commit Meaningfully

Keep commits small, focused, and logical.

```bash
# Good commit messages
git commit -m "feat: implement Socket wrapper with bind/listen/accept"
git commit -m "feat: add non-blocking flag setup in Socket::setNonBlocking()"
git commit -m "test: add Socket unit tests"

# Bad commit messages ❌
git commit -m "stuff"
git commit -m "fixes"
git commit -m "WIP"
```

### Commit Message Format (Conventional Commits)

```
<type>: <subject>

<body (optional)>

Example:
feat: implement Socket wrapper class

- Add Socket constructor with AF_INET, SOCK_STREAM
- Implement bind(), listen(), accept() methods
- Add setNonBlocking() with fcntl(O_NONBLOCK)
- Include error handling with WebServException

Related to task: 1.1
```

**Types:**
- `feat:` - New feature
- `fix:` - Bug fix
- `test:` - Tests
- `refactor:` - Code refactoring
- `docs:` - Documentation
- `chore:` - Build, dependencies, etc.

---

## 3. Push Your Branch

```bash
# Push local branch to remote
git push origin <branch-name>

# If branch rejected, force push only if you're sure (NOT recommended on main)
git push -u origin <branch-name>
```

---

## 4. Open a Pull Request (PR)

Go to GitHub and create a PR from your branch to `main`.

### PR Title Format

```
[Phase X] Task X.Y: Brief description

Examples:
[Phase 1] Task 1.1: Socket Wrapper Implementation
[Phase 2] Task 2.3: Static File Serving Handler
[Phase 4] Task 4.1: Connection Keep-Alive & Timeout
```

### PR Description Template

```markdown
## Task
- **Kanban Task:** Task 1.1 (reference from kanban.md)
- **Assignee:** Dev A
- **Phase:** Phase 1

## Description
Brief explanation of what was implemented.

## Changes
- Implementation detail 1
- Implementation detail 2
- Implementation detail 3

## Testing
- [x] Compiles without warnings
- [x] Tested locally with [describe test]
- [x] Valgrind clean (no leaks)
- [x] [Any relevant stress test]

---

## 5. Code Review & Merging

**Before merging:**

1. **Resolve Merge Conflicts** (if any)
   ```bash
   git fetch origin
   git merge origin/main
   # Fix conflicts manually
   git add .
   git commit -m "Resolve merge conflicts with main"
   git push origin <branch-name>
   ```

---

## Full Example Workflow

```bash
# Step 1: Start fresh
git checkout main
git pull origin main

# Step 2: Create feature branch
git checkout -b feature/socket-wrapper

# Step 3: Make changes
# ... edit files ...

# Step 4: Commit with meaningful message
git add src/socket/socket.cpp includes/socket.hpp
git commit -m "feat: implement Socket wrapper with bind/listen/accept"

git add src/socket/socket.cpp
git commit -m "feat: add non-blocking flag setup in Socket::setNonBlocking()"

# Step 5: Test before push
make clean && make
valgrind --leak-check=full ./webserv

# Step 6: Push to remote
git push -u origin feature/socket-wrapper

# Step 7: Open PR on GitHub
# - Go to GitHub repo
# - Click "Compare & pull request"
# - Fill in PR title and description
# - Request review from other devs

# Step 8: Address review feedback, push updates
# (commits automatically added to PR)

# Step 9: Merge to main
---

## Handling Conflicts

If your branch conflicts with main:

```bash
# Option 1: Rebase (cleaner history)
git fetch origin
git rebase origin/main
# Resolve conflicts in editor
git add .
git rebase --continue
git push origin <branch-name> --force-with-lease

# Option 2: Merge (simpler)
git fetch origin
git merge origin/main
# Resolve conflicts
git add .
git commit -m "Resolve merge conflicts"
git push origin <branch-name>
```
