#!/usr/bin/env bash
# Protege a branch main do repositório atual (ou REPO=owner/name).
# Requer gh autenticado com permissão admin no repositório.
set -euo pipefail

REPO="${REPO:-$(gh repo view --json nameWithOwner -q .nameWithOwner)}"
BRANCH="${BRANCH:-main}"

echo "Aplicando proteção em ${REPO}:${BRANCH} ..."

# Ruleset moderno (GitHub) — exige checks de CI antes do merge
gh api \
  --method POST \
  -H "Accept: application/vnd.github+json" \
  "/repos/${REPO}/rulesets" \
  -f name='protect-main' \
  -f target='branch' \
  -F enforcement='active' \
  -f 'conditions[ref_name][include][]=refs/heads/main' \
  -f 'rules[][type]=deletion' \
  -f 'rules[][type]=non_fast_forward' \
  -f 'rules[][type]=pull_request' \
  -F 'rules[][parameters][required_approving_review_count]=1' \
  -F 'rules[][parameters][dismiss_stale_reviews_on_push]=true' \
  -F 'rules[][parameters][require_code_owner_review]=false' \
  -F 'rules[][parameters][require_last_push_approval]=false' \
  -F 'rules[][parameters][required_review_thread_resolution]=true' \
  -f 'rules[][type]=required_status_checks' \
  -F 'rules[][parameters][strict_required_status_checks_policy]=true' \
  -f 'rules[][parameters][required_status_checks][][context]=security-scan' \
  -f 'rules[][parameters][required_status_checks][][context]=validate-secrets-template' \
  -f 'rules[][parameters][required_status_checks][][context]=unit-tests' \
  -f 'rules[][parameters][required_status_checks][][context]=firmware-build' \
  >/tmp/ruleset-result.json

echo "Ruleset criado/atualizado. Resumo:"
python3 - <<'PY'
import json
from pathlib import Path
data = json.loads(Path("/tmp/ruleset-result.json").read_text())
print(f"  id={data.get('id')} name={data.get('name')} enforcement={data.get('enforcement')}")
PY

echo
echo "Verifique em: https://github.com/${REPO}/settings/rules"
