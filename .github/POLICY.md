# PRs

Branch off `main` as `<tool>/<feature>` (e.g. `pdnkit/spice-export`)
or `chore/<thing>`. Use a worktree so multiple branches can be active
without filesystem races:

```
git worktree add ../circuitcore-<tag> -b <tool>/<feature> main
```

Rebase off `main`, don't merge.

PRs that touch only `pdnkit/` or `sikit/` self-merge when CI is green:

```
gh pr merge --auto --squash
```

PRs that touch `board/`, `sexpr/`, `formats/`, top-level config, or
`.github/` trigger CODEOWNERS and wait for review.

# Branch protection (one-time, repo admin)

After this lands on `main`:

```
gh api repos/UnsignedChad/circuitcore/branches/main/protection \
  --method PUT \
  --input - <<JSON
{
  "required_status_checks": {
    "strict": true,
    "contexts": ["build + test (gcc-13, Ubuntu 24.04)"]
  },
  "enforce_admins": false,
  "required_pull_request_reviews": {
    "require_code_owner_reviews": true,
    "required_approving_review_count": 0
  },
  "restrictions": null,
  "allow_force_pushes": false,
  "allow_deletions": false,
  "required_linear_history": true,
  "required_conversation_resolution": true
}
JSON
```

`require_code_owner_reviews: true` plus `required_approving_review_count: 0`
means: CODEOWNERS approval required when triggered, no review needed
otherwise (CI green is the bar for un-owned paths).
